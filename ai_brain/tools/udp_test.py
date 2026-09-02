#!/usr/bin/env python3
"""UDP 测试脚本：监听端口 8080，接收 172.20.15.38 的电机 CSV 数据"""
import socket
import time

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.settimeout(2.0)
sock.bind(('', 8080))

print("UDP 监听端口 8080，等待数据... (按 Ctrl+C 停止)\n")

count = 0
start = time.time()
try:
    while True:
        try:
            data, addr = sock.recvfrom(4096)
            count += 1
            decoded = data.decode('utf-8', errors='replace').strip()
            print(f"[#{count}] 来自 {addr[0]}:{addr[1]}  {len(data)} bytes")
            print(f"  RAW: {decoded}")
            
            # 尝试解析 CSV
            parts = decoded.split(',')
            if len(parts) >= 4:
                try:
                    ts = float(parts[0])
                    hip = float(parts[1])
                    knee = float(parts[2])
                    ankle = float(parts[3])
                    print(f"  解析: t={ts:.0f}ms | 右髋={hip:+.3f}° 右膝={knee:+.3f}° 右踝={ankle:+.3f}°")
                except ValueError:
                    print(f"  [WARN] 数值解析失败")
            print()
        except socket.timeout:
            elapsed = time.time() - start
            if count == 0:
                print(f"  等待中... ({elapsed:.0f}s)", end='\r')
            continue
except KeyboardInterrupt:
    print(f"\n\n停止。共收到 {count} 个数据包，耗时 {time.time()-start:.1f}s")
finally:
    sock.close()