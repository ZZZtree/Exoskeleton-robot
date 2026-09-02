#!/usr/bin/env python3
"""
冷启动全过程精准测量 (Qwen3.5-2B)
测量步骤:
  1. 请求发出时间
  2. llama-server 进程启动时间
  3. 模型加载 + CUDA初始化 (ollama官方load_duration)
  4. 提示词预填充 (prompt_eval_duration)
  5. 首token生成时间 (TTFT)
  6. 完整响应时间
"""
import json
import subprocess
import sys
import threading
import time
import urllib.request

sys.stdout.reconfigure(line_buffering=True)

def get_llama_pid():
    out = subprocess.run(["pgrep", "-f", "llama-server"], capture_output=True,
                         text=True, timeout=3).stdout.strip()
    return out.split("\n")[0] if out else None

def main():
    MODEL = "qwen3.5:2b"
    print("=" * 60)
    print("  🧊 冷启动全过程精准测量")
    print("=" * 60)

    # 确认模型未加载 + 缓存已清
    print(f"\n[准备] 模型: {MODEL}")
    print(f"[准备] llama-server 进程: {get_llama_pid() or '无(已卸载)'}")

    # 发起流式请求, 记录各时间点
    payload = {"model": MODEL,
               "messages": [{"role": "user", "content": "你好"}],
               "stream": True, "think": False}
    req = urllib.request.Request("http://127.0.0.1:11434/api/chat",
                                 data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"})

    t_request = time.time()      # t0: 请求发出
    llama_spawn_t = None
    first_token_t = None
    done_info = {}

    print(f"\n⏱  请求发出: t=0.000s")
    with urllib.request.urlopen(req, timeout=180) as resp:
        for line in resp:
            line = line.decode().strip()
            if not line.startswith("{"):
                continue
            now = time.time()
            chunk = json.loads(line)
            if chunk.get("done"):
                done_info = chunk
                break
            m = chunk.get("message", {})
            if m.get("content") and first_token_t is None:
                first_token_t = now
            # 检测 llama-server 进程出现
            if llama_spawn_t is None and get_llama_pid():
                llama_spawn_t = now
    t_done = time.time()

    # 输出时间线
    print(f"⏱  llama-server 进程出现: +{(llama_spawn_t - t_request)*1000:.0f}ms" if llama_spawn_t else "⚠️ 未检测到进程出现")
    print(f"⏱  首token到达: +{(first_token_t - t_request)*1000:.0f}ms" if first_token_t else "⚠️ 无首token")
    print(f"⏱  响应完成: +{(t_done - t_request)*1000:.0f}ms")

    # Ollama 官方分段时间
    print("\n" + "=" * 60)
    print("  📊 Ollama 官方阶段细分")
    print("=" * 60)
    total = done_info.get("total_duration", 0) / 1e9
    load = done_info.get("load_duration", 0) / 1e9
    p_eval = done_info.get("prompt_eval_duration", 0) / 1e9
    eval_d = done_info.get("eval_duration", 0) / 1e9
    print(f"  总耗时:        {total:.2f}s")
    print(f"  ① 模型加载+CUDA: {load:.2f}s  ({load/total*100:.0f}%)")
    print(f"  ② 提示词预填充:  {p_eval:.3f}s")
    print(f"  ③ token生成:     {eval_d:.3f}s  ({done_info.get('eval_count',0)} tokens)")
    print(f"  ④ 其他开销:      {total-load-p_eval-eval_d:.3f}s")
    print(f"\n  首token延迟(TTFT)≈ 加载 + 预填充 ≈ {load+p_eval:.2f}s")

    # 磁盘读取对比(参考)
    print("\n" + "=" * 60)
    print("  📁 磁盘读取对比")
    print("=" * 60)
    blob = "/usr/share/ollama/.ollama/models/blobs/"
    out = subprocess.run(["bash", "-c", f"ls -la {blob} | sort -k5 -rh | head -1"],
                         capture_output=True, text=True, timeout=5).stdout
    size_mb = int(out.split()[4]) // 1024 // 1024 if out else 0
    print(f"  模型文件: {size_mb}MB")
    print(f"  eMMC读速~236MB/s → 纯IO约 {size_mb/236:.1f}s (但被GPU传输掩盖)")

if __name__ == "__main__":
    main()
