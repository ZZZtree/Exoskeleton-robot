#!/usr/bin/env python3
"""
30轮长对话基准 (Ollama GGUF qwen2.5vl:3b)
- 25轮文字 + 5轮图片 (与HF基准相同问题, 可直接对比)
- 每轮记录: TTFT, 速度, 上下文, 系统内存
- 结果保存到 ~/benchmark_ollama.json
"""
import base64
import http.client
import json
import os
import re
import socket
import subprocess
import sys
import time

sys.stdout.reconfigure(line_buffering=True)
BASE = "http://127.0.0.1:11434"
BASE_HOST = "127.0.0.1"
BASE_PORT = 11434
MODEL = "qwen2.5vl:3b"
OUT_FILE = os.path.expanduser("~/benchmark_ollama.json")
IMG_DIR = "/home/rock/vision_test"

TEXT_QUESTIONS = [
    "我最近总是睡不好觉，有什么建议吗？",
    "我每天晚上都刷手机到很晚，这样影响大吗？",
    "那睡前应该做点什么来放松呢？",
    "我试过喝热牛奶，好像没什么用……",
    "那我白天喝咖啡会有影响吗？",
    "我大概几点以后就不该喝咖啡了？",
    "除了睡眠，我最近肩膀还一直酸痛……",
    "可能是上班坐太久了吧，有什么缓解办法？",
    "我打算周末开始运动，你有什么推荐？",
    "跑步和游泳哪个更适合我这种新手？",
    "我办了一张健身房的月卡，值吗？",
    "那我第一次去应该怎么安排训练？",
    "我担心自己坚持不下来，怎么保持动力？",
    "对了，运动的时候饮食要注意什么？",
    "早上吃什么比较健康有营养？",
    "我平时经常点外卖，怎么吃得健康一点？",
    "你有简单的家常菜推荐吗？",
    "我想学着做饭，应该先从什么开始？",
    "这周末想约朋友来家里聚会，有什么建议？",
    "那聚会准备点什么吃的比较好？",
    "我朋友下周过生日，送什么礼物合适？",
    "预算200块左右，你有什么推荐？",
    "我想养一只宠物，你觉得猫和狗选哪个？",
    "养猫的话需要注意些什么？",
    "谢谢你今天的建议！帮我总结一下我们今天聊的重点吧。",
]

IMAGE_QUESTIONS = [
    ("test_image.png", "对了，这是我刚拍的一张照片，你能说说里面有什么吗？"),
    ("test_scene.png", "这张是我周末去郊外拍的照片，你觉得场景怎么样？"),
    ("robot_scene.png", "这张图是我朋友发来的，他说里面有个好玩的东西，你看是什么？"),
    ("test_image.png", "我在这张图里藏了一道算术题，你能算出来吗？"),
    ("robot_scene.png", "再看这张，你觉得这个机器人可爱吗？它在什么环境里？"),
]

def system_mem_mb():
    try:
        out = subprocess.run(["timeout", "2", "/usr/bin/tegrastats", "--interval", "1000"],
                             capture_output=True, text=True, timeout=4).stdout
        line = [l for l in out.strip().split("\n") if l.strip()][-1]
        m = re.search(r"RAM (\d+)/(\d+)MB", line)
        return int(m.group(1)) if m else None
    except Exception:
        return None

def chat_turn(messages, image_path=None):
    """调用Ollama流式API (大socket缓冲区, 防止背压卡死)"""
    payload = {"model": MODEL, "messages": messages, "stream": True}
    if image_path:
        b64 = base64.b64encode(
            open(os.path.join(IMG_DIR, image_path), "rb").read()).decode()
        payload["messages"][-1]["images"] = [b64]

    body = json.dumps(payload)
    # 建立连接并调大接收缓冲区 (关键: 防止服务端背压阻塞)
    conn = http.client.HTTPConnection(BASE_HOST, BASE_PORT, timeout=180)
    t0 = time.time()  # 从发送请求开始计时, TTFT包含预填充
    conn.connect()
    try:
        sock = conn.sock
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 * 1024 * 1024)
    except Exception:
        pass
    conn.request("POST", "/api/chat", body=body,
                 headers={"Content-Type": "application/json"})
    resp = conn.getresponse()

    first_token_t = None
    content = []
    done_info = {}
    try:
        for raw in resp:
            line = raw.decode().strip()
            if not line.startswith("{"):
                continue
            chunk = json.loads(line)
            if chunk.get("done"):
                done_info = chunk
                break
            msg = chunk.get("message", {})
            if msg.get("content"):
                if first_token_t is None:
                    first_token_t = time.time()
                content.append(msg["content"])
    finally:
        resp.close()
        conn.close()  # 每轮独立连接, 用完即关, 避免脏连接

    elapsed = time.time() - t0
    ttft_ms = (first_token_t - t0) * 1000 if first_token_t else elapsed * 1000
    ctx = done_info.get("prompt_eval_count", 0)
    n_tokens = done_info.get("eval_count", 0)
    speed = n_tokens / elapsed if elapsed > 0 else 0
    mem = system_mem_mb()
    return ctx, ttft_ms, elapsed, speed, mem, "".join(content)

def main():
    print("=" * 62)
    print(f"  30轮长对话基准 (Ollama {MODEL})")
    print("=" * 62)
    print(f"  当前上下文配置: {os.environ.get('OLLAMA_CONTEXT_LENGTH', '8192')}")

    history = []  # Ollama消息历史
    results = []
    baseline_mem = system_mem_mb()

    print(f"\n{'轮次':<5}{'类型':<6}{'上下文':<9}{'首字ms':<9}{'耗时s':<8}{'tok/s':<8}{'系统内存MB'}")
    print("-" * 66)

    for i, q in enumerate(TEXT_QUESTIONS, 1):
        history.append({"role": "user", "content": q})
        ctx, ttft, elapsed, speed, mem, ans = chat_turn(history)
        results.append({"turn": i, "type": "text", "question": q,
                        "context_tokens": ctx, "ttft_ms": round(ttft),
                        "total_time_s": round(elapsed, 2),
                        "tokens_per_s": round(speed, 2),
                        "system_mem_mb": mem})
        print(f"  {i:<5}{'文字':<6}{ctx:<9}{ttft:<9.0f}{elapsed:<8.1f}{speed:<8.1f}{mem}")
        history.append({"role": "assistant", "content": ans})

    for j, (img_file, q) in enumerate(IMAGE_QUESTIONS, 26):
        history.append({"role": "user", "content": q})
        ctx, ttft, elapsed, speed, mem, ans = chat_turn(history, img_file)
        results.append({"turn": j, "type": "image", "image": img_file,
                        "question": q, "context_tokens": ctx,
                        "ttft_ms": round(ttft), "total_time_s": round(elapsed, 2),
                        "tokens_per_s": round(speed, 2), "system_mem_mb": mem})
        print(f"  {j:<5}{'图片':<6}{ctx:<9}{ttft:<9.0f}{elapsed:<8.1f}{speed:<8.1f}{mem}")
        history.append({"role": "assistant", "content": ans})

    avg_speed = sum(r["tokens_per_s"] for r in results) / len(results)
    summary = {
        "model": f"{MODEL} (Ollama GGUF Q4)",
        "date": time.strftime("%Y-%m-%d %H:%M:%S"),
        "turns": len(results),
        "avg_speed_tok_per_s": round(avg_speed, 2),
        "baseline_system_mem_mb": baseline_mem,
        "final_context_tokens": results[-1]["context_tokens"],
        "per_turn": results,
    }
    with open(OUT_FILE, "w") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)

    print("\n" + "=" * 62)
    print("  📈 Ollama 基准汇总")
    print(f"  平均速度: {avg_speed:.1f} tokens/s (30轮)")
    print(f"  基线内存: {baseline_mem}MB → 末尾: {results[-1]['system_mem_mb']}MB")
    print(f"  最终上下文: {results[-1]['context_tokens']} tokens")
    print(f"\n  ✅ 已保存: {OUT_FILE}")
    print("  (对比HF基准: ~/benchmark_baseline.json)")

if __name__ == "__main__":
    main()
