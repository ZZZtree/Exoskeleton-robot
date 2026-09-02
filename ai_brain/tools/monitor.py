#!/usr/bin/env python3
"""
Jetson AGX Orin 性能监控工具
用法:
  python3 ~/monitor.py               # 单次快照
  python3 ~/monitor.py --loop 2      # 每2秒实时刷新 (Ctrl+C 停止)
  python3 ~/monitor.py --log 2 120   # 每2秒记录120秒, 输出CSV到~/monitor_log.csv
"""
import csv
import os
import re
import subprocess
import sys
import time

TEGRASTATS = "/usr/bin/tegrastats"

def get_tegrastats():
    """运行一次 tegrastats 并解析"""
    try:
        out = subprocess.run(
            ["timeout", "2", TEGRASTATS, "--interval", "1000"],
            capture_output=True, text=True, timeout=4
        ).stdout
        lines = [l for l in out.strip().split("\n") if l.strip()]
        return parse_tegrastats(lines[-1]) if lines else {}
    except Exception as e:
        return {"error": str(e)}

def parse_tegrastats(line):
    d = {}
    m = re.search(r"RAM (\d+)/(\d+)MB", line)
    if m:
        d["ram_used_mb"] = int(m.group(1)); d["ram_total_mb"] = int(m.group(2))
    m = re.search(r"SWAP (\d+)/(\d+)MB", line)
    if m:
        d["swap_used_mb"] = int(m.group(1)); d["swap_total_mb"] = int(m.group(2))
    m = re.search(r"CPU \[([^\]]+)\]", line)
    if m:
        cores = []
        for c in m.group(1).split(","):
            cm = re.match(r"(\d+)%@(\d+)", c.strip())
            if cm:
                cores.append({"util": int(cm.group(1)), "freq": int(cm.group(2))})
            elif c.strip() == "off":
                cores.append({"util": -1, "freq": 0})
        d["cpu_cores"] = cores
        active = [c for c in cores if c["util"] >= 0]
        d["cpu_avg"] = sum(c["util"] for c in active) / len(active) if active else 0
        d["cpu_freq_max"] = max((c["freq"] for c in active), default=0)
    m = re.search(r"GR3D_FREQ (\d+)%", line)
    if m:
        d["gpu_util"] = int(m.group(1))
    for key in ["cpu", "soc0", "soc1", "soc2", "tj"]:
        m = re.search(rf"{key}@([\d.]+)C", line)
        if m:
            d[f"temp_{key}"] = float(m.group(1))
    m = re.search(r"VDD_GPU_SOC (\d+)mW/(\d+)mW", line)
    if m:
        d["gpu_pwr_mw"] = int(m.group(2))
    m = re.search(r"VDD_CPU_CV (\d+)mW/(\d+)mW", line)
    if m:
        d["cpu_pwr_mw"] = int(m.group(2))
    return d

def get_ollama_ps():
    try:
        out = subprocess.run(["/usr/local/bin/ollama", "ps"],
                             capture_output=True, text=True, timeout=4).stdout
        lines = [l for l in out.split("\n") if l.strip()]
        if len(lines) < 2:
            return "无模型加载"
        return "; ".join(r.strip() for r in lines[1:]) or "无模型加载"
    except Exception as e:
        return f"ollama ps 错误: {e}"

def get_top_procs(n=5):
    out = subprocess.run(
        ["ps", "aux", "--sort=-%cpu"],
        capture_output=True, text=True, timeout=4).stdout
    lines = out.strip().split("\n")[1:]
    return sorted(lines, key=lambda l: float(l.split()[2]), reverse=True)[:n]

def snapshot():
    d = get_tegrastats()
    print("=" * 62)
    print(f"  Jetson AGX Orin 性能快照  {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 62)
    if "error" in d:
        print(f"  ❌ tegrastats 错误: {d['error']}")
        return d
    used = d["ram_used_mb"]; total = d["ram_total_mb"]
    pct = used / total * 100
    print(f"  内存: {used/1024:.1f}GB / {total/1024:.1f}GB ({pct:.0f}%)"
          f"  | 交换: {d['swap_used_mb']/1024:.1f}GB")
    cores = d["cpu_cores"]
    bar = "".join("█" if c["util"] >= 50 else ("▆" if c["util"] >= 20 else "▂")
                  if c["util"] >= 0 else "·" for c in cores)
    print(f"  CPU: 平均 {d['cpu_avg']:.0f}%  频率 {d['cpu_freq_max']/1000:.1f}GHz")
    print(f"       核心分布: {bar}")
    g = d.get("gpu_util", 0)
    gbar = "█" * (g // 10) + "░" * (10 - g // 10)
    print(f"  GPU: [{gbar}] {g}%")
    temps = [f"{k.upper()}: {v:.1f}°C" for k, v in d.items()
             if k.startswith("temp_")]
    warn = " ⚠️高温!" if any(v > 80 for k, v in d.items() if k.startswith("temp_")) else ""
    print(f"  温度: {'  '.join(temps)}{warn}")
    print(f"  功耗: GPU+SoC {d.get('gpu_pwr_mw',0)/1000:.1f}W"
          f"  CPU {d.get('cpu_pwr_mw',0)/1000:.1f}W")
    print(f"\n  Ollama: {get_ollama_ps()}")
    print("\n  占用最高的进程 (CPU% / MEM% / RSS / 命令):")
    for l in get_top_procs(4):
        f = l.split(None, 10)
        print(f"    {f[2]:>4}%  {f[3]:>4}%  {int(f[5])//1024:>5}MB  {f[10][:50]}")
    print("=" * 62)
    return d



def loop_mode(interval):
    print(f"实时监控 (每{interval}秒, Ctrl+C 停止)...\n")
    try:
        while True:
            d = snapshot()
            if d.get("gpu_util", 0) > 80 or d.get("cpu_avg", 0) > 80:
                print("  🚨 高负载警告! GPU/CPU > 80%")
            if any(v > 80 for k, v in d.items() if k.startswith("temp_")):
                print("  🚨 温度过高! 注意散热")
            print()
            time.sleep(interval)
    except KeyboardInterrupt:
        print("\n监控结束。")

def log_mode(interval, duration):
    path = os.path.expanduser("~/monitor_log.csv")
    fields = ["time", "ram_used_mb", "ram_total_mb", "cpu_avg", "gpu_util",
              "temp_cpu", "temp_tj", "gpu_pwr_mw"]
    print(f"记录模式: 每{interval}秒, 共{duration}秒 → {path}")
    t0 = time.time()
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        while time.time() - t0 < duration:
            d = get_tegrastats()
            row = {"time": time.strftime("%H:%M:%S")}
            for k in fields[1:]:
                row[k] = d.get(k, "")
            w.writerow(row)
            f.flush()
            print(f"  [{row['time']}] RAM {row['ram_used_mb']}MB"
                  f" CPU {row['cpu_avg']}% GPU {row['gpu_util']}%"
                  f" temp_tj {row['temp_tj']}°C")
            time.sleep(interval)
    print(f"\n完成! 数据已保存到 {path}")

if __name__ == "__main__":
    args = sys.argv[1:]
    if args and args[0] == "--loop" and len(args) > 1:
        loop_mode(float(args[1]))
    elif args and args[0] == "--log" and len(args) > 2:
        log_mode(float(args[1]), float(args[2]))
    else:
        snapshot()
