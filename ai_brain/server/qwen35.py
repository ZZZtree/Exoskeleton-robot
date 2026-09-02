#!/usr/bin/env python3
"""
Qwen3.5 无长思考终端 (默认快速回答)
用法: python3 ~/qwen35.py
命令:
  /think        切换思考模式 (默认关闭)
  /image <路径>  附带图片
  /clear        清空历史
  /exit         退出
"""
import base64
import json
import os
import sys
import time
import urllib.request

sys.stdout.reconfigure(line_buffering=True)
MODEL = "qwen3.5:2b"
BASE = "http://127.0.0.1:11434"
THINK = False  # 默认不思考

def chat(messages, images, think, prompt_tail):
    payload = {"model": MODEL, "messages": messages, "stream": True,
               "think": think, "options": {"num_predict": 500}}
    # 图片附到最后一条消息
    if images:
        payload["messages"][-1]["images"] = [base64.b64encode(open(i, "rb").read()).decode() for i in images]

    req = urllib.request.Request(f"{BASE}/api/chat",
                                 data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"})
    t0 = time.time()
    parts = []
    with urllib.request.urlopen(req, timeout=180) as resp:
        for line in resp:
            line = line.decode().strip()
            if not line.startswith("{"):
                continue
            chunk = json.loads(line)
            if chunk.get("done"):
                break
            m = chunk.get("message", {})
            c = m.get("content", "")
            if c:
                parts.append(c)
                print(c, end="", flush=True)
    dt = time.time() - t0
    print(f"\n  [⏱ {dt:.1f}s]\n")
    return "".join(parts)

def main():
    global THINK
    print("=" * 50)
    print(f"  💬 Qwen3.5-2B 无思考终端")
    print(f"  模式: {'思考关闭(快速)' if not THINK else '思考开启(慢但深)'}")
    print(f"  命令: /think | /image <路径> | /clear | /exit")
    print("=" * 50)
    history = []
    pending_images = []

    while True:
        try:
            q = input("\n你> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\n再见!")
            break
        if not q:
            continue
        if q == "/exit":
            print("再见!")
            break
        if q == "/think":
            THINK = not THINK
            print(f"  [思考模式: {'开启' if THINK else '关闭'}]")
            continue
        if q == "/clear":
            history, pending_images = [], []
            print("  [已清空历史]")
            continue
        if q.startswith("/image"):
            parts = q.split(None, 1)
            if len(parts) < 2 or not os.path.exists(parts[1]):
                print("  [图片不存在，用法: /image 路径]")
                continue
            pending_images.append(parts[1])
            print(f"  [已添加图片: {parts[1]}]")
            continue

        history.append({"role": "user", "content": q})
        mode = "思考中" if THINK else "回答中"
        print(f"\nQwen({mode})> ", end="", flush=True)
        ans = chat(history, pending_images, THINK, None)
        history.append({"role": "assistant", "content": ans})
        pending_images = []

if __name__ == "__main__":
    main()
