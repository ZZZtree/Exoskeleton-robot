#!/usr/bin/env python3
"""
ESP32-P4 H.264 视频流端到端延迟测量工具

本工具提供多种延迟测量方法，帮助评估和优化端到端延迟。

使用方法：
    python latency_measurement.py <ESP32_IP地址> [选项]

选项：
    --method <method>    测量方法 (默认: all)
                         all      - 运行所有测量方法
                         rtt      - 网络 RTT 测量
                         decode   - 解码+渲染延迟测量
                         visual   - 视觉延迟测量（需要摄像头对准屏幕）
    --duration <秒>      测量持续时间 (默认: 30)
    --output <文件>      输出结果到文件

测量方法说明：
====================

方法 1: 网络 RTT 测量 (rtt)
----------------------------
原理：通过 UDP 往返时间估算网络延迟。
- 客户端发送时间戳包到 ESP32
- ESP32 回显该包
- 客户端计算 RTT = 接收时间 - 发送时间
- 单向网络延迟 ≈ RTT / 2

方法 2: 解码+渲染延迟测量 (decode)
------------------------------------
原理：测量从收到完整 UDP 帧到 OpenCV 显示的时间。
- 记录帧接收完成的时间戳 T_recv
- 记录帧显示的时间戳 T_display
- 解码渲染延迟 = T_display - T_recv

方法 3: 视觉延迟测量 (visual)
------------------------------
原理：使用摄像头拍摄屏幕上的计时器，比较实际时间和显示时间。
- 在屏幕上显示毫秒级计时器
- 用另一个摄像头拍摄屏幕
- 比较计时器显示值和实际值
- 这是最准确的端到端延迟测量方法

延迟预算分析（目标 120ms）：
=============================
| 阶段                | 预算(ms) | 说明                          |
|--------------------|---------|-------------------------------|
| 摄像头采集          | 5       | ISP 输出 YUV420               |
| H.264 编码          | 8       | 硬件编码器，全 IDR 模式        |
| UDP 网络传输        | 20      | Wi-Fi 5/6 局域网               |
| UDP 接收+帧组装     | 5       | 客户端接收和组帧               |
| H.264 解码          | 10      | PyAV/FFmpeg 软件解码           |
| OpenCV 显示         | 5       | 图像变换+渲染                  |
| 缓冲/抖动余量       | 67      | 用于吸收网络抖动和调度延迟      |
| 总计                | 120     | 目标端到端延迟                  |
"""

import socket
import time
import struct
import sys
import threading
import os
import argparse
import json
from datetime import datetime
from collections import deque

# ============ 协议常量 ============
H264_UDP_PORT = 1235
H264_UDP_HEADER = 8
UDP_MTU = 1460

H264_FLAG_SINGLE = 0x03
H264_FLAG_FIRST = 0x00
H264_FLAG_MIDDLE = 0x01
H264_FLAG_LAST = 0x02

H264_PKT_TYPE_VIDEO = 0
H264_PKT_TYPE_SPSPPS = 1
H264_PKT_TYPE_READY = 0xFE
H264_PKT_TYPE_ACK = 0xFD

# 时间戳包类型（用于 RTT 测量）
PKT_TYPE_TIMESTAMP = 0xFC


class LatencyMeasurer:
    """延迟测量器"""

    def __init__(self, server_ip, port=H264_UDP_PORT):
        self.server_ip = server_ip
        self.port = port
        self.sock = None
        self.running = False

        # RTT 测量
        self.rtt_samples = deque(maxlen=100)
        self.rtt_timestamps = {}  # seq -> send_time

        # 解码渲染延迟测量
        self.decode_render_samples = deque(maxlen=100)

        # 帧接收时间记录
        self.frame_recv_time = {}  # seq -> recv_time
        self.frame_display_time = {}  # seq -> display_time

        # 统计
        self.total_frames = 0
        self.start_time = 0

    def connect(self):
        """创建 UDP socket 并完成握手"""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 2 * 1024 * 1024)
        self.sock.settimeout(2.0)

        # 发送 HELLO
        self.sock.sendto(b"HELLO", (self.server_ip, self.port))
        print(f"[INFO] 连接 {self.server_ip}:{self.port}...")

        handshake_start = time.time()
        while True:
            try:
                data, addr = self.sock.recvfrom(UDP_MTU)
                if len(data) < H264_UDP_HEADER:
                    continue
                pkt_type = data[3]
                if pkt_type == H264_PKT_TYPE_READY:
                    ack_pkt = bytearray(H264_UDP_HEADER)
                    ack_pkt[3] = H264_PKT_TYPE_ACK
                    self.sock.sendto(bytes(ack_pkt), (self.server_ip, self.port))
                    print(f"[INFO] 握手成功 ({time.time()-handshake_start:.1f}s)")
                    self.sock.settimeout(0.5)
                    return True
            except socket.timeout:
                elapsed = time.time() - handshake_start
                if elapsed > 10:
                    print("[ERROR] 握手超时")
                    return False
                self.sock.sendto(b"HELLO", (self.server_ip, self.port))

    def measure_rtt(self, duration=30):
        """测量网络 RTT

        发送时间戳包到 ESP32，ESP32 回显后计算 RTT。
        ESP32 端需要实现回显功能（将收到的包原样返回）。
        """
        print(f"\n{'='*50}")
        print(f"  网络 RTT 测量")
        print(f"  持续时间: {duration}秒")
        print(f"{'='*50}")

        self.running = True
        seq = 0
        start_time = time.time()
        last_print = start_time

        while self.running and (time.time() - start_time) < duration:
            try:
                # 发送时间戳包
                now = time.time()
                pkt = struct.pack('!BBBBd', 0, 0, H264_FLAG_SINGLE,
                                  PKT_TYPE_TIMESTAMP, now)
                self.sock.sendto(pkt, (self.server_ip, self.port))
                self.rtt_timestamps[seq] = now
                seq = (seq + 1) & 0xFFFF

                # 接收回显
                try:
                    data, addr = self.sock.recvfrom(64)
                    if len(data) >= 12:  # 8字节头 + 8字节 double
                        recv_time = time.time()
                        # 解析时间戳
                        ts = struct.unpack('!d', data[8:16])[0]
                        rtt = (recv_time - ts) * 1000  # ms
                        if 0 < rtt < 1000:  # 过滤异常值
                            self.rtt_samples.append(rtt)
                except socket.timeout:
                    pass

                # 每秒打印统计
                now = time.time()
                if now - last_print >= 1.0:
                    if self.rtt_samples:
                        avg_rtt = sum(self.rtt_samples) / len(self.rtt_samples)
                        min_rtt = min(self.rtt_samples)
                        max_rtt = max(self.rtt_samples)
                        one_way = avg_rtt / 2
                        print(f"\r[RTT] 平均: {avg_rtt:.1f}ms | "
                              f"最小: {min_rtt:.1f}ms | "
                              f"最大: {max_rtt:.1f}ms | "
                              f"单向: {one_way:.1f}ms | "
                              f"样本: {len(self.rtt_samples)}", end='', flush=True)
                    last_print = now

                time.sleep(0.01)  # 10ms 间隔，约 100 包/秒

            except Exception as e:
                print(f"\n[ERROR] {e}")

        self.running = False
        self._print_rtt_results()

    def _print_rtt_results(self):
        """打印 RTT 测量结果"""
        if not self.rtt_samples:
            print("\n[WARN] 未收集到 RTT 样本")
            return

        avg_rtt = sum(self.rtt_samples) / len(self.rtt_samples)
        min_rtt = min(self.rtt_samples)
        max_rtt = max(self.rtt_samples)
        # 计算抖动（RTT 标准差）
        variance = sum((x - avg_rtt) ** 2 for x in self.rtt_samples) / len(self.rtt_samples)
        jitter = variance ** 0.5

        print(f"\n\n{'='*50}")
        print(f"  RTT 测量结果")
        print(f"{'='*50}")
        print(f"  样本数:     {len(self.rtt_samples)}")
        print(f"  平均 RTT:   {avg_rtt:.1f} ms")
        print(f"  最小 RTT:   {min_rtt:.1f} ms")
        print(f"  最大 RTT:   {max_rtt:.1f} ms")
        print(f"  抖动 (σ):   {jitter:.1f} ms")
        print(f"  单向延迟:   {avg_rtt/2:.1f} ms")
        print(f"{'='*50}")

    def measure_decode_render(self, duration=30):
        """测量解码+渲染延迟

        通过 H.264 UDP 视频流测量从收到完整帧到显示的时间。
        需要与 h264_video_client.py 配合使用。
        """
        print(f"\n{'='*50}")
        print(f"  解码+渲染延迟测量")
        print(f"  持续时间: {duration}秒")
        print(f"  说明: 需要同时运行 h264_video_client.py")
        print(f"{'='*50}")

        # 这个测量需要与显示循环集成
        # 在 h264_video_client.py 中已经实现了此功能
        print("\n[INFO] 解码+渲染延迟已在 h264_video_client.py 中实现")
        print("[INFO] 运行客户端时按 'l' 键可查看实时延迟信息")
        print("[INFO] 客户端每秒会打印延迟统计:\n")
        print("  [DISPLAY] 显示FPS: 30.0 | 延迟: 45ms (min=30, max=80) | 目标: 120ms")

    def measure_all(self, duration=30):
        """运行所有测量"""
        print(f"\n{'='*50}")
        print(f"  ESP32-P4 端到端延迟测量套件")
        print(f"  目标: {self.server_ip}:{self.port}")
        print(f"  持续时间: {duration}秒")
        print(f"{'='*50}")

        # 1. RTT 测量
        self.measure_rtt(duration // 2)

        # 2. 解码渲染延迟
        self.measure_decode_render(duration // 2)

        # 3. 汇总
        self._print_summary()

    def _print_summary(self):
        """打印汇总结果"""
        print(f"\n\n{'='*50}")
        print(f"  端到端延迟预算分析")
        print(f"{'='*50}")

        # 估算各阶段延迟
        if self.rtt_samples:
            avg_rtt = sum(self.rtt_samples) / len(self.rtt_samples)
            network_one_way = avg_rtt / 2
        else:
            network_one_way = 15  # 默认估算

        # 延迟预算
        latency_budget = {
            "摄像头采集": 5,
            "H.264 编码": 8,
            "UDP 网络传输": network_one_way,
            "UDP 接收+帧组装": 5,
            "H.264 解码": 10,
            "OpenCV 显示": 5,
            "缓冲/抖动余量": max(0, 120 - (5 + 8 + network_one_way + 5 + 10 + 5)),
        }

        total = sum(latency_budget.values())
        target = 120

        print(f"\n  {'阶段':<20} {'预算(ms)':<10} {'占比':<10}")
        print(f"  {'-'*40}")
        for stage, budget in latency_budget.items():
            pct = (budget / total) * 100
            print(f"  {stage:<20} {budget:<10.1f} {pct:<10.1f}%")
        print(f"  {'-'*40}")
        print(f"  {'总计':<20} {total:<10.1f} {'100%':<10}")

        if total <= target:
            print(f"\n  ✓ 预算在 {target}ms 目标内 (余量: {target - total:.0f}ms)")
        else:
            print(f"\n  ⚠ 预算超出 {target}ms 目标 ({total - target:.0f}ms)")
            print(f"  建议优化:")
            if latency_budget["缓冲/抖动余量"] < 20:
                print(f"    - 减少网络抖动（使用有线网络或优化 Wi-Fi）")
            print(f"    - 降低分辨率以减少编码和解码时间")
            print(f"    - 使用更快的解码器（如硬件解码）")

        print(f"{'='*50}\n")


def visual_latency_guide():
    """视觉延迟测量指南"""
    print(f"""
{'='*50}
  视觉延迟测量方法（最准确）
{'='*50}

原理：
  使用高帧率摄像头（如手机慢动作模式）拍摄屏幕，
  比较屏幕上显示的计时器和实际时间。

步骤：
  1. 在 ESP32 端添加帧时间戳（在 H.264 SEI 中嵌入时间戳）
  2. 客户端显示时在画面角落显示该时间戳
  3. 用另一个摄像头（或手机）拍摄显示画面
  4. 同时拍摄一个高精度计时器（如毫秒级秒表）
  5. 比较两个画面中的时间差

替代方案（无需额外设备）：
  1. 在 ESP32 端 GPIO 输出一个脉冲信号（每帧开始时翻转）
  2. 用示波器同时测量 ESP32 GPIO 和客户端显示延迟
  3. 或者使用网络时间协议 (NTP) 同步两端时间

Python 实现示例：

  # ESP32 端：在 H.264 SEI 中嵌入时间戳
  # 使用 esp_timer_get_time() 获取微秒级时间戳
  # 编码到 H.264 SEI NAL 单元中

  # 客户端：解析 SEI 时间戳
  # 收到帧时记录本地时间
  # 延迟 = 本地时间 - SEI 时间戳 - 时间同步误差

{'='*50}
""")


def main():
    parser = argparse.ArgumentParser(
        description="ESP32-P4 H.264 视频流端到端延迟测量工具")
    parser.add_argument("server_ip", help="ESP32 IP 地址")
    parser.add_argument("--method", choices=['all', 'rtt', 'decode', 'visual'],
                       default='all', help="测量方法 (默认: all)")
    parser.add_argument("--duration", type=int, default=30,
                       help="测量持续时间 (秒, 默认: 30)")
    parser.add_argument("--output", help="输出结果到文件")

    args = parser.parse_args()

    measurer = LatencyMeasurer(args.server_ip)

    if not measurer.connect():
        print("[ERROR] 无法连接到 ESP32")
        sys.exit(1)

    if args.method == 'visual':
        visual_latency_guide()
        return

    if args.method == 'all':
        measurer.measure_all(args.duration)
    elif args.method == 'rtt':
        measurer.measure_rtt(args.duration)
    elif args.method == 'decode':
        measurer.measure_decode_render(args.duration)

    if args.output:
        results = {
            "timestamp": datetime.now().isoformat(),
            "server_ip": args.server_ip,
            "method": args.method,
            "duration": args.duration,
            "rtt_samples": list(measurer.rtt_samples) if measurer.rtt_samples else [],
            "rtt_avg": sum(measurer.rtt_samples) / len(measurer.rtt_samples) if measurer.rtt_samples else 0,
            "rtt_min": min(measurer.rtt_samples) if measurer.rtt_samples else 0,
            "rtt_max": max(measurer.rtt_samples) if measurer.rtt_samples else 0,
        }
        with open(args.output, 'w') as f:
            json.dump(results, f, indent=2)
        print(f"\n[INFO] 结果已保存到: {args.output}")


if __name__ == "__main__":
    main()
