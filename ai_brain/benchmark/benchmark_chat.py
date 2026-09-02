#!/usr/bin/env python3
"""
长对话基准测试 v2 (Qwen2-VL-2B)
- 30轮多轮对话 (25轮文字 + 5轮图片)
- 每轮记录: 首字延迟(TTFT), 生成速度, 上下文长度, 系统内存
- 结果保存到 ~/benchmark_baseline.json 供后续调优对比
用法: python3 ~/benchmark_chat.py
"""
import json
import os
import re
import subprocess
import sys
import time

import torch
from PIL import Image
from transformers import Qwen2VLForConditionalGeneration, AutoProcessor
from transformers.generation.streamers import BaseStreamer

sys.stdout.reconfigure(line_buffering=True)
MODEL_DIR = "/home/rock/qwen2_vl_2b"
OUT_FILE = os.path.expanduser("~/benchmark_baseline.json")
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


class TTFTStreamer(BaseStreamer):
    def __init__(self):
        self.start_time = time.time()
        self.first_token_time = None
        self.put_count = 0
        super().__init__()
    def put(self, value):
        self.put_count += 1
        if self.put_count == 1:
            return  # 第一次put是提示词, 跳过
        if self.first_token_time is None:
            self.first_token_time = time.time()
    def end(self):
        pass

def system_mem_mb():
    try:
        out = subprocess.run(["timeout", "2", "/usr/bin/tegrastats", "--interval", "1000"],
                             capture_output=True, text=True, timeout=4).stdout
        line = [l for l in out.strip().split("\n") if l.strip()][-1]
        m = re.search(r"RAM (\d+)/(\d+)MB", line)
        return int(m.group(1)) if m else None
    except Exception:
        return None

def main():
    print("=" * 62)
    print("  📋 长对话基准测试 v2 (Qwen2-VL-2B, 30轮日常生活)")
    print("=" * 62)

    print("\n[加载模型...]")
    t0 = time.time()
    model = Qwen2VLForConditionalGeneration.from_pretrained(
        MODEL_DIR, torch_dtype=torch.float16, device_map="cuda:0")
    processor = AutoProcessor.from_pretrained(MODEL_DIR)
    model.eval()
    load_time = time.time() - t0
    load_mem = torch.cuda.memory_allocated() // (1024 * 1024)
    print(f"  加载耗时: {load_time:.1f}s | 权重显存: {load_mem}MB")

    history = []
    results = []
    baseline_mem = system_mem_mb()

    print(f"\n{'轮次':<5}{'类型':<6}{'上下文':<9}{'首字ms':<9}{'耗时s':<8}{'tok/s':<8}{'系统内存MB'}")
    print("-" * 66)

    def run_turn(question, image_path=None):
        if image_path:
            img = Image.open(os.path.join(IMG_DIR, image_path))
            msg = {"role": "user", "content": [
                {"type": "image"}, {"type": "text", "text": question}]}
            messages = list(history) + [msg]
            text = processor.apply_chat_template(messages, tokenize=False,
                                                 add_generation_prompt=True)
            inputs = processor(text=[text], images=[img], return_tensors="pt").to("cuda")
        else:
            messages = list(history) + [{"role": "user", "content": question}]
            text = processor.apply_chat_template(messages, tokenize=False,
                                                 add_generation_prompt=True)
            inputs = processor(text=[text], return_tensors="pt").to("cuda")
        ctx_len = inputs.input_ids.shape[1]
        streamer = TTFTStreamer()
        t_start = time.time()
        gen = model.generate(**inputs, max_new_tokens=100, do_sample=False,
                             use_cache=True, streamer=streamer,
                             pad_token_id=processor.tokenizer.pad_token_id)
        elapsed = time.time() - t_start
        ttft_ms = (streamer.first_token_time - streamer.start_time) * 1000
        n_tokens = gen.shape[1] - ctx_len  # 从生成张量精确统计
        speed = n_tokens / elapsed if elapsed > 0 else 0
        mem_now = system_mem_mb()
        answer = processor.batch_decode(gen[:, ctx_len:],
                                        skip_special_tokens=True)[0]
        return ctx_len, ttft_ms, elapsed, speed, mem_now, answer

    for i, q in enumerate(TEXT_QUESTIONS, 1):
        ctx, ttft, elapsed, speed, mem, ans = run_turn(q)
        results.append({"turn": i, "type": "text", "question": q,
                        "context_tokens": ctx, "ttft_ms": round(ttft),
                        "total_time_s": round(elapsed, 2),
                        "tokens_per_s": round(speed, 2),
                        "system_mem_mb": mem})
        print(f"  {i:<5}{'文字':<6}{ctx:<9}{ttft:<9.0f}{elapsed:<8.1f}{speed:<8.1f}{mem}")
        history.append({"role": "user", "content": q})
        history.append({"role": "assistant", "content": ans})

    for j, (img_file, q) in enumerate(IMAGE_QUESTIONS, 26):
        ctx, ttft, elapsed, speed, mem, ans = run_turn(q, img_file)
        results.append({"turn": j, "type": "image", "image": img_file,
                        "question": q, "context_tokens": ctx,
                        "ttft_ms": round(ttft), "total_time_s": round(elapsed, 2),
                        "tokens_per_s": round(speed, 2), "system_mem_mb": mem})
        print(f"  {j:<5}{'图片':<6}{ctx:<9}{ttft:<9.0f}{elapsed:<8.1f}{speed:<8.1f}{mem}")
        history.append({"role": "user", "content": f"[图片] {q}"})
        history.append({"role": "assistant", "content": ans})



    peak_mem = torch.cuda.max_memory_allocated() // (1024 * 1024)
    avg_speed = sum(r["tokens_per_s"] for r in results) / len(results)
    summary = {
        "model": "Qwen2-VL-2B-Instruct (HF fp16)",
        "date": time.strftime("%Y-%m-%d %H:%M:%S"),
        "load_time_s": round(load_time, 1),
        "weight_mem_mb": load_mem,
        "peak_mem_mb": peak_mem,
        "baseline_system_mem_mb": baseline_mem,
        "turns": len(results),
        "avg_speed_tok_per_s": round(avg_speed, 2),
        "per_turn": results,
    }
    with open(OUT_FILE, "w") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2)

    print("\n" + "=" * 62)
    print("  📈 基准汇总")
    print(f"  加载耗时: {load_time:.1f}s | 权重显存: {load_mem}MB | 峰值: {peak_mem}MB")
    print(f"  平均速度: {avg_speed:.1f} tokens/s (30轮)")
    print(f"  基线系统内存: {baseline_mem}MB → 末尾: {results[-1]['system_mem_mb']}MB")
    print(f"  最终上下文: {results[-1]['context_tokens']} tokens")
    print(f"\n  ✅ 基准数据已保存: {OUT_FILE}")
    print("  (后续调优后重跑本脚本即可对比)")

if __name__ == "__main__":
    main()
