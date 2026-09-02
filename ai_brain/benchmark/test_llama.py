#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
llama.cpp 交互式测试终端 (当前 9B 模型)
用法: python3 /home/rock/test_llama.py
  输入问题 → 流式输出 + 实时统计 (tok/s, 首token延迟, 总耗时)
  命令: /clear 清历史 | /mt <n> 调token | /ollama 对比 | /quit 退出
"""
import json
import sys
import time
import urllib.request

LLAMA = f"http://localhost:{sys.argv[1] if len(sys.argv) > 1 else 8080}/v1/chat/completions"
OLLAMA = "http://localhost:11434/api/chat"
max_tokens = 300
history = []


def model_label():
    port = sys.argv[1] if len(sys.argv) > 1 else 8080
    return "Qwen3.5-9B Q4_K_M" if port == "8080" else "Qwen3.5-2B Q8_0"


def stream_llama(msgs, mt):
    """流式请求 llama.cpp, 逐字输出, 返回统计。"""
    payload = {"messages": msgs, "stream": True, "max_tokens": mt}
    req = urllib.request.Request(LLAMA, data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"})
    t0 = time.time()
    first_at = None
    buf = []
    with urllib.request.urlopen(req, timeout=600) as resp:
        for raw in resp:
            line = raw.decode("utf-8", "replace").strip()
            if not line.startswith("data:"):
                continue
            data = line[5:].strip()
            if data == "[DONE]":
                break
            try:
                obj = json.loads(data)
            except json.JSONDecodeError:
                continue
            c = obj["choices"][0].get("delta", {}).get("content")
            if c:
                if first_at is None:
                    first_at = time.time()
                buf.append(c)
                sys.stdout.write(c)
                sys.stdout.flush()
    elapsed = time.time() - t0
    ttft = (first_at - t0) * 1000 if first_at else 0
    n = len(buf)
    print(f"\n── 统计: {n} tokens | {n/elapsed:.1f} tok/s | 首token {ttft:.0f}ms | 总 {elapsed:.1f}s")
    return "".join(buf)


def ask_ollama(msgs, mt):
    payload = {"model": "qwen3.5:9b", "messages": msgs,
               "stream": False, "think": False}
    req = urllib.request.Request(OLLAMA, data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"})
    t0 = time.time()
    with urllib.request.urlopen(req, timeout=600) as resp:
        d = json.loads(resp.read())
    elapsed = time.time() - t0
    content = (d.get("message", {}).get("content") or "").strip()
    n = d.get("eval_count", 0)
    print(f"[Ollama] {content}")
    print(f"── [Ollama] {n} tokens | {n/elapsed:.1f} tok/s | 总 {elapsed:.1f}s")
    return content


def main():
    global max_tokens
    print("=" * 56)
    print(f"  llama.cpp 交互式测试 ({model_label()})")
    print(f"  端口: {sys.argv[1] if len(sys.argv)>1 else 8080} | 输入问题回车 | /ollama 对比 | /clear /mt /quit")
    print("=" * 56)
    while True:
        try:
            q = input("\n你 > ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\n再见!")
            break
        if not q:
            continue
        if q.lower() in ("/quit", "q", "exit"):
            print("再见!")
            break
        if q == "/clear":
            history.clear()
            print("✅ 已清空")
            continue
        if q.startswith("/mt"):
            try:
                max_tokens = int(q.split()[1])
                print(f"✅ max_tokens = {max_tokens}")
            except (IndexError, ValueError):
                print(f"用法: /mt <n> (当前 {max_tokens})")
            continue
        if q == "/ollama":
            if not history:
                print("先输入一个问题再对比")
                continue
            last_user = next((m["content"] for m in reversed(history)
                              if m["role"] == "user"), None)
            if last_user:
                print("\n同一问题对比 Ollama 9b:")
                ask_ollama([{"role": "user", "content": last_user}], max_tokens)
            continue
        history.append({"role": "user", "content": q})
        print("\n[llama.cpp] ", end="")
        ans = stream_llama(list(history), max_tokens)
        history.append({"role": "assistant", "content": ans})


if __name__ == "__main__":
    main()
