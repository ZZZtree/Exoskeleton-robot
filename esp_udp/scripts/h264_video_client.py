#!/usr/bin/env python3
"""
ESP32-P4 H.264 视频流 + JPEG 拍照 Python 客户端

功能：
1. H.264 实时视频预览（UDP 端口 1235）- 使用 av.open 字节流模式自动解析 SPS/PPS
2. JPEG 拍照（TCP 端口 8080）
3. 按键 's' 保存当前帧为图片
4. 按键 'p' 通过 TCP 请求 JPEG 拍照
5. 按键 'q' 退出

用法：
    python h264_video_client.py <ESP32_IP地址>
    
示例：
    python h264_video_client.py 192.168.1.100

解码方式（自动选择）：
    1. PyAV (av) - 最可靠，推荐安装: pip install av
    2. FFmpeg 子进程管道 - 备选，需要系统安装 ffmpeg

    注意：如果两者都不可用，程序会明确报错退出，不再退回到 OpenCV。
"""

import socket
import struct
import sys
import threading
import time
import cv2
import numpy as np
import os
import subprocess
import tempfile
from datetime import datetime

# ============ 协议配置 ============

# H.264 UDP 视频流
H264_UDP_PORT = 1235
H264_UDP_HEADER = 8  # 帧序号(2) + 标志(1) + 包类型(1) + 大小(4)

# H.264 分包标志
H264_FLAG_SINGLE = 0x03
H264_FLAG_FIRST = 0x00
H264_FLAG_MIDDLE = 0x01
H264_FLAG_LAST = 0x02

# H.264 包类型（对应 data[3] 字段）
H264_PKT_TYPE_VIDEO = 0   # 普通视频帧
H264_PKT_TYPE_SPSPPS = 1  # 独立 SPS/PPS 参数集包（ESP32 定期发送，解决 UDP 丢包问题）
H264_PKT_TYPE_READY = 0xFE  # ESP32 就绪信号（握手用）
H264_PKT_TYPE_ACK   = 0xFD  # PC 确认信号（握手用）

# TCP JPEG 拍照
TCP_JPEG_PORT = 8080

# UDP 接收缓冲区大小
# 25fps × ~184KB/帧 = ~4.6MB/s 吞吐量，需要足够大的缓冲区平滑突发流量
UDP_MTU = 1460
RECV_BUF_SIZE = UDP_MTU * 2048  # 约 2.85MB

# H.264 NAL 单元起始码
H264_START_CODE = b'\x00\x00\x00\x01'


def _detect_decoder():
    """检测可用的 H.264 解码器"""
    # 1. 检查 PyAV (av.CodecContext) - 最可靠的方式
    try:
        import av
        codec = av.CodecContext.create('h264', 'r')
        if codec:
            print("[INFO] 使用 PyAV (av.CodecContext) 解码器")
            return 'pyav'
    except Exception as e:
        print(f"[WARN] PyAV CodecContext 不可用: {e}")

    # 2. 检查 FFmpeg
    try:
        proc = subprocess.run(['ffmpeg', '-version'],
                              capture_output=True, timeout=2)
        if proc.returncode == 0:
            print("[INFO] 使用 FFmpeg 子进程管道解码器")
            return 'ffmpeg'
    except Exception as e:
        print(f"[WARN] FFmpeg 不可用: {e}")

    print("[ERROR] 未找到可用的 H.264 解码器，请安装 PyAV 或 FFmpeg")
    return 'unsupported'


class H264Decoder:
    """H.264 解码器抽象层

    解码策略：
    1. PyAV (av.CodecContext) - 最可靠，流式解码，维护解码器内部参考帧状态
    2. FFmpeg 子进程管道 - 次选，需要系统安装 ffmpeg

    流式解码模式（PyAV）：
    - 解码器内部维护 DPB（解码图像缓冲区），自动管理 IDR/P 帧参考关系
    - 每帧数据作为独立 packet 送入，解码器根据 NAL 类型自动处理
    - IDR 帧重置参考帧列表，P 帧参考之前的 IDR/P 帧
    - 无需外部累积缓冲区或 IDR 帧检测

    FFmpeg 子进程模式：
    - 每帧独立调用 ffmpeg 解码（慢但可靠）
    - 适合调试和验证码流正确性
    """

    def __init__(self, width=800, height=1280):
        self.width = width
        self.height = height
        self.decoder_type = _detect_decoder()
        self._init_decoder()

    def _init_decoder(self):
        """初始化解码器"""
        if self.decoder_type == 'pyav':
            self._init_pyav()
        elif self.decoder_type == 'ffmpeg':
            self._init_ffmpeg()
        else:
            raise RuntimeError("没有可用的 H.264 解码器，请安装 PyAV 或 FFmpeg")

    def _init_pyav(self):
        """初始化 PyAV CodecContext 解码器（流式模式）

        流式模式关键配置：
        - thread_type = 0: 自动选择线程模式
        - skip_frame = 'NONE': 解码所有帧（包括非参考帧）
        - flags2 = 'SHOW_ALL': 显示所有帧，包括延迟帧
        - 解码器内部维护 DPB，自动管理参考帧
        """
        import av
        self._av_codec = av.CodecContext.create('h264', 'r')
        self._av_codec.thread_type = 0  # 自动选择线程模式
        self._av_codec.skip_frame = 'NONE'
        # 用于累积不完整帧数据的缓冲区（仅用于跨包 NAL 单元拼接）
        self._pyav_buffer = b''
        # packet pts/dts 计数器，确保单调递增的时间戳
        self._packet_pts = 0

    def _init_ffmpeg(self):
        """初始化 FFmpeg 子进程管道"""
        self._ffmpeg_cmd = [
            'ffmpeg',
            '-hide_banner', '-loglevel', 'quiet',
            '-f', 'h264',
            '-i', 'pipe:0',
            '-f', 'rawvideo',
            '-pix_fmt', 'bgr24',
            '-s', f'{self.width}x{self.height}',
            '-an',
            'pipe:1'
        ]
        self._ffmpeg_proc = None
        self._ffmpeg_buffer = b''

    def decode(self, h264_data: bytes):
        """解码 H.264 数据，返回 BGR 图像帧

        流式解码：每帧数据独立送入，解码器维护内部参考帧状态。
        IDR 帧自动重置 DPB，P 帧自动参考之前的帧。
        """
        if not h264_data or len(h264_data) < 10:
            return None

        if self.decoder_type == 'pyav':
            return self._decode_pyav(h264_data)
        elif self.decoder_type == 'ffmpeg':
            return self._decode_ffmpeg(h264_data)
        return None

    def _decode_pyav(self, h264_data: bytes):
        """使用 PyAV CodecContext 流式解码

        流式解码特点：
        - 每帧数据作为独立 packet 送入
        - 解码器内部维护 DPB，P 帧自动参考之前的 IDR/P 帧
        - 即使解码失败（如丢包导致参考帧缺失），后续帧仍可恢复
        - 不需要外部累积缓冲区或 IDR 帧检测

        注意：PyAV CodecContext.decode() 返回一个生成器，
        可能产生 0 个、1 个或多个帧。对于 H.264：
        - IDR 帧：通常返回 1 帧
        - P 帧：如果参考帧可用，返回 1 帧；否则返回 0 帧
        - B 帧：可能延迟输出（需要后续帧触发）

        修复说明：
        av.Packet() 必须设置 pts/dts 时间戳，否则解码器内部 DPB
        （解码图像缓冲区）的参考帧管理可能出错，导致 P 帧参考错误的
        帧，产生花屏。这里使用单调递增的 pts 计数器。
        """
        import av
        try:
            packet = av.Packet(h264_data)
            # 设置 pts/dts 时间戳 - 关键修复！
            # 解码器使用 pts 来管理 DPB 中的参考帧列表
            # 没有正确 pts 会导致 P 帧参考错误的帧，产生花屏
            # H.264 标准时间基准是 90kHz（90000），
            # 每帧增量 = 90000 / fps，这里使用 90000/20 = 4500
            packet.pts = self._packet_pts
            packet.dts = self._packet_pts
            packet.time_base = 90000  # H.264 标准 90kHz 时间基准
            self._packet_pts += 4500  # 每帧增量 = 90000 / 20fps

            frames = self._av_codec.decode(packet)
            for frame in frames:
                if frame is not None:
                    img = frame.to_ndarray(format='bgr24')
                    if img is not None and img.size > 0:
                        return img
        except Exception:
            pass
        return None

    def _decode_ffmpeg(self, h264_data: bytes):
        """使用 FFmpeg 子进程管道解码"""
        try:
            with tempfile.NamedTemporaryFile(suffix='.h264', delete=False) as f:
                f.write(h264_data)
                temp_path = f.name

            cmd = [
                'ffmpeg',
                '-hide_banner', '-loglevel', 'quiet',
                '-f', 'h264',
                '-i', temp_path,
                '-f', 'rawvideo',
                '-pix_fmt', 'bgr24',
                '-s', f'{self.width}x{self.height}',
                '-frames:v', '1',
                '-an',
                'pipe:1'
            ]
            proc = subprocess.run(cmd, capture_output=True, timeout=5)
            os.unlink(temp_path)

            if proc.returncode == 0 and len(proc.stdout) > 0:
                frame_size = self.width * self.height * 3
                if len(proc.stdout) >= frame_size:
                    img = np.frombuffer(proc.stdout[:frame_size],
                                        dtype=np.uint8).reshape(
                        (self.height, self.width, 3))
                    return img
        except Exception:
            pass
        return None

    def flush(self):
        """刷新解码器"""
        if self.decoder_type == 'pyav':
            try:
                self._av_codec.flush()
            except Exception:
                pass

    def reinit(self):
        """重新初始化解码器（彻底重建，替代 flush）

        当连续解码失败时，flush() 无法清除 DPB（解码图像缓冲区）中的
        损坏参考帧。重建解码器实例可以彻底重置所有内部状态。

        注意：重建后需要等待下一个 IDR 帧才能正常解码。
        """
        if self.decoder_type == 'pyav':
            try:
                self._av_codec.flush()
            except Exception:
                pass
            self._init_pyav()
        elif self.decoder_type == 'ffmpeg':
            self._init_ffmpeg()
    def close(self):
        """关闭解码器"""
        if self.decoder_type == 'pyav':
            try:
                self._av_codec.flush()
            except Exception:
                pass
        elif self.decoder_type == 'ffmpeg' and self._ffmpeg_proc:
            try:
                self._ffmpeg_proc.terminate()
            except Exception:
                pass


class H264UDPClient:
    """H.264 UDP 视频流客户端

    使用 PyAV CodecContext 解码 H.264 流，自动处理 SPS/PPS。

    握手流程：
    1. PC 发送 HELLO 包通知 ESP32
    2. ESP32 回复 READY 信号（包类型=0xFE）
    3. PC 收到 READY 后回复 ACK（包类型=0xFD）
    4. 握手完成，ESP32 开始发送视频数据

    解码策略：
    - 累积 H.264 数据到缓冲区
    - 检测 IDR 帧（NAL 类型=5），从 IDR 帧开始解码
    - 每次收到完整帧时尝试解码
    - 解码失败时保留缓冲区，等待下一个 IDR 帧

    功能：
    - 实时检测 ESP32 是否在发送 H.264 数据
    - 每次收到数据包时实时输出
    - 检测丢包和网络延迟
    - 显示详细的连接状态和统计信息
    """

    def __init__(self, server_ip: str, port: int = H264_UDP_PORT,
                 width=800, height=1280):
        self.server_ip = server_ip
        self.port = port
        self.width = width
        self.height = height
        self.sock = None
        self.running = False
        self.frame_buffers = {}  # seq -> {data: bytes, chunks: [(flags, data)], received: set}
        self.frame_lock = threading.Lock()
        self.latest_frame = None
        self.latest_frame_lock = threading.Lock()
        self.frame_count = 0
        self.last_log_time = time.time()
        self.stats_lock = threading.Lock()

        # 调试：保存第一帧到文件
        self._debug_frame_saved = False

        # 握手状态
        self.handshake_done = False
        self.handshake_logged = False

        # H.264 解码器
        self.decoder = H264Decoder(width, height)

        # ============ H.264 帧累积和解码策略 ============
        # 累积缓冲区 - 存储收到的 H.264 数据
        self.h264_buffer = b''
        self.h264_buffer_lock = threading.Lock()
        self.last_decode_time = time.time()
        self.decode_interval = 0.05  # 每 50ms 尝试解码一次
        # 帧组装超时 - 超时未收到 LAST 的半帧直接丢弃，不能送解码器
        # 30fps 时每帧 33ms，网络抖动需要余量，设为 0.5s
        # 从 0.3s 增加到 0.5s 以减少因网络瞬时抖动导致的帧超时丢弃
        # 同时配合独立超时检查线程（每 100ms），及时清理真正丢失的帧
        self.frame_assembly_timeout = 0.5

        # 独立超时检查线程控制
        # 问题：_check_frame_assembly_timeout() 只在收到多包时调用，
        # 如果网络短暂中断，超时帧无法及时清理。
        # 修复：独立线程每 100ms 检查一次，及时清理超时帧。
        self._timeout_checker_running = False
        self._timeout_checker_thread = None


        # IDR 帧检测
        # 800x1280 H.264 帧约 184KB，最小缓冲区设为 32KB
        self.min_buffer_size = 32 * 1024  # 最小 32KB 才开始解码

        # 解码失败计数 - 连续失败时清空缓冲区重新开始
        self.consecutive_decode_failures = 0
        self.max_consecutive_failures = 10  # 连续 10 次失败后清空缓冲区

        # 解码器重建后等待 IDR 帧标志
        # 当解码器重建后，DPB 为空，P 帧解码会失败。
        # 设置此标志后，丢弃所有 P 帧直到收到 IDR 帧。
        self._waiting_for_idr = False

        # ============ SPS/PPS 缓存 ============
        # 从第一个完整的 IDR 帧中提取 SPS/PPS 并缓存
        # 当 UDP 丢包导致后续帧缺少 SPS/PPS 时，注入缓存的 SPS/PPS
        self.cached_sps_pps = b''       # 缓存的 SPS+PPS NAL 单元
        self.sps_pps_lock = threading.Lock()
        self.sps_pps_extracted = False  # 是否已成功提取 SPS/PPS

        # ============ ESP32 发送状态检测 ============
        self.last_packet_time = 0
        self.esp32_sending = False
        self.esp32_status = "等待连接..."
        self.consecutive_timeouts = 0

        # 丢包检测
        self.last_seq = -1
        self.lost_packets = 0
        self.total_packets = 0
        self.out_of_order_packets = 0

        # 包类型统计
        self.packet_type_counts = {
            H264_FLAG_SINGLE: 0,
            H264_FLAG_FIRST: 0,
            H264_FLAG_MIDDLE: 0,
            H264_FLAG_LAST: 0,
        }

        # 统计
        self.total_bytes_received = 0
        self.total_frames_decoded = 0
        self.decode_errors = 0
        self.assembled_frames = 0
        self.partial_frame_timeouts = 0
        self.discarded_p_frames = 0  # 强制完成但丢弃的 P 帧数

        # 实时输出控制
        self.last_packet_print_time = 0
        self.packet_print_interval = 0.05  # 每 50ms 打印一次，显示所有包信息
        self.last_frame_log_time = 0
        self.frame_log_interval = 0.25
        self.debug_frame_dir = "debug_frames"
        self.debug_frame_limit = 5
        self.debug_frame_saved = 0
        os.makedirs(self.debug_frame_dir, exist_ok=True)

    def connect(self):
        """创建 UDP socket 并发送 HELLO 包，等待 ESP32 握手"""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, RECV_BUF_SIZE)
        self.sock.settimeout(2.0)  # 2秒超时

        # 发送 HELLO 包
        self.sock.sendto(b"HELLO", (self.server_ip, self.port))
        print(f"\n{'='*50}")
        print(f"  ESP32-P4 H.264 视频客户端")
        print(f"  目标: {self.server_ip}:{self.port}")
        print(f"{'='*50}")
        print(f"[→] 发送 HELLO 到 ESP32...")

        # 等待 ESP32 回复 READY 信号
        self.running = True
        handshake_start = time.time()
        ack_sent = False

        while self.running and not self.handshake_done:
            try:
                data, addr = self.sock.recvfrom(UDP_MTU)
                if len(data) < H264_UDP_HEADER:
                    continue

                pkt_type = data[3]
                if pkt_type == H264_PKT_TYPE_READY:
                    # ESP32 已就绪，发送 ACK
                    if not ack_sent:
                        ack_pkt = bytearray(H264_UDP_HEADER)
                        ack_pkt[0] = 0
                        ack_pkt[1] = 0
                        ack_pkt[2] = H264_FLAG_SINGLE
                        ack_pkt[3] = H264_PKT_TYPE_ACK
                        ack_pkt[4] = 0
                        ack_pkt[5] = 0
                        ack_pkt[6] = 0
                        ack_pkt[7] = 0
                        self.sock.sendto(bytes(ack_pkt), (self.server_ip, self.port))
                        ack_sent = True
                        elapsed = time.time() - handshake_start
                        print(f"[←] 收到 ESP32 READY 信号 ({elapsed*1000:.0f}ms)")
                        print(f"[→] 发送 ACK 确认...")
                        self.handshake_done = True
                        self.handshake_logged = False
                elif pkt_type == H264_PKT_TYPE_VIDEO:
                    # 如果还没完成握手但收到了视频数据，也视为握手成功
                    if not self.handshake_done:
                        self.handshake_done = True
                        self.handshake_logged = False

            except socket.timeout:
                elapsed = time.time() - handshake_start
                if elapsed > 10:
                    print(f"[✗] 握手超时 ({elapsed:.0f}s)，请检查 ESP32 是否运行")
                    self.running = False
                    return False
                # 重发 HELLO
                if int(elapsed) % 2 == 0 and int(elapsed) > 0:
                    self.sock.sendto(b"HELLO", (self.server_ip, self.port))
                    print(f"[→] 重发 HELLO... ({elapsed:.0f}s)")
            except Exception as e:
                print(f"[ERROR] 握手错误: {e}")
                self.running = False
                return False

        if self.handshake_done:
            print(f"[✓] 握手成功！ESP32 已就绪，开始接收视频流...")
            print(f"{'='*50}\n")
            self.sock.settimeout(1.0)  # 恢复为1秒超时用于后续接收
            return True
        return False

    def parse_udp_header(self, data: bytes):
        """解析 H.264 UDP 头部"""
        if len(data) < H264_UDP_HEADER:
            return None

        seq = (data[0] << 8) | data[1]
        flags = data[2]
        pkt_type = data[3]  # 包类型：0=视频帧, 0xFE=READY, 0xFD=ACK
        # 所有包都使用完整的 8 字节头，后续包 size 为 0
        size = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7]

        # 所有包都从 H264_UDP_HEADER 开始取 payload
        payload = data[H264_UDP_HEADER:]

        return {
            'seq': seq,
            'flags': flags,
            'pkt_type': pkt_type,
            'size': size,
            'payload': payload
        }

    def _check_esp32_status(self):
        """检测 ESP32 发送状态

        根据最近是否收到数据包来判断 ESP32 是否在正常发送。
        """
        now = time.time()
        elapsed = now - self.last_packet_time

        if self.last_packet_time == 0:
            # 从未收到过数据包
            self.esp32_sending = False
            self.esp32_status = "等待 ESP32 发送数据..."
        elif elapsed < 2.0:
            # 2秒内收到过数据包 - ESP32 正在发送
            if not self.esp32_sending:
                self.esp32_sending = True
                self.esp32_status = f"ESP32 正在发送 ✓ (延迟 {elapsed*1000:.0f}ms)"
            else:
                self.esp32_status = f"ESP32 正在发送 ✓ (延迟 {elapsed*1000:.0f}ms)"
        elif elapsed < 5.0:
            # 2-5秒没收到数据 - 可能有问题
            self.esp32_sending = False
            self.esp32_status = f"⚠ ESP32 可能停止发送 ({elapsed:.0f}s 无数据)"
        else:
            # 超过5秒没收到数据 - ESP32 很可能停止发送
            self.esp32_sending = False
            self.esp32_status = f"✗ ESP32 停止发送 ({elapsed:.0f}s 无数据)"

    def _detect_packet_loss(self, seq: int):
        """检测丢包

        通过检查序列号是否连续来检测丢包。
        """
        self.total_packets += 1

        if self.last_seq >= 0:
            expected = (self.last_seq + 1) & 0xFFFF
            if seq != expected:
                if seq > expected:
                    # 有丢包
                    lost = seq - expected
                    self.lost_packets += lost
                else:
                    # 乱序
                    self.out_of_order_packets += 1

        self.last_seq = seq

    def _count_packet_type(self, flags: int):
        if flags in self.packet_type_counts:
            self.packet_type_counts[flags] += 1

    def _flag_name(self, flags: int) -> str:
        return {
            H264_FLAG_SINGLE: "SINGLE",
            H264_FLAG_FIRST: "FIRST",
            H264_FLAG_MIDDLE: "MID",
            H264_FLAG_LAST: "LAST",
        }.get(flags, f"0x{flags:02X}")

    def _extract_sps_pps(self, data: bytes) -> bool:
        """从 H.264 数据中提取 SPS (type 7) 和 PPS (type 8) 并缓存

        当收到完整的 IDR 帧（含 SPS/PPS）时调用此方法，
        提取 SPS/PPS 并缓存，用于后续数据缺少参数集时注入。
        """
        i = 0
        sps_data = b''
        pps_data = b''
        while i < len(data) - 4:
            if data[i] == 0 and data[i+1] == 0:
                start_code_len = 0
                nal_start = i
                if data[i+2] == 1:
                    start_code_len = 3
                elif data[i+2] == 0 and data[i+3] == 1:
                    start_code_len = 4
                else:
                    i += 1
                    continue

                nal_type = data[i + start_code_len] & 0x1F
                if nal_type == 7:  # SPS
                    # 找到下一个 NAL 单元来确定 SPS 的结束位置
                    j = i + start_code_len + 1
                    while j < len(data) - 4:
                        if data[j] == 0 and data[j+1] == 0:
                            if data[j+2] == 1 or (data[j+2] == 0 and data[j+3] == 1):
                                sps_data = data[i:j]
                                i = j
                                break
                        j += 1
                    else:
                        sps_data = data[i:]
                        break
                    continue
                elif nal_type == 8 and sps_data:  # PPS（在 SPS 之后）
                    j = i + start_code_len + 1
                    while j < len(data) - 4:
                        if data[j] == 0 and data[j+1] == 0:
                            if data[j+2] == 1 or (data[j+2] == 0 and data[j+3] == 1):
                                pps_data = data[i:j]
                                i = j
                                break
                        j += 1
                    else:
                        pps_data = data[i:]
                        break

                    if sps_data and pps_data:
                        with self.sps_pps_lock:
                            self.cached_sps_pps = sps_data + pps_data
                            self.sps_pps_extracted = True
                        return True
                else:
                    i += start_code_len
            else:
                i += 1
        return False

    def _ensure_sps_pps(self, data: bytes) -> bytes:
        """确保 H.264 数据包含 SPS/PPS，如果没有则注入缓存的 SPS/PPS

        当 UDP 丢包导致 IDR 帧的 FIRST 包丢失时，
        收到的数据可能不包含 SPS/PPS。此方法检测并修复这种情况。

        注入策略：
        - 只对 IDR 帧（NAL type 5）注入 SPS/PPS
        - P 帧（NAL type 1）不需要注入，因为解码器已从之前的 IDR 帧获取了参数集
        - 避免对每一帧都注入 SPS/PPS，防止解码器状态被重复刷新
        """
        # 检查数据是否以 SPS (type 7) 开头
        if len(data) >= 5:
            if data[0] == 0 and data[1] == 0:
                if (data[2] == 1 and (data[3] & 0x1F) == 7) or \
                   (data[2] == 0 and data[3] == 1 and (data[4] & 0x1F) == 7):
                    return data  # 已有 SPS，无需注入

        # 检查是否有 SPS 在数据中
        has_sps = False
        i = 0
        while i < len(data) - 4:
            if data[i] == 0 and data[i+1] == 0:
                if data[i+2] == 1:
                    if (data[i+3] & 0x1F) == 7:
                        has_sps = True
                        break
                    i += 3
                elif data[i+2] == 0 and data[i+3] == 1:
                    if (data[i+4] & 0x1F) == 7:
                        has_sps = True
                        break
                    i += 4
                else:
                    i += 1
            else:
                i += 1

        if has_sps:
            return data  # 数据中已有 SPS

        # 检查是否是 IDR 帧（NAL type 5），只有 IDR 帧才需要注入 SPS/PPS
        is_idr = False
        i = 0
        while i < len(data) - 4:
            if data[i] == 0 and data[i+1] == 0:
                if data[i+2] == 1:
                    if (data[i+3] & 0x1F) == 5:  # IDR 切片
                        is_idr = True
                        break
                    i += 3
                elif data[i+2] == 0 and data[i+3] == 1:
                    if (data[i+4] & 0x1F) == 5:  # IDR 切片
                        is_idr = True
                        break
                    i += 4
                else:
                    i += 1
            else:
                i += 1

        # 只对 IDR 帧注入 SPS/PPS
        if is_idr:
            with self.sps_pps_lock:
                if self.cached_sps_pps:
                    return self.cached_sps_pps + data
        # P 帧不需要注入 SPS/PPS，解码器已从之前的 IDR 帧获取了参数集

        return data  # 没有缓存或不是 IDR 帧，返回原数据

    def _find_idr_in_buffer(self, data: bytes) -> int:
        """在缓冲区中查找第一个完整的 GOP 起始位置（SPS/PPS/IDR）

        策略：找到第一个 SPS (type 7)，然后确认后面有 IDR (type 5)。
        如果找不到 SPS，退而求其次找 IDR (type 5) 本身。

        返回 GOP 的起始偏移，如果未找到则返回 -1。
        """
        # 第一遍：找到所有 NAL 单元的位置和类型
        nal_positions = []  # [(offset, nal_type), ...]
        i = 0
        while i < len(data) - 4:
            if data[i] == 0 and data[i+1] == 0:
                if data[i+2] == 1:
                    nal_type = data[i+3] & 0x1F
                    nal_positions.append((i, nal_type))
                    i += 3
                elif data[i+2] == 0 and data[i+3] == 1:
                    nal_type = data[i+4] & 0x1F
                    nal_positions.append((i, nal_type))
                    i += 4
                else:
                    i += 1
            else:
                i += 1

        if not nal_positions:
            return -1

        # 第二遍：找第一个 SPS (type 7)，确认后面有 IDR (type 5)
        for idx, (offset, nal_type) in enumerate(nal_positions):
            if nal_type == 7:  # SPS
                # 检查后面是否有 IDR (type 5)
                for j in range(idx + 1, len(nal_positions)):
                    _, next_type = nal_positions[j]
                    if next_type == 5:  # IDR 切片
                        return offset  # 从 SPS 开始
                    elif next_type == 7:  # 另一个 SPS，继续
                        continue
                # 没找到 IDR，但至少从 SPS 开始
                return offset

        # 第三遍：没找到 SPS，找第一个 IDR (type 5)
        for offset, nal_type in nal_positions:
            if nal_type == 5:
                return offset

        # 第四遍：实在不行，从第一个 NAL 单元开始
        return nal_positions[0][0]

    def _find_last_nal(self, data: bytes) -> int:
        """在数据中查找最后一个完整的 NAL 单元起始位置

        用于在解码后，定位最后一个完整帧的结束位置，
        以便保留后续不完整的数据。
        """
        last_pos = -1
        i = 0
        while i < len(data) - 4:
            if data[i] == 0 and data[i+1] == 0:
                if data[i+2] == 1:
                    last_pos = i
                    i += 3
                elif data[i+2] == 0 and data[i+3] == 1:
                    last_pos = i
                    i += 4
                else:
                    i += 1
            else:
                i += 1
        return last_pos

    def _append_to_buffer(self, h264_data: bytes):
        """将 H.264 数据追加到累积缓冲区

        限制缓冲区大小防止内存溢出。
        解码成功后 _decode_frame_direct 会清理已解码的旧数据，
        所以缓冲区通常保持在较小范围。这里设置 1MB 上限作为安全网。
        """
        with self.h264_buffer_lock:
            max_buffer = 1024 * 1024  # 1MB（解码成功后会自动清理）
            if len(self.h264_buffer) + len(h264_data) > max_buffer:
                # 保留最新的数据
                keep_size = max_buffer // 2
                self.h264_buffer = self.h264_buffer[-keep_size:] + h264_data
            else:
                self.h264_buffer += h264_data
        self.total_bytes_received += len(h264_data)

    def _save_debug_frame(self, frame_data: bytes, seq: int):
        if self.debug_frame_saved >= self.debug_frame_limit:
            return
        try:
            filename = os.path.join(self.debug_frame_dir, f"frame_{self.debug_frame_saved:02d}_seq_{seq}.h264")
            with open(filename, 'wb') as f:
                f.write(frame_data)
            self.debug_frame_saved += 1
            print(f"\n[DEBUG] 已保存完整帧: {filename} ({len(frame_data)}B)")
        except Exception as e:
            print(f"\n[DEBUG] 保存完整帧失败: {e}")

    def _decode_frame_direct(self, h264_data: bytes) -> np.ndarray:
        """逐帧解码一帧完整的 H.264 数据

        解码策略（纯流式解码）：
        1. 每帧 H.264 数据（完整的访问单元）直接送入 PyAV CodecContext
        2. 解码器内部维护 DPB（解码图像缓冲区），自动管理 IDR/P 帧参考关系
        3. IDR 帧自动重置参考帧列表，P 帧自动参考之前的 IDR/P 帧
        4. 不需要累积缓冲区或 GOP 扫描

        为什么逐帧解码可行：
        - H.264 解码器本身就是设计为逐访问单元输入的
        - 每帧数据（SPS/PPS + IDR 切片 或 P 切片）是一个完整的访问单元
        - PyAV CodecContext 内部维护 DPB，P 帧自动参考之前的帧
        - 时间复杂度 O(1)，不随 GOP 增长

        处理 SPS/PPS 丢失：
        - UDP 可能丢包导致帧缺少 SPS/PPS
        - 使用缓存的 SPS/PPS 注入到帧数据前
        - 注入后仍只解码当前这一帧
        """
        try:
            if len(h264_data) < 100:
                return None

            # 确保 SPS/PPS 已提取（从第一帧 IDR 中提取）
            if not self.sps_pps_extracted:
                self._extract_sps_pps(h264_data)

            # 注入缓存的 SPS/PPS（如果当前帧缺少）
            decode_data = self._ensure_sps_pps(h264_data)

            # 逐帧解码
            frame = self.decoder.decode(decode_data)
            if frame is not None:
                self.total_frames_decoded += 1
                self.consecutive_decode_failures = 0
                return frame
            else:
                self.decode_errors += 1
                self.consecutive_decode_failures += 1
                if self.consecutive_decode_failures >= self.max_consecutive_failures:
                    # 连续失败，刷新解码器
                    self.decoder.flush()
                    self.consecutive_decode_failures = 0
                    print(f"\n[INFO] 连续 {self.max_consecutive_failures} 次解码失败，刷新解码器")
                return None
        except Exception as e:
            self.decode_errors += 1
            self.consecutive_decode_failures += 1
            if self.consecutive_decode_failures >= self.max_consecutive_failures:
                self.decoder.flush()
                self.consecutive_decode_failures = 0
                print(f"\n[INFO] 连续 {self.max_consecutive_failures} 次解码失败，刷新解码器")
            return None

    def _is_idr_frame(self, data: bytes) -> bool:
        """检测 H.264 数据是否为 IDR 帧（包含 IDR 切片 NAL type 5）

        用于判断强制完成的帧是否值得送入解码器：
        - IDR 帧：不依赖参考帧，即使数据不完整也可能解码出部分图像
        - P 帧：依赖之前的参考帧，损坏的 P 帧会污染 DPB
        """
        i = 0
        while i < len(data) - 4:
            if data[i] == 0 and data[i+1] == 0:
                if data[i+2] == 1:
                    nal_type = data[i+3] & 0x1F
                    if nal_type == 5:  # IDR 切片
                        return True
                    i += 3
                elif data[i+2] == 0 and data[i+3] == 1:
                    nal_type = data[i+4] & 0x1F
                    if nal_type == 5:  # IDR 切片
                        return True
                    i += 4
                else:
                    i += 1
            else:
                i += 1
        return False

    def process_frame(self, h264_data: bytes):
        """处理一帧 H.264 数据

        纯流式解码：
        - 每帧数据直接送入解码器
        - 解码器内部维护 DPB，自动管理参考帧
        - 不需要累积缓冲区
        - 时间复杂度 O(1)

        解码器恢复策略（修复 DPB 污染级联失败 v3）：
        - 连续解码失败时，flush() 无法清除 DPB 中的损坏参考帧
        - 当 consecutive_decode_failures >= max_consecutive_failures 时，
          重建解码器实例（reinit），彻底重置所有内部状态
        - **关键修复 v3**：重建后设置 `_waiting_for_idr = True` 标志，
          丢弃所有 P 帧直到收到干净的 IDR 帧，避免 P 帧引用空 DPB
        - 收到 IDR 帧后清除标志，恢复正常解码
        """
        try:
            # === 关键修复：解码器重建后等待 IDR 帧 ===
            # 解码器重建后 DPB 为空，P 帧需要参考之前的帧，
            # 如果此时收到 P 帧，解码会失败并触发再次重建，形成死循环。
            # 只有 IDR 帧（不依赖任何参考帧）才能让解码器恢复正常。
            if self._waiting_for_idr:
                if self._is_idr_frame(h264_data):
                    self._waiting_for_idr = False
                    print(f"\n[INFO] 收到 IDR 帧，解码器恢复")
                else:
                    # 丢弃 P 帧，等待 IDR 帧
                    return

            frame = self._decode_frame_direct(h264_data)
            if frame is not None:
                with self.latest_frame_lock:
                    self.latest_frame = frame
                # 解码成功，重置连续失败计数
                self.consecutive_decode_failures = 0
            else:
                # 解码失败（如 P 帧参考帧缺失）
                self.consecutive_decode_failures += 1
                with self.stats_lock:
                    self.decode_errors += 1

                # 连续失败超过阈值，重建解码器
                if self.consecutive_decode_failures >= self.max_consecutive_failures:
                    print(f"\n[WARN] 连续 {self.consecutive_decode_failures} 帧解码失败，"
                          f"重建解码器实例并等待 IDR 帧...")
                    self.decoder.reinit()
                    self.consecutive_decode_failures = 0
                    self._waiting_for_idr = True  # 设置等待 IDR 标志
                    # 重建后清空累积缓冲区，等待下一个 IDR 帧
                    with self.h264_buffer_lock:
                        self.h264_buffer = b''
        except Exception as e:
            self.consecutive_decode_failures += 1
            with self.stats_lock:
                self.decode_errors += 1
            if self.consecutive_decode_failures >= self.max_consecutive_failures:
                print(f"\n[WARN] 连续 {self.consecutive_decode_failures} 帧解码异常，"
                      f"重建解码器实例并等待 IDR 帧...")
                self.decoder.reinit()
                self.consecutive_decode_failures = 0
                self._waiting_for_idr = True  # 设置等待 IDR 标志
                with self.h264_buffer_lock:
                    self.h264_buffer = b''

    def _assemble_frame(self, seq: int) -> bytes:
        """组装完整的一帧数据

        多包模式下，将所有分片按顺序拼接成完整帧。
        """
        with self.frame_lock:
            if seq not in self.frame_buffers:
                return None
            info = self.frame_buffers[seq]
            chunks = info['chunks']
            if not info.get('complete', False):
                return None

            # 按 chunk 索引排序并拼接
            sorted_chunks = sorted(chunks, key=lambda x: x[0])
            frame_data = b''.join([c[1] for c in sorted_chunks])
            del self.frame_buffers[seq]
            return frame_data

    def _check_frame_assembly_timeout(self):
        """检查帧组装超时，处理未完成帧。

        策略（马赛克修复 v2）：
        - 使用 FIRST 包中的 size 字段（整帧字节数）精确计算预期分片数
        - 如果已收到 >= 95% 的数据，强制完成（容忍少量尾部丢包）
        - **关键修复**：强制完成的帧需要检测是否为 IDR 帧：
          * 如果是 IDR 帧：先 flush 解码器清除损坏的 DPB，再解码（干净重启）
          * 如果是 P 帧：**直接丢弃**，避免损坏的 P 帧污染 DPB
        - 否则直接丢弃，避免将损坏的半帧送入解码器导致 DPB 污染
        - 超时从 3.0s 缩短到 0.5s（20fps 时每帧 50ms，0.5s 足够判断丢帧）
        """
        now = time.time()
        timed_out_seqs = []
        forced_complete_seqs = []

        with self.frame_lock:
            for seq, info in list(self.frame_buffers.items()):
                first_time = info.get('first_time', 0)
                if first_time == 0 or (now - first_time) <= self.frame_assembly_timeout:
                    continue

                chunks = info['chunks']
                if not chunks:
                    del self.frame_buffers[seq]
                    continue

                flags_seen = list(info.get('flags_seen', []))
                saw_first = H264_FLAG_FIRST in flags_seen
                saw_last = H264_FLAG_LAST in flags_seen
                chunk_count = len(chunks)
                received_bytes = info.get('received_bytes', 0)
                expected_size = info.get('expected_size', 0)

                if saw_first and not saw_last and expected_size > 0:
                    # 使用 FIRST 包中的 size 字段精确判断
                    # 每个 UDP 分片 payload 最大 1452 字节（UDP_MTU - 8字节头）
                    # 预期分片数 = ceil(expected_size / 1452)
                    max_payload = UDP_MTU - H264_UDP_HEADER  # 1452
                    expected_chunks = (expected_size + max_payload - 1) // max_payload
                    received_pct = (received_bytes / expected_size) * 100 if expected_size > 0 else 0

                    if received_pct >= 95.0:
                        # 已收到 >= 95% 数据，强制完成
                        info['complete'] = True
                        info['forced_complete'] = True
                        forced_complete_seqs.append((seq, chunk_count, expected_chunks, received_pct))
                    else:
                        # 数据缺失太多，直接丢弃，避免 DPB 污染
                        timed_out_seqs.append((seq, chunk_count, flags_seen, received_pct, expected_chunks))
                        del self.frame_buffers[seq]
                elif saw_first and not saw_last:
                    # 没有 expected_size 信息（旧版 ESP32 固件），回退到分片数判断
                    if chunk_count >= 30:
                        info['complete'] = True
                        info['forced_complete'] = True
                        forced_complete_seqs.append((seq, chunk_count, 0, 0))
                    else:
                        timed_out_seqs.append((seq, chunk_count, flags_seen, 0, 0))
                        del self.frame_buffers[seq]
                else:
                    timed_out_seqs.append((seq, chunk_count, flags_seen, 0, 0))
                    del self.frame_buffers[seq]

        for seq, chunk_count, flags_seen, received_pct, expected_chunks in timed_out_seqs:
            self.partial_frame_timeouts += 1
            saw_first = H264_FLAG_FIRST in flags_seen
            saw_last = H264_FLAG_LAST in flags_seen
            if expected_chunks > 0:
                print(f"\n[WARN] 帧 seq={seq} 超时丢弃，收到 {chunk_count}/{expected_chunks} 分片 "
                      f"({received_pct:.1f}%), FIRST={saw_first}, LAST={saw_last}")
            else:
                print(f"\n[WARN] 帧 seq={seq} 超时丢弃，仅收到 {chunk_count} 个分片 "
                      f"(FIRST={saw_first}, LAST={saw_last})")

        for seq, chunk_count, expected_chunks, received_pct in forced_complete_seqs:
            self.partial_frame_timeouts += 1
            if expected_chunks > 0:
                print(f"\n[INFO] 帧 seq={seq} 超时但强制完成（{chunk_count}/{expected_chunks} 分片, "
                      f"{received_pct:.1f}%），尝试解码")
            else:
                print(f"\n[INFO] 帧 seq={seq} 超时但强制完成（{chunk_count} 个分片），尝试解码")
            frame_data = self._assemble_frame(seq)
            if frame_data is not None and len(frame_data) > 100:
                # === 马赛克修复：强制完成的帧需要检测是否为 IDR 帧 ===
                # 强制完成的帧可能缺少关键数据（丢失的分片），
                # 如果是 P 帧，损坏的数据会污染 DPB，导致后续帧全部花屏。
                # 只有 IDR 帧的强制完成才值得尝试（IDR 不依赖参考帧）。
                is_idr = self._is_idr_frame(frame_data)
                if is_idr:
                    # IDR 帧：先 flush 解码器清除损坏的 DPB，再解码
                    print(f"\n[INFO] 帧 seq={seq} 是 IDR 帧，flush 解码器后解码")
                    self.decoder.flush()
                    self.assembled_frames += 1
                    self.process_frame(frame_data)
                else:
                    # P 帧：直接丢弃，避免损坏的 P 帧污染 DPB
                    self.discarded_p_frames += 1
                    print(f"\n[INFO] 帧 seq={seq} 是 P 帧，强制完成但丢弃（避免 DPB 污染）")
                    # 不送入解码器，等待下一个 IDR 帧

    def _timeout_checker_loop(self):
        """独立超时检查线程

        问题：_check_frame_assembly_timeout() 只在收到多包时调用，
        如果网络短暂中断，超时帧无法及时清理，导致 frame_buffers 中
        累积大量未完成帧。

        修复：独立线程每 100ms 检查一次，及时清理超时帧。
        """
        while self._timeout_checker_running:
            try:
                if self.running and self.handshake_done:
                    self._check_frame_assembly_timeout()
            except Exception:
                pass
            time.sleep(0.1)  # 每 100ms 检查一次

    def _handle_single_packet(self, header: dict):
        """处理单包模式 - 直接解码

        单包模式意味着整帧数据在一个 UDP 包中（≤1452B）。
        直接送入解码器逐帧解码。
        """
        frame_data = header['payload']
        self.process_frame(frame_data)
        with self.stats_lock:
            self.frame_count += 1
            self.total_bytes_received += len(frame_data) + H264_UDP_HEADER

    def _handle_multi_packet(self, header: dict):
        """处理多包模式 - 先帧组装，然后逐帧解码

        策略：
        1. FIRST 包：创建帧组装条目
        2. MID 包：追加到当前帧
        3. LAST 包：完成帧组装，立即送入解码器逐帧解码
        4. 超时检查：丢弃未完成帧
        """
        seq = header['seq']
        flags = header['flags']
        payload = header['payload']

        self._count_packet_type(flags)

        with self.frame_lock:
            if seq not in self.frame_buffers:
                info = {
                    'chunks': [],
                    'complete': False,
                    'first_time': time.time(),
                    'flags_seen': [],
                    'expected_size': 0,       # FIRST 包中的 size 字段（整帧字节数）
                    'received_bytes': 0,       # 已收到的数据字节数
                }
                # FIRST 包携带整帧大小信息
                if flags == H264_FLAG_FIRST:
                    info['expected_size'] = header.get('size', 0)
                self.frame_buffers[seq] = info

            info = self.frame_buffers[seq]
            chunk_idx = len(info['chunks'])
            info['chunks'].append((chunk_idx, payload))
            info['flags_seen'].append(flags)
            info['received_bytes'] += len(payload)

            if flags == H264_FLAG_LAST:
                info['complete'] = True

        if flags == H264_FLAG_LAST:
            frame_data = self._assemble_frame(seq)
            if frame_data is not None:
                self.assembled_frames += 1
                if not self._debug_frame_saved and len(frame_data) > 100:
                    self._debug_frame_saved = True
                    try:
                        preview = frame_data[:min(64, len(frame_data))]
                        print(f"\n[DEBUG] 第一帧 H.264 数据 ({len(frame_data)}B):")
                        print(f"[DEBUG] 前 64 字节: {preview.hex()}")
                        if frame_data[:3] == b'\x00\x00\x01':
                            print(f"[DEBUG] NAL 起始码: 3字节 (0x000001)")
                            nal_type = frame_data[3] & 0x1F
                            print(f"[DEBUG] 第一个 NAL 类型: {nal_type} (SPS=7, PPS=8, IDR=5, 非IDR=1)")
                        elif frame_data[:4] == b'\x00\x00\x00\x01':
                            print(f"[DEBUG] NAL 起始码: 4字节 (0x00000001)")
                            nal_type = frame_data[4] & 0x1F
                            print(f"[DEBUG] 第一个 NAL 类型: {nal_type} (SPS=7, PPS=8, IDR=5, 非IDR=1)")
                        else:
                            print(f"[DEBUG] 无标准 NAL 起始码！前4字节: {frame_data[:4].hex()}")
                        self._save_debug_frame(frame_data, seq)
                    except Exception as e:
                        print(f"\n[DEBUG] 保存调试信息失败: {e}")

                self.process_frame(frame_data)
                with self.stats_lock:
                    self.frame_count += 1
                    # 统计帧数据的总字节数（包括UDP头）
                    # 帧数据 = 所有分片的payload之和
                    self.total_bytes_received += len(frame_data) + H264_UDP_HEADER

    def receive_loop(self):
        """接收循环

        实时检测 ESP32 发送状态，包括：
        - 每次收到数据包时实时输出包信息
        - 检测丢包和序列号连续性
        - 网络延迟
        - 解码状态
        """
        self.running = True
        print("[INFO] H.264 视频流接收中...\n")

        # 启动独立超时检查线程（每 100ms 检查一次）
        # 修复：即使网络中断导致没有新包到达，超时帧也能被及时清理
        self._timeout_checker_running = True
        self._timeout_checker_thread = threading.Thread(target=self._timeout_checker_loop, daemon=True)
        self._timeout_checker_thread.start()

        while self.running:
            try:
                data, addr = self.sock.recvfrom(UDP_MTU)
                if len(data) < H264_UDP_HEADER:
                    continue

                # 更新 ESP32 发送状态
                now = time.time()
                self.last_packet_time = now
                self.consecutive_timeouts = 0

                header = self.parse_udp_header(data)
                if header is None:
                    continue

                # 检测丢包
                self._detect_packet_loss(header['seq'])

                # 处理不同类型的包
                pkt_type = header['pkt_type']
                seq = header['seq']
                flags = header['flags']
                payload_len = len(header['payload'])

                if pkt_type == H264_PKT_TYPE_VIDEO:
                    # 视频帧数据
                    if flags == H264_FLAG_SINGLE:
                        self._count_packet_type(flags)
                        self._handle_single_packet(header)
                    else:
                        self._handle_multi_packet(header)
                        self._check_frame_assembly_timeout()

                    if now - self.last_packet_print_time >= self.packet_print_interval:
                        elapsed_since_last = now - self.last_packet_time
                        flag_name = self._flag_name(flags)

                        realtime_info = (f"\r[RECV] seq={seq:5d} | {flag_name:6s} | "
                                        f"{payload_len:5d}B | "
                                        f"buf={len(self.h264_buffer)//1024}KB | "
                                        f"delay={elapsed_since_last*1000:.0f}ms   ")
                        print(realtime_info, end='', flush=True)
                        self.last_packet_print_time = now

                elif pkt_type == H264_PKT_TYPE_READY:
                    # ESP32 就绪信号
                    if not self.handshake_done:
                        # 握手阶段：回复 ACK
                        ack_pkt = bytearray(H264_UDP_HEADER)
                        ack_pkt[0] = 0
                        ack_pkt[1] = 0
                        ack_pkt[2] = H264_FLAG_SINGLE
                        ack_pkt[3] = H264_PKT_TYPE_ACK
                        ack_pkt[4] = 0
                        ack_pkt[5] = 0
                        ack_pkt[6] = 0
                        ack_pkt[7] = 0
                        self.sock.sendto(bytes(ack_pkt), (self.server_ip, self.port))
                        print(f"\n[←] ESP32 READY 信号 (seq={seq})")
                        print(f"[→] 发送 ACK 确认")
                        self.handshake_done = True
                        print(f"[✓] 握手完成！开始接收视频流...")
                    # 握手完成后忽略 READY 包（避免无限循环）

                elif pkt_type == H264_PKT_TYPE_ACK:
                    # ESP32 的 ACK - 握手阶段已处理，忽略
                    pass

                elif pkt_type == H264_PKT_TYPE_SPSPPS:
                    # 独立 SPS/PPS 参数集包
                    # 这是 ESP32 定期发送的 SPS/PPS 数据，用于解决 UDP 丢包
                    # 导致解码器缺少参数集的问题。
                    #
                    # 关键修复：将 SPS/PPS 直接喂给解码器！
                    # 如果第一个 IDR 帧的 FIRST 包丢失（导致该 IDR 帧不完整而被丢弃），
                    # 解码器将永远没有 SPS/PPS，直到下一个完整的 IDR 帧到达。
                    # 通过将定期发送的 SPS/PPS 送入解码器，即使 IDR 帧丢失，
                    # 解码器也能获得参数集，后续 P 帧可以正常解码。
                    #
                    # 注意：SPS/PPS 本身不包含帧数据，解码器 decode() 会返回 None，
                    # 但解码器内部会更新参数集状态，不影响后续帧解码。
                    sps_pps_data = header['payload']
                    if len(sps_pps_data) > 0:
                        with self.sps_pps_lock:
                            self.cached_sps_pps = sps_pps_data
                            self.sps_pps_extracted = True
                        # 关键修复：将 SPS/PPS 直接喂给解码器
                        self.decoder.decode(sps_pps_data)
                        if now - self.last_packet_print_time >= self.packet_print_interval:
                            print(f"\r[SPS/PPS] 收到独立 SPS/PPS 包: {len(sps_pps_data)}B, 已喂解码器   ",
                                  end='', flush=True)
                            self.last_packet_print_time = now

                # 每秒打印汇总统计
                if now - self.last_log_time >= 1.0:
                    # 检测 ESP32 状态
                    self._check_esp32_status()

                    # 检查帧组装超时 - 将超时未完成的帧强制送入缓冲区
                    self._check_frame_assembly_timeout()

                    with self.stats_lock:
                        fps = self.frame_count / (now - self.last_log_time)
                        decode_fps = self.total_frames_decoded / (now - self.last_log_time)
                        mbps = (self.total_bytes_received * 8) / (now - self.last_log_time) / 1_000_000
                        loss_rate = (self.lost_packets / max(self.total_packets, 1)) * 100

                        # 显示 ESP32 发送状态 + 统计信息
                        total_packets_seen = max(self.total_packets, 1)
                        single_pct = self.packet_type_counts[H264_FLAG_SINGLE] * 100 / total_packets_seen
                        first_pct = self.packet_type_counts[H264_FLAG_FIRST] * 100 / total_packets_seen
                        mid_pct = self.packet_type_counts[H264_FLAG_MIDDLE] * 100 / total_packets_seen
                        last_pct = self.packet_type_counts[H264_FLAG_LAST] * 100 / total_packets_seen

                        # 带宽利用率 = 实际带宽 / 目标带宽 (6Mbps)
                        bw_util = (mbps / 6.0) * 100
                        status_line = (f"\n[STATUS] {self.esp32_status} | "
                                      f"接收FPS: {fps:.1f} | "
                                      f"解码FPS: {decode_fps:.1f} | "
                                      f"组帧: {self.assembled_frames} | "
                                      f"解码: {self.total_frames_decoded} | "
                                      f"错误: {self.decode_errors} | "
                                      f"带宽: {mbps:.1f}/{6.0:.0f}Mbps({bw_util:.0f}%) | "
                                      f"丢包: {self.lost_packets} ({loss_rate:.1f}%) | "
                                      f"PCT[S/F/M/L]={single_pct:.0f}/{first_pct:.0f}/{mid_pct:.0f}/{last_pct:.0f} | "
                                      f"超时帧: {self.partial_frame_timeouts} | "
                                      f"丢弃P: {self.discarded_p_frames}")
                        print(status_line, end='', flush=True)

                        self.frame_count = 0
                        self.total_bytes_received = 0
                        self.total_frames_decoded = 0
                        self.decode_errors = 0
                        self.lost_packets = 0
                        self.total_packets = 0
                        self.packet_type_counts = {
                            H264_FLAG_SINGLE: 0,
                            H264_FLAG_FIRST: 0,
                            H264_FLAG_MIDDLE: 0,
                            H264_FLAG_LAST: 0,
                        }
                    self.last_log_time = now

            except socket.timeout:
                # 超时 - 没有收到数据包
                self.consecutive_timeouts += 1
                self._check_esp32_status()

                # 每 3 秒打印一次状态（即使没有数据）
                now = time.time()
                if now - self.last_log_time >= 3.0:
                    print(f"\r[STATUS] {self.esp32_status} | "
                          f"等待数据中... ({self.consecutive_timeouts}s)", end='', flush=True)
                    self.last_log_time = now
                continue
            except Exception as e:
                if self.running:
                    print(f"\n[ERROR] 接收错误: {e}")
                continue

    def get_latest_frame(self):
        """获取最新帧"""
        with self.latest_frame_lock:
            if self.latest_frame is not None:
                return self.latest_frame.copy()
            return None

    def cleanup(self):
        """清理资源"""
        self.running = False
        # 停止独立超时检查线程
        self._timeout_checker_running = False
        if self._timeout_checker_thread and self._timeout_checker_thread.is_alive():
            self._timeout_checker_thread.join(timeout=1.0)
        if self.sock:
            self.sock.close()
        if self.decoder:
            self.decoder.close()
        print("\n[INFO] H.264 客户端已关闭")


class TCPJPEGClient:
    """TCP JPEG 拍照客户端"""

    def __init__(self, server_ip: str, port: int = TCP_JPEG_PORT):
        self.server_ip = server_ip
        self.port = port

    def capture_photo(self) -> bytes:
        """
        通过 TCP 请求 JPEG 拍照
         
        返回: JPEG 数据字节流，失败返回 None
        """
        sock = None
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(10.0)
            sock.connect((self.server_ip, self.port))

            # 发送触发信号
            sock.sendall(b"CAPTURE")

            # 接收 JPEG 数据大小（4字节，大端序）
            size_data = sock.recv(4)
            if len(size_data) < 4:
                print("[ERROR] TCP JPEG: 接收大小数据失败")
                return None

            jpeg_size = struct.unpack('!I', size_data)[0]
            if jpeg_size <= 0 or jpeg_size > 10 * 1024 * 1024:  # 最大 10MB
                print(f"[ERROR] TCP JPEG: 无效的 JPEG 大小: {jpeg_size}")
                return None

            # 接收 JPEG 数据
            jpeg_data = b''
            remaining = jpeg_size
            while remaining > 0:
                chunk = sock.recv(min(4096, remaining))
                if not chunk:
                    break
                jpeg_data += chunk
                remaining -= len(chunk)

            if len(jpeg_data) != jpeg_size:
                print(f"[ERROR] TCP JPEG: 接收不完整 {len(jpeg_data)}/{jpeg_size}")
                return None

            return jpeg_data

        except socket.timeout:
            print("[ERROR] TCP JPEG: 连接超时")
            return None
        except Exception as e:
            print(f"[ERROR] TCP JPEG: {e}")
            return None
        finally:
            if sock:
                sock.close()

    def save_photo(self, jpeg_data: bytes, prefix: str = "photo") -> str:
        """保存 JPEG 数据到文件"""
        if jpeg_data is None:
            return None

        # 创建 photos 目录
        os.makedirs("photos", exist_ok=True)

        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")[:-3]
        filename = f"photos/{prefix}_{timestamp}.jpg"

        with open(filename, 'wb') as f:
            f.write(jpeg_data)

        print(f"\n[INFO] 照片已保存: {filename} ({len(jpeg_data)} bytes)")
        return filename


def display_video(h264_client: H264UDPClient, tcp_jpeg: TCPJPEGClient):
    """显示视频和按键处理

    性能优化策略：
    - 使用预分配的工作缓冲区，避免每帧创建新数组
    - 缓存水印字符串，减少字符串格式化开销
    - 使用 cv2.transpose() + cv2.flip() 替代 cv2.rotate()（更高效）
    - 不限制显示帧率上限，让 OpenCV 以最高帧率渲染
    """
    import cv2

    print("\n========== ESP32-P4 视频客户端 ==========")
    print("按键说明:")
    print("  's' - 保存当前视频帧为图片")
    print("  'p' - 通过 TCP 请求 JPEG 拍照")
    print("  'q' - 退出")
    print("==========================================\n")

    cv2.namedWindow("ESP32-P4 H.264 Video", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("ESP32-P4 H.264 Video", 800, 1280)

    frame_save_counter = 0
    os.makedirs("captures", exist_ok=True)

    # 等待帧的超时计数
    no_frame_count = 0
    last_no_frame_log = time.time()
    start_time = time.time()

    # 显示帧率统计
    display_frame_count = 0
    last_display_fps_log = time.time()

    # 用于检测重复帧的 ID
    last_frame_id = 0
    frame_id_counter = 0

    # 预分配工作缓冲区，避免每帧创建新数组
    _work_buffer = None
    _last_timestamp_str = ''
    _timestamp_counter = 0

    while h264_client.running:
        now = time.time()
        frame = h264_client.get_latest_frame()
        if frame is not None:
            no_frame_count = 0
            frame_id_counter += 1

            display_frame_count += 1

            # === 高效图像变换（避免多次分配） ===
            h, w = frame.shape[:2]
            if h > w:
                # 竖屏：使用 transpose（旋转90度） + flip（镜像）
                # cv2.transpose() 比 cv2.rotate() 更高效
                frame = cv2.transpose(frame)
                # transpose 后宽高互换，水平翻转
                frame = cv2.flip(frame, 1)
            else:
                # 横屏：只需水平翻转（摄像头镜像）
                frame = cv2.flip(frame, 1)

            # === 高效水印渲染（缓存字符串，减少格式化开销） ===
            # 每 30 帧才更新一次时间戳字符串（约每秒更新一次）
            _timestamp_counter += 1
            if _timestamp_counter >= 30 or _last_timestamp_str == '':
                _timestamp_counter = 0
                _last_timestamp_str = datetime.now().strftime("%H:%M:%S")
            cv2.putText(frame, f"ESP32-P4 H.264 | {_last_timestamp_str}",
                        (10, 30), cv2.FONT_HERSHEY_SIMPLEX,
                        0.6, (0, 255, 0), 1)

            cv2.imshow("ESP32-P4 H.264 Video", frame)
        else:
            no_frame_count += 1
            elapsed = now - start_time
            # 每 3 秒打印一次等待状态
            if now - last_no_frame_log >= 3.0:
                buf_size = len(h264_client.h264_buffer) // 1024
                esp_status = h264_client.esp32_status
                print(f"\r[INFO] 等待视频帧... ({elapsed:.0f}s) | "
                      f"ESP32: {esp_status} | "
                      f"buf={buf_size}KB   ", end='', flush=True)
                last_no_frame_log = now

        # 每秒打印显示帧率
        if now - last_display_fps_log >= 1.0:
            display_fps = display_frame_count / (now - last_display_fps_log)
            print(f"\n[DISPLAY] 显示FPS: {display_fps:.1f}")
            display_frame_count = 0
            last_display_fps_log = now

        # 按键处理（使用 1ms 延迟，让 imshow 以最高帧率渲染）
        key = cv2.waitKey(1) & 0xFF

        if key == ord('q'):
            print("[INFO] 用户请求退出")
            h264_client.running = False
            break

        elif key == ord('s'):
            # 保存当前视频帧
            if frame is not None:
                frame_save_counter += 1
                timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
                filename = f"captures/video_frame_{timestamp}_{frame_save_counter}.jpg"
                cv2.imwrite(filename, frame)
                print(f"\n[INFO] 视频帧已保存: {filename}")

        elif key == ord('p'):
            # TCP JPEG 拍照
            print("\n[INFO] 请求 JPEG 拍照...")
            jpeg_data = tcp_jpeg.capture_photo()
            if jpeg_data:
                tcp_jpeg.save_photo(jpeg_data)
                # 显示拍照结果
                try:
                    photo_frame = cv2.imdecode(np.frombuffer(jpeg_data, np.uint8), cv2.IMREAD_COLOR)
                    if photo_frame is not None:
                        cv2.imshow("JPEG Photo", photo_frame)
                        cv2.waitKey(1000)
                        cv2.destroyWindow("JPEG Photo")
                except Exception as e:
                    print(f"[WARN] 显示照片失败: {e}")
            else:
                print("[ERROR] JPEG 拍照失败")

    cv2.destroyAllWindows()


def parse_server_address(raw: str) -> str:
    """解析服务器地址"""
    # 移除可能的端口号
    if ':' in raw:
        raw = raw.split(':')[0]
    return raw.strip()


def main():
    if len(sys.argv) < 2:
        print("用法: python h264_video_client.py <ESP32_IP地址>")
        print("示例: python h264_video_client.py 192.168.1.100")
        sys.exit(1)

    server_ip = parse_server_address(sys.argv[1])
    print(f"[INFO] 目标服务器: {server_ip}")

    # 初始化客户端
    h264_client = H264UDPClient(server_ip, H264_UDP_PORT)
    tcp_jpeg = TCPJPEGClient(server_ip, TCP_JPEG_PORT)

    try:
        # 连接 H.264 UDP 流
        h264_client.connect()

        # 启动接收线程
        recv_thread = threading.Thread(target=h264_client.receive_loop, daemon=True)
        recv_thread.start()

        # 等待接收线程启动
        time.sleep(0.5)

        # 显示视频（主线程）
        display_video(h264_client, tcp_jpeg)

    except KeyboardInterrupt:
        print("\n[INFO] 用户中断")
    except Exception as e:
        print(f"[ERROR] {e}")
    finally:
        h264_client.cleanup()


if __name__ == "__main__":
    main()
