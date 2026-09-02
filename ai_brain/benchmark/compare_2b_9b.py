#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""2B vs 9B 模型对比 (同问题, 速度/质量/内存)"""
import json, time, urllib.request, subprocess

M9 = "http://localhost:8080/v1/chat/completions"   # 9B
M2 = "http://localhost:8081/v1/chat/completions"   # 2B

QS = [
    "用三句话介绍什么是人工智能，并举例说明",
    "请写一首关于秋天的五言绝句",
    "如果我有一个半径5厘米的球，它的体积是多少？给出计算过程",
]

def ask(url, q):
    d = json.dumps({"messages":[{"role":"user","content":q}],"max_tokens":200}).encode()
    r = urllib.request.Request(url, data=d, headers={"Content-Type":"application/json"})
    t0=time.time()
    x = json.loads(urllib.request.urlopen(r,timeout=180).read())
    el=time.time()-t0
    t=x["timings"]
    return x["choices"][0]["message"]["content"], t["predicted_n"], t["predicted_per_second"], t["prompt_ms"]/1000, el

def mem_kb(pattern):
    try:
        out = subprocess.run(["ps","-eo","rss,args"],capture_output=True,text=True,timeout=5).stdout
        for line in out.splitlines():
            if pattern in line:
                return int(line.split()[0])//1024
    except: pass
    return 0

def show(url, name, q):
    try:
        ans, n, spd, ttft, el = ask(url, q)
        print(f"  [{name}] {spd:5.1f} tok/s | TTFT {ttft*1000:5.0f}ms | {n}tok | 总{el:.1f}s")
        print(f"    答: {ans[:80]}...")
    except Exception as e:
        print(f"  [{name}] 失败: {e}")

print("="*62)
print("  2B (Q8_0) vs 9B (Q4_K_M) 对比")
print("="*62)
for i, q in enumerate(QS, 1):
    print(f"\n【问题{i}】{q}")
    show(M9, "9B", q)
    show(M2, "2B", q)

print("\n"+"="*62)
print("  内存占用 (RSS)")
m9 = mem_kb("qwen3.5-9b-q4_k_m")
m2 = mem_kb("qwen3.5-2b-q8_0")
print(f"  9B: {m9}MB | 2B: {m2}MB | 差距 {m9-m2}MB")
print("="*62)
