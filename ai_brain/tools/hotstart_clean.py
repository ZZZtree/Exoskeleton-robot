#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
干净状态热启动复测: 我们的服务重启到干净态, 与 Ollama 常驻态对比
各 8 次流式 TTFT, 输出统计 + 每次明细
输出: /tmp/hotstart_clean.txt
"""
import json, subprocess, time, urllib.request, statistics

OUT = "/tmp/hotstart_clean.txt"
BIN = "/home/rock/llama.cpp/build/bin/llama-server"
MODEL = "/home/rock/models/qwen3.5-9b-q4_k_m.gguf"
PROMPT = "用一句话介绍你自己"

def log(m):
    print(m, flush=True)
    open(OUT, "a").write(m + "\n")

def wait_health(timeout=300):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            urllib.request.urlopen("http://localhost:8080/health", timeout=2).read()
            return time.time() - t0
        except Exception:
            time.sleep(0.5)
    return None

def ours_ttft():
    p = {"messages": [{"role": "user", "content": PROMPT}], "max_tokens": 32, "stream": True}
    r = urllib.request.Request("http://localhost:8080/v1/chat/completions",
                               data=json.dumps(p).encode(),
                               headers={"Content-Type": "application/json"})
    t0 = time.time()
    with urllib.request.urlopen(r, timeout=120) as resp:
        for raw in resp:
            line = raw.decode("utf-8", "replace").strip()
            if line.startswith("data:") and line[5:].strip() != "[DONE]":
                try:
                    o = json.loads(line[5:].strip())
                except Exception:
                    continue
                if o.get("choices", [{}])[0].get("delta", {}).get("content"):
                    return time.time() - t0
    return None

def ollama_ttft():
    p = {"model": "qwen3.5:9b", "messages": [{"role": "user", "content": PROMPT}],
         "stream": True, "think": False, "options": {"num_predict": 32}}
    r = urllib.request.Request("http://localhost:11434/api/chat",
                               data=json.dumps(p).encode(),
                               headers={"Content-Type": "application/json"})
    t0 = time.time()
    with urllib.request.urlopen(r, timeout=600) as resp:
        for raw in resp:
            line = raw.decode("utf-8", "replace").strip()
            if not line.startswith("{"):
                continue
            o = json.loads(line)
            if o.get("message", {}).get("content"):
                return time.time() - t0
    return None

def main():
    open(OUT, "w").close()
    log("=" * 68)
    log("  干净状态热启动复测 (端到端首 token, 各 8 次)")
    log(f"  两边同为 9B | {time.strftime('%Y-%m-%d %H:%M')}")
    log("=" * 68)

    # --- 重启我们的服务到干净态 ---
    log("\n[A] 重启 llama.cpp 到干净态 (清空所有 slot 与缓存)...")
    subprocess.run(["pkill", "-f", "llama-server.*qwen3.5-9b"], capture_output=True)
    time.sleep(3)
    t0 = time.time()
    with open("/tmp/hot_ours_server2.log", "wb") as lf:
        subprocess.Popen([BIN, "-m", MODEL, "-ngl", "99", "-t", "8", "-c", "65536",
                          "--parallel", "16", "--kv-unified", "--host", "0.0.0.0", "--port", "8080",
                          "-fa", "on", "-ctk", "q8_0", "-ctv", "q8_0", "-lm", "mlock",
                          "--reasoning", "off", "--cache-prompt", "--cache-reuse", "256",
                          "--slot-save-path", "/home/rock/kv_swap/"],
                         stdout=lf, stderr=subprocess.STDOUT)
    ready = wait_health()
    if ready is None:
        log("  ❌ 启动超时"); return
    log(f"  冷启动就绪: {time.time()-t0:.1f}s")

    # 第一次请求(预热) 单独记录
    warm = ours_ttft()
    log(f"  首次请求(含预热): {warm*1000:.0f}ms")
    a = []
    for i in range(8):
        x = ours_ttft()
        a.append(x)
        log(f"    第{i+2}次请求: {x*1000:.0f}ms")
        time.sleep(0.5)
    log(f"  → 我们 热启动 TTFT: 平均 {statistics.mean(a)*1000:.0f}ms | "
        f"中位 {statistics.median(a)*1000:.0f}ms | 最小 {min(a)*1000:.0f}ms | 最大 {max(a)*1000:.0f}ms")

    # --- Ollama ---
    log("\n[B] Ollama (9B) 常驻态 8 次...")
    ollama_ttft()  # 预热
    time.sleep(1)
    b = []
    for i in range(8):
        x = ollama_ttft()
        b.append(x)
        log(f"    第{i+1}次请求: {x*1000:.0f}ms")
        time.sleep(0.5)
    log(f"  → Ollama 热启动 TTFT: 平均 {statistics.mean(b)*1000:.0f}ms | "
        f"中位 {statistics.median(b)*1000:.0f}ms | 最小 {min(b)*1000:.0f}ms | 最大 {max(b)*1000:.0f}ms")

    ma, mb = statistics.median(a), statistics.median(b)
    log(f"\n  中位数对比: 我们 {ma*1000:.0f}ms vs Ollama {mb*1000:.0f}ms "
        f"({'我们快' if ma < mb else 'Ollama 快'} {abs(mb/ma-1)*100:.0f}%)")

if __name__ == "__main__":
    main()
