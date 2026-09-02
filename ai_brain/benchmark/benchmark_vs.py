#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
llama.cpp vs Ollama 速度对比基准 (受控实验)
流程:
  1. 加载 KV cache (重置/冷启动) → 用 10 个问题测 llama.cpp (tok/s + 首token)
  2. 卸载 KV cache (重置)       → 重新加载
  3. 用同样 10 个问题测 Ollama  (tok/s + 首token)
  4. 输出对比表
用法: python3 /home/rock/benchmark_vs.py
"""
import json
import subprocess
import sys
import time
import urllib.request

LLAMA = "http://localhost:8080/v1/chat/completions"
LLAMA_SLOTS = "http://localhost:8080/slots"
OLLAMA = "http://localhost:11434/api/chat"
MAX_TOKENS = 128

QUESTIONS = [
    "用一句话介绍你自己",
    "中国的首都是哪里？请回答",
    "1+1等于几？直接给答案",
    "为什么天空是蓝色的？请简单解释",
    "写一首关于春天的短诗",
    "如何煮鸡蛋？给出步骤",
    "什么是人工智能？用一句话",
    "推荐一本好书并说明理由",
    "解释一下什么是光合作用",
    "一年有几个季节？分别是什么",
]


def reset_llama_kv():
    """重置 llama.cpp 的 KV cache (所有槽位清空)。"""
    try:
        req = urllib.request.Request(
            LLAMA_SLOTS, data=b"{}",
            headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=8) as r:
            r.read()
    except Exception:
        pass
    time.sleep(1)


def reset_ollama_kv():
    """卸载 Ollama 模型 (清空 KV cache), 下次请求重新加载。"""
    subprocess.run(["ollama", "stop", "qwen3.5:2b"],
                   capture_output=True, timeout=10)
    time.sleep(2)


def test_llama(q):
    """测 llama.cpp 单问题, 返回 (tok/s, TTFT秒, 总耗时秒)。"""
    payload = {"messages": [{"role": "user", "content": q}],
               "max_tokens": MAX_TOKENS}
    req = urllib.request.Request(
        LLAMA, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"})
    t0 = time.time()
    with urllib.request.urlopen(req, timeout=300) as resp:
        d = json.loads(resp.read())
    total = time.time() - t0
    t = d["timings"]
    return t["predicted_per_second"], t["prompt_ms"] / 1000.0, total


def test_ollama(q):
    """测 Ollama 单问题, 返回 (tok/s, TTFT秒, 总耗时秒)。"""
    payload = {"model": "qwen3.5:2b",
               "messages": [{"role": "user", "content": q}],
               "stream": False, "think": False}
    req = urllib.request.Request(
        OLLAMA, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"})
    t0 = time.time()
    with urllib.request.urlopen(req, timeout=300) as resp:
        d = json.loads(resp.read())
    total = time.time() - t0
    n = d.get("eval_count", 0)
    dur = d.get("eval_duration", 0) or 1
    ttft = (d.get("prompt_eval_duration", 0) or 0) / 1e9
    return n / dur * 1e9, ttft, total


def proc_rss_kb(needle):
    """按命令行关键字找进程, 返回 (pid, RSS KB)。找不到返回 (None, 0)。"""
    try:
        out = subprocess.run(["ps", "-eo", "pid,rss,args"],
                             capture_output=True, text=True, timeout=5).stdout
        for line in out.splitlines():
            if needle in line:
                parts = line.split()
                return int(parts[0]), int(parts[1])
    except Exception:
        pass
    return None, 0


def run_engine(name, test_fn, reset_fn, proc_needle):
    """先重置 KV cache, 再用 10 个问题测试, 记录速度+内存, 返回结果列表。"""
    print(f"\n{'='*60}")
    print(f"  {name}: 卸载/重置 KV cache → 重新加载 → 测试 10 个问题")
    print(f"{'='*60}")
    reset_fn()
    # 基线内存 (模型加载后)
    _, base_mem = proc_rss_kb(proc_needle)
    results = []
    for i, q in enumerate(QUESTIONS, 1):
        try:
            speed, ttft, total = test_fn(q)
            _, rss = proc_rss_kb(proc_needle)
            results.append((q, speed, ttft, total, rss))
            print(f"  [{i:2d}/10] {q[:18]:<20} {speed:6.1f} tok/s  "
                  f"TTFT {ttft*1000:5.0f}ms  总 {total:.1f}s  内存 {rss/1024:.0f}MB")
        except Exception as e:
            print(f"  [{i:2d}/10] {q[:18]:<20} 失败: {e}")
            results.append((q, 0, 0, 0, 0))
        time.sleep(0.3)
    _, peak_mem = proc_rss_kb(proc_needle)
    print(f"  → {name} 内存: 加载后 {base_mem/1024:.0f}MB → 峰值 {peak_mem/1024:.0f}MB")
    return results, base_mem, peak_mem


def summarize(name, results):
    speeds = [r[1] for r in results if r[1] > 0]
    ttts = [r[2] for r in results if r[2] > 0]
    if not speeds:
        return None
    return {"name": name,
            "avg_speed": sum(speeds) / len(speeds),
            "avg_ttft": sum(ttts) / len(ttts)}


def main():
    print("=" * 60)
    print("  llama.cpp vs Ollama 速度+内存对比基准")
    print(f"  模型: Qwen3.5-2B Q8_0 | 10 个问题 | max_tokens={MAX_TOKENS}")
    print(f"  功耗: MAXN 60W | 每个引擎测试前重置 KV cache")
    print("=" * 60)

    # llama.cpp: 用我们的模型路径区分 (vs Ollama 的 runner 也叫 llama-server)
    llama_res, llama_base, llama_peak = run_engine(
        "llama.cpp", test_llama, reset_llama_kv, "qwen3.5-2b-q8_0.gguf")
    # Ollama: 用它的模型 blob 路径区分
    ollama_res, ollama_base, ollama_peak = run_engine(
        "Ollama    ", test_ollama, reset_ollama_kv,
        "--model /usr/share/ollama/.ollama/models/blobs/sha256-b709")

    ls, os = summarize("llama.cpp", llama_res), summarize("Ollama", ollama_res)

    print(f"\n{'='*60}")
    print("  📊 对比总结 (10 题平均)")
    print(f"{'='*60}")
    print(f"  {'引擎':<12}{'平均tok/s':<12}{'首token':<12}{'内存(加载→峰值)'}")
    print(f"  {'-'*56}")
    if ls:
        print(f"  {'llama.cpp':<12}{ls['avg_speed']:<12.1f}{ls['avg_ttft']*1000:<10.0f}ms  "
              f"{llama_base/1024:.0f}MB → {llama_peak/1024:.0f}MB")
    if os:
        print(f"  {'Ollama':<12}{os['avg_speed']:<12.1f}{os['avg_ttft']*1000:<10.0f}ms  "
              f"{ollama_base/1024:.0f}MB → {ollama_peak/1024:.0f}MB")
    if ls and os and os['avg_speed'] > 0:
        diff = (ls['avg_speed'] / os['avg_speed'] - 1) * 100
        print(f"\n  → 速度: llama.cpp 比 Ollama 快 {diff:.1f}%")
        if ls['avg_ttft'] > 0:
            print(f"  → 首token: llama.cpp 快 {ls['avg_ttft']/os['avg_ttft']*100-100:.0f}%")
        if ollama_peak > 0:
            mem_diff = (llama_peak - ollama_peak) / ollama_peak * 100
            print(f"  → 内存: llama.cpp 比 Ollama {'少' if mem_diff < 0 else '多'} "
                  f"{abs(mem_diff):.0f}% ({llama_peak/1024:.0f}MB vs {ollama_peak/1024:.0f}MB)")


if __name__ == "__main__":
    main()

