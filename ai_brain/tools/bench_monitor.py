#!/usr/bin/env python3
"""启动推理 + 高频采样, 捕捉推理时的CPU/GPU/内存真实负载"""
import json
import subprocess
import threading
import time

TEGRASTATS = "/usr/bin/tegrastats"

def start_inference():
    payload = {
        "model": "qwen2.5vl:3b",
        "messages": [{"role": "user",
                      "content": "请写一篇500字以上关于人工智能未来发展的文章"}],
        "stream": False,
        "options": {"num_predict": 900},
    }
    subprocess.Popen(
        ["curl", "-s", "--max-time", "120",
         "http://127.0.0.1:11434/api/chat",
         "-H", "Content-Type: application/json",
         "-d", json.dumps(payload),
         "-o", "/tmp/bench_result.json"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

# 启动推理
start_inference()
print("推理已启动, 开始高频采样 (0.5秒/次, 共15秒)...\n")
print(f"{'时间':<10}{'RAM(MB)':<10}{'CPU均%':<8}{'GPU%':<6}{'CPU频率':<9}{'功耗W':<6}{'温度°C'}")
samples = []
t0 = time.time()
while time.time() - t0 < 15:
    try:
        out = subprocess.run(["timeout", "1", TEGRASTATS, "--interval", "500"],
                             capture_output=True, text=True, timeout=2).stdout
        line = [l for l in out.strip().split("\n") if l.strip()][-1]
        # 解析
        import re
        m_ram = re.search(r"RAM (\d+)/(\d+)MB", line)
        m_cpu = re.search(r"CPU \[([^\]]+)\]", line)
        m_gpu = re.search(r"GR3D_FREQ (\d+)%", line)
        m_tj = re.search(r"tj@([\d.]+)C", line)
        m_pwr = re.search(r"VDD_GPU_SOC (\d+)mW/(\d+)mW", line)
        cores = [int(c.split('%')[0]) for c in m_cpu.group(1).split(',')
                 if c.strip() != 'off']
        cpu_avg = sum(cores) / len(cores) if cores else 0
        freqs = [int(re.match(r'\d+%@(\d+)', c).group(1)) for c in
                 m_cpu.group(1).split(',') if c.strip() != 'off']
        row = {
            "ram": int(m_ram.group(1)), "cpu": cpu_avg,
            "gpu": int(m_gpu.group(1)) if m_gpu else 0,
            "freq": max(freqs) if freqs else 0,
            "pwr": int(m_pwr.group(2)) / 1000 if m_pwr else 0,
            "temp": float(m_tj.group(1)) if m_tj else 0,
        }
        samples.append(row)
        print(f"{time.strftime('%H:%M:%S'):<10}{row['ram']:<10}"
              f"{row['cpu']:<8.0f}{row['gpu']:<6}"
              f"{row['freq']/1000:<9.1f}{row['pwr']:<6.1f}{row['temp']:<6.1f}")
    except Exception as e:
        print(f"  采样错误: {e}")
    time.sleep(0.3)

print("\n===== 采样统计 =====")
print(f"  GPU 峰值: {max(s['gpu'] for s in samples)}%"
      f"  GPU 平均: {sum(s['gpu'] for s in samples)/len(samples):.0f}%")
print(f"  CPU 峰值: {max(s['cpu'] for s in samples):.0f}%"
      f"  CPU 平均: {sum(s['cpu'] for s in samples)/len(samples):.0f}%")
print(f"  RAM 峰值: {max(s['ram'] for s in samples)}MB"
      f"  (加载模型后较空闲时多约 {max(s['ram'] for s in samples)-12100}MB)")
print(f"  温度峰值: {max(s['temp'] for s in samples):.1f}°C")
print(f"  功耗峰值: {max(s['pwr'] for s in samples):.1f}W")
