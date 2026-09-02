#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
双模型流式对比终端 (边输出边显示)
  [1] llama.cpp  http://localhost:8080   --reasoning off (无思考, 直接回答)
  [2] Ollama     http://localhost:11434  think:false     (无思考, 直接回答)

用法: python3 /home/rock/compare_models.py
  /clear 清历史  /mt <n> 调token数  /quit 退出
"""
import json
import shutil
import sys
import threading
import time
import unicodedata
import urllib.request

LLAMA_URL = "http://localhost:8080/v1/chat/completions"
OLLAMA_URL = "http://localhost:11434/api/chat"   # /api/chat 才支持 think:false

max_tokens = 512
histories = {"llama.cpp": [], "ollama": []}

LOCK = threading.Lock()
panels = {"llama.cpp": [], "ollama": []}      # 已收到的文本
finished = {"llama.cpp": False, "ollama": False}
stats = {}


def stream_llama(messages, mt):
    payload = {"messages": messages, "stream": True, "max_tokens": mt}
    req = urllib.request.Request(LLAMA_URL, data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"})
    t0 = time.time(); n_tok = 0
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
            delta = obj["choices"][0].get("delta", {})
            c = delta.get("content")
            if c:
                with LOCK:
                    panels["llama.cpp"].append(c)
                n_tok += 1
    elapsed = time.time() - t0
    stats["llama.cpp"] = {"time_s": elapsed, "tokens": n_tok,
                          "speed": round(n_tok / elapsed, 1) if elapsed else 0}


def stream_ollama(messages, mt):
    payload = {"model": "qwen3.5:2b", "messages": messages,
               "stream": True, "think": False}
    req = urllib.request.Request(OLLAMA_URL, data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"})
    t0 = time.time(); n_tok = 0
    with urllib.request.urlopen(req, timeout=600) as resp:
        for raw in resp:
            line = raw.decode("utf-8", "replace").strip()
            if not line:
                continue
            # Ollama /api/chat 流式是 NDJSON(无 data: 前缀); 也兼容 SSE 前缀
            if line.startswith("data:"):
                line = line[5:].strip()
            if line == "[DONE]":
                break
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            c = obj.get("message", {}).get("content")
            if c:
                with LOCK:
                    panels["ollama"].append(c)
                n_tok += 1
            if obj.get("done"):
                break
    elapsed = time.time() - t0
    stats["ollama"] = {"time_s": elapsed, "tokens": n_tok,
                       "speed": round(n_tok / elapsed, 1) if elapsed else 0}


def _disp_w(s):
    """字符串显示宽度 (CJK 全角算 2 列)。"""
    return sum(2 if unicodedata.east_asian_width(c) in ("W", "F") else 1 for c in s)


def _pad(s, width):
    """按显示宽度截断/补齐到 width 列。"""
    out, w = [], 0
    for c in s:
        cw = 2 if unicodedata.east_asian_width(c) in ("W", "F") else 1
        if w + cw > width:
            break
        out.append(c)
        w += cw
    return "".join(out) + " " * (width - w)


def render():
    """左右两列 ANSI 重绘: 左=llama.cpp(青色) 右=Ollama(绿色)。"""
    cols = shutil.get_terminal_size((100, 30)).columns
    inner = max(20, (cols - 3) // 2)          # 每列可用宽度
    with LOCK:
        l = "".join(panels["llama.cpp"])
        r = "".join(panels["ollama"])
    ll, rl = l.splitlines(), r.splitlines()
    n = max(len(ll), len(rl))
    body = []
    for i in range(n):
        left = _pad(ll[i] if i < len(ll) else "", inner)
        right = _pad(rl[i] if i < len(rl) else "", inner)
        body.append(f"│\033[36m{left}\033[0m│\033[32m{right}\033[0m│")
    bar = "─" * inner
    out = ["\033[2J\033[H",
           f"┌{bar}┬{bar}┐",
           f"│\033[36m{' llama.cpp':<{inner}}\033[0m│\033[32m{' Ollama':<{inner}}\033[0m│",
           f"├{bar}┼{bar}┤"]
    out += body[-20:]
    out.append(f"└{bar}┴{bar}┘")
    st = []
    for name in ("llama.cpp", "ollama"):
        s = stats.get(name)
        if s:
            st.append(f"{name}: {s['time_s']:.1f}s {s['tokens']}tok {s['speed']}tok/s")
        elif finished[name]:
            st.append(f"{name}: 完成")
        else:
            st.append(f"{name}: 生成中...")
    out.append("  " + "  |  ".join(st))
    out.append("  (输入 /quit 退出 | /clear 清空)")
    sys.stdout.write("\n".join(out))
    sys.stdout.flush()


def main():
    is_tty = sys.stdout.isatty()
    print("=" * 60)
    print("  双模型流式对比终端 (无思考模式)")
    print("  [1] llama.cpp :8080  [2] Ollama :11434 (think:false)")
    print("=" * 60)

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
            for k in histories:
                histories[k].clear()
            print("✅ 已清空")
            continue
        if q.startswith("/mt"):
            try:
                global max_tokens
                max_tokens = int(q.split()[1])
                print(f"✅ max_tokens = {max_tokens}")
            except (IndexError, ValueError):
                print(f"用法: /mt <n> (当前 {max_tokens})")
            continue

        for k in panels:
            panels[k].clear()
        for k in finished:
            finished[k] = False
        stats.clear()
        for k in histories:
            histories[k].append({"role": "user", "content": q})

        def worker(name, fn, msgs):
            try:
                fn(msgs, max_tokens)
            except Exception as e:
                with LOCK:
                    panels[name].append(f"\n[错误] {e}")
            finally:
                finished[name] = True

        threads = [
            threading.Thread(target=worker,
                             args=("llama.cpp", stream_llama, list(histories["llama.cpp"]))),
            threading.Thread(target=worker,
                             args=("ollama", stream_ollama, list(histories["ollama"]))),
        ]
        for t in threads:
            t.start()

        while not all(finished[k] for k in finished):
            if is_tty:
                render()
            time.sleep(0.25)
        for t in threads:
            t.join()

        if is_tty:
            render()
            print()
        with LOCK:
            for k in panels:
                full = "".join(panels[k])
                if full.strip():
                    histories[k].append({"role": "assistant", "content": full})


if __name__ == "__main__":
    main()

