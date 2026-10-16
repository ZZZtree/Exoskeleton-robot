#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
热启动对比实测: llama.cpp(我们, 常驻) vs Ollama(keep_alive 卸载)
口径统一: 端到端首 token 延迟 (TTFT) = 发出请求 → 第一个内容 token 到达
场景:
  A. 我们的引擎: 进程启动(冷) → 就绪 → 连续请求(热)
  B. Ollama:     ollama stop(卸载) → 首个请求(冷) → 第二请求(热)
输出: /tmp/hotstart_test.txt
"""
import json, subprocess, time, urllib.request, sys, os

OUT = "/tmp/hotstart_test.txt"
OURS_BIN = "/home/rock/llama.cpp/build/bin/llama-server"
MODEL = "/home/rock/models/qwen3.5-9b-q4_k_m.gguf"
OLLAMA_MODEL = "qwen3.5:9b"
PROMPT = "用一句话介绍你自己"

def log(msg):
    print(msg, flush=True)
    with open(OUT, "a") as f:
        f.write(msg + "\n")

def wait_health(url, timeout=300):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            urllib.request.urlopen(url, timeout=2).read()
            return time.time() - t0
        except Exception:
            time.sleep(0.5)
    return None

def our_stream_ttft():
    """流式请求我们的引擎, 返回 (TTFT秒, 生成tok/s, 总秒)。"""
    payload = {"messages": [{"role": "user", "content": PROMPT}], "max_tokens": 64, "stream": True}
    req = urllib.request.Request("http://localhost:8080/v1/chat/completions",
                                 data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"})
    t0 = time.time(); ttft = None; n = 0; t_last = None
    with urllib.request.urlopen(req, timeout=300) as resp:
        for raw in resp:
            line = raw.decode("utf-8", "replace").strip()
            if not line.startswith("data:"):
                continue
            data = line[5:].strip()
            if data == "[DONE]":
                break
            try:
                obj = json.loads(data)
            except Exception:
                continue
            ch = obj.get("choices", [{}])[0].get("delta", {}).get("content", "")
            if ch:
                if ttft is None:
                    ttft = time.time() - t0
                n += 1; t_last = time.time()
    total = time.time() - t0
    speed = n / (t_last - t0 - ttft) if (ttft and t_last and (t_last - t0 - ttft) > 0) else 0
    return ttft or 0, speed, total

def ollama_stream():
    """流式请求 Ollama, 返回 (TTFT秒, 端到端总秒, 阶段信息dict)。"""
    payload = {"model": OLLAMA_MODEL, "messages": [{"role": "user", "content": PROMPT}],
               "stream": True, "think": False, "options": {"num_predict": 64}}
    req = urllib.request.Request("http://localhost:11434/api/chat",
                                 data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"})
    t0 = time.time(); ttft = None; info = {}
    with urllib.request.urlopen(req, timeout=600) as resp:
        for raw in resp:
            line = raw.decode("utf-8", "replace").strip()
            if not line.startswith("{"):
                continue
            obj = json.loads(line)
            if obj.get("message", {}).get("content") and ttft is None:
                ttft = time.time() - t0
            if obj.get("done"):
                info = obj
                break
    total = time.time() - t0
    return ttft or 0, total, info

def main():
    open(OUT, "w").close()
    log("=" * 66)
    log("  热启动对比实测: llama.cpp(常驻) vs Ollama(空闲卸载)")
    log(f"  模型: 我们都是 9B | 口径: 端到端首token(TTFT) | {time.strftime('%Y-%m-%d %H:%M')}")
    log("=" * 66)

    # ---------------- A. 我们的引擎 ----------------
    log("\n[A] llama.cpp (我们, 9B q4_k_m, 64K池, 16slot, mlock 常驻)")
    subprocess.run(["pkill", "-f", "llama-server.*qwen3.5-9b"], capture_output=True)
    time.sleep(1)
    t_start = time.time()
    with open("/tmp/hot_ours_server.log", "wb") as lf:
        subprocess.Popen([OURS_BIN, "-m", MODEL, "-ngl", "99", "-t", "8", "-c", "65536",
                          "--parallel", "16", "--kv-unified", "--host", "0.0.0.0", "--port", "8080",
                          "-fa", "on", "-ctk", "q8_0", "-ctv", "q8_0", "-lm", "mlock",
                          "--reasoning", "off", "--cache-prompt", "--cache-reuse", "256",
                          "--slot-save-path", "/home/rock/kv_swap/"],
                         stdout=lf, stderr=subprocess.STDOUT)
    ready = wait_health("http://localhost:8080/health", timeout=300)
    if ready is None:
        log("  ❌ 启动超时, 见 /tmp/hot_ours_server.log")
        return
    log(f"  进程启动 → 可服务: {time.time()-t_start:.1f}s  (含加载 5.8GB 权重 + 64K KV 池分配)")

    for i in range(1, 4):
        ttft, speed, total = our_stream_ttft()
        tag = "【首次请求, 含预热】" if i == 1 else "【热启动】"
        log(f"  请求{i} {tag}: TTFT {ttft*1000:6.0f}ms | 生成 {speed:5.1f} tok/s | 总 {total:.1f}s")

    # ---------------- B. Ollama ----------------
    log("\n[B] Ollama (9B, 默认 keep_alive —— 空闲自动卸载)")
    subprocess.run(["ollama", "stop", OLLAMA_MODEL], capture_output=True)
    time.sleep(3)
    log("  已执行 ollama stop → 模型卸载 (模拟空闲卸载状态)")

    ttft_c, total_c, info = ollama_stream()
    load = info.get("load_duration", 0) / 1e9
    peval = info.get("prompt_eval_duration", 0) / 1e9
    log(f"  冷启动(重建载): TTFT {ttft_c*1000:6.0f}ms | 总 {total_c:.1f}s")
    log(f"     其中 模型加载+CUDA {load:.1f}s | 预填充 {peval*1000:.0f}ms")

    time.sleep(1)
    ttft_h, total_h, info_h = ollama_stream()
    load_h = info_h.get("load_duration", 0) / 1e9
    log(f"  热启动(常驻后): TTFT {ttft_h*1000:6.0f}ms | 总 {total_h:.1f}s | 加载 {load_h:.2f}s")

    log("\n" + "=" * 66)
    log("  📊 对比 (端到端首 token)")
    log("=" * 66)
    log(f"  我们(常驻)          : ~{our_stream_ttft()[0]*1000:.0f}ms  ← 始终热启动")
    log(f"  Ollama 热(常驻后)    : ~{ttft_h*1000:.0f}ms")
    log(f"  Ollama 冷(卸载后)    : ~{ttft_c*1000:.0f}ms  ← 含加载 {load:.1f}s")
    log(f"  → 冷/热差距: {ttft_c/max(ttft_h,0.001):.0f} 倍")

if __name__ == "__main__":
    main()
