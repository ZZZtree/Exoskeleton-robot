#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""9B 模型对比: 本系统(llama.cpp) vs Ollama"""
import json, time, urllib.request, subprocess

MINE = "http://localhost:8080/v1/chat/completions"   # 本系统 9B
OLLAMA = "http://localhost:11434/api/chat"            # Ollama 9b

QS = ["请写一篇150字左右关于秋天的散文", "解释一下什么是人工智能并用三个例子说明", "我有个半径5厘米的球，体积怎么算？请给出完整计算过程"]

def test_mine(q, mt):
    d = json.dumps({"messages":[{"role":"user","content":q}],"max_tokens":mt}).encode()
    r = urllib.request.Request(MINE, data=d, headers={"Content-Type":"application/json"})
    t0=time.time(); x=json.loads(urllib.request.urlopen(r,timeout=180).read()); el=time.time()-t0
    t=x['timings']
    return t['predicted_n'], t['predicted_per_second'], t['prompt_ms']/1000*1000, el

def test_ollama(q, mt):
    d = json.dumps({"model":"qwen3.5:9b","messages":[{"role":"user","content":q}],"stream":False,"think":False}).encode()
    r = urllib.request.Request(OLLAMA, data=d, headers={"Content-Type":"application/json"})
    t0=time.time(); x=json.loads(urllib.request.urlopen(r,timeout=180).read()); el=time.time()-t0
    n=x.get('eval_count',0); dur=x.get('eval_duration',1)/1e9
    prompt_ms=x.get('prompt_eval_duration',0)/1e6
    return n, n/dur, prompt_ms, el

def mem_mb(needle):
    try:
        out=subprocess.run(["ps","-eo","rss,args"],capture_output=True,text=True,timeout=5).stdout
        for line in out.splitlines():
            if needle in line: return int(line.split()[0])//1024
    except: pass
    return 0

print("="*66)
print("  9B 模型对比: 本系统(llama.cpp Q4_K_M) vs Ollama(qwen3.5:9b)")
print("="*66)
# 1. 生成速度
print("\n【1】生成速度")
for q in QS:
    n1,s1,tt1,e1 = test_mine(q, 250)
    n2,s2,tt2,e2 = test_ollama(q, 250)
    print(f"  本系统: {n1}tok {s1:.1f} tok/s | Ollama: {n2}tok {s2:.1f} tok/s")
# 2. 首token
print("\n【2】首 Token 延迟")
for q in QS[:2]:
    n1,s1,tt1,e1 = test_mine(q, 250)
    n2,s2,tt2,e2 = test_ollama(q, 250)
    print(f"  本系统: {tt1:.0f}ms | Ollama: {tt2:.0f}ms")
# 3. 内存
print("\n【3】内存占用 (RSS)")
mm = mem_mb("qwen3.5-9b-q4_k_m")
mo = mem_mb("--model /usr/share/ollama/.ollama/models/blobs/sha256-")
if not mo:
    import subprocess as sp
    out=sp.run(["ps","-eo","rss,args"],capture_output=True,text=True).stdout
    for line in out.splitlines():
        if "ollama" in line and "llama-server" in line:
            mo=int(line.split()[0])//1024
print(f"  本系统: {mm}MB | Ollama: {mo}MB")
