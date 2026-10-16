#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
热启动重复测量 (消除单次采样误差)
- 我们: 直连 llama-server :8080，5 次流式 TTFT
- Ollama: 确保常驻后，5 次流式 TTFT
输出: /tmp/hotstart_verify.txt
"""
import json, subprocess, time, urllib.request, statistics

OUT = "/tmp/hotstart_verify.txt"
PROMPT = "用一句话介绍你自己"

def log(m):
    print(m, flush=True)
    open(OUT, "a").write(m + "\n")

def ours_ttft():
    p = {"messages": [{"role": "user", "content": PROMPT}], "max_tokens": 32, "stream": True}
    r = urllib.request.Request("http://localhost:8080/v1/chat/completions",
                               data=json.dumps(p).encode(),
                               headers={"Content-Type": "application/json"})
    t0 = time.time(); ttft = None
    with urllib.request.urlopen(r, timeout=120) as resp:
        for raw in resp:
            line = raw.decode("utf-8", "replace").strip()
            if line.startswith("data:") and line[5:].strip() != "[DONE]":
                try:
                    o = json.loads(line[5:].strip())
                except Exception:
                    continue
                if o.get("choices", [{}])[0].get("delta", {}).get("content"):
                    ttft = time.time() - t0; break
    return ttft

def ollama_ttft():
    p = {"model": "qwen3.5:9b", "messages": [{"role": "user", "content": PROMPT}],
         "stream": True, "think": False, "options": {"num_predict": 32}}
    r = urllib.request.Request("http://localhost:11434/api/chat",
                               data=json.dumps(p).encode(),
                               headers={"Content-Type": "application/json"})
    t0 = time.time(); ttft = None
    with urllib.request.urlopen(r, timeout=600) as resp:
        for raw in resp:
            line = raw.decode("utf-8", "replace").strip()
            if not line.startswith("{"):
                continue
            o = json.loads(line)
            if o.get("message", {}).get("content"):
                ttft = time.time() - t0; break
    return ttft

def stats(name, xs):
    xs = [x for x in xs if x]
    if not xs:
        log(f"  {name}: 无数据"); return
    log(f"  {name}: 平均 {statistics.mean(xs)*1000:.0f}ms | "
        f"最小 {min(xs)*1000:.0f}ms | 最大 {max(xs)*1000:.0f}ms  (n={len(xs)})")

def main():
    open(OUT, "w").close()
    log("=" * 66)
    log("  热启动重复测量 (端到端首 token, 各 5 次)")
    log(f"  两边同为 9B | {time.strftime('%Y-%m-%d %H:%M')}")
    log("=" * 66)

    # 确保我们的服务在跑
    try:
        urllib.request.urlopen("http://localhost:8080/health", timeout=3).read()
        log("\n[A] llama.cpp 已在常驻状态")
    except Exception:
        log("\n[A] llama.cpp 未运行, 跳过")
        a = []
        ours_ok = False
    else:
        ours_ok = True
        a = [ours_ttft() for _ in range(5)]
        stats("我们 (llama.cpp 常驻)", a)

    log("\n[B] Ollama (9B)")
    log("  预热一次(确保常驻)...")
    ollama_ttft()
    time.sleep(1)
    b = [ollama_ttft() for _ in range(5)]
    stats("Ollama (常驻后)", b)

    if ours_ok and a and b:
        ma, mb = statistics.mean([x for x in a if x]), statistics.mean([x for x in b if x])
        log(f"\n  → 热启动 TTFT: 我们 {ma*1000:.0f}ms vs Ollama {mb*1000:.0f}ms "
            f"(快 {(mb/ma-1)*100:.0f}%)")
    log(f"\n  提示: 两边模型大小 我们 5.8GB(q4_k_m) vs Ollama 6.6GB → 详见文档")

if __name__ == "__main__":
    main()
