#!/usr/bin/env python3
"""
交互式对话 (Qwen2-VL-2B) — 支持多轮记忆 + 发图
用法: python3 ~/chat.py
命令:
  /image <路径>   附带一张图片再提问
  /clear          清空对话历史
  /exit 或 Ctrl+D 退出
"""
import os
import sys
import time
import torch
from PIL import Image
from transformers import Qwen2VLForConditionalGeneration, AutoProcessor

sys.stdout.reconfigure(line_buffering=True)
MODEL_DIR = "/home/rock/qwen2_vl_2b"

def main():
    print("加载 Qwen2-VL-2B 模型...")
    t0 = time.time()
    model = Qwen2VLForConditionalGeneration.from_pretrained(
        MODEL_DIR, torch_dtype=torch.float16, device_map="cuda:0")
    processor = AutoProcessor.from_pretrained(MODEL_DIR)
    model.eval()
    print(f"✅ 模型就绪 ({time.time()-t0:.1f}s), 开始对话吧!\n")
    print("=" * 50)
    print("  💬 Qwen2-VL-2B 交互对话")
    print("  命令: /image <路径> | /clear | /exit")
    print("=" * 50)

    messages = []   # 对话历史
    images = []     # 所有用过的图片 (按顺序)

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
        if q == "/clear":
            messages = []
            images = []
            print("[已清空对话历史]")
            continue
        if q.startswith("/image"):
            parts = q.split(None, 1)
            if len(parts) < 2:
                print("[用法: /image 图片路径]")
                continue
            path = parts[1].strip()
            if not os.path.exists(path):
                print(f"[图片不存在: {path}]")
                continue
            messages.append({"role": "user", "content": [
                {"type": "image"},
                {"type": "text", "text": "请描述这张图片。"}]})
            images.append(Image.open(path))
            print(f"[已添加图片: {path}, 等待你输入问题...]")
            continue

        # 普通文字轮 (若刚发过图, 补充问题到该轮)
        if messages and messages[-1]["role"] == "user" and \
           isinstance(messages[-1]["content"], list) and \
           messages[-1]["content"][0]["type"] == "image":
            messages[-1]["content"][1]["text"] = q
        else:
            messages.append({"role": "user", "content": q})

        # 构建输入
        text = processor.apply_chat_template(messages, tokenize=False,
                                             add_generation_prompt=True)
        try:
            if images:
                inputs = processor(text=[text], images=list(images),
                                   return_tensors="pt").to("cuda")
            else:
                inputs = processor(text=[text], return_tensors="pt").to("cuda")
        except Exception as e:
            print(f"[输入处理失败: {e}]")
            break

        # 生成 (流式打印)
        print("\nQwen> ", end="", flush=True)
        t_start = time.time()
        gen = model.generate(**inputs, max_new_tokens=300, do_sample=False,
                             use_cache=True,
                             pad_token_id=processor.tokenizer.pad_token_id)
        answer = processor.batch_decode(
            gen[:, inputs.input_ids.shape[1]:], skip_special_tokens=True)[0]
        print(answer, flush=True)
        dt = time.time() - t_start
        n = gen.shape[1] - inputs.input_ids.shape[1]
        print(f"  [⏱ {dt:.1f}s | {n} tokens | {n/dt:.1f} tok/s]")

        messages.append({"role": "assistant", "content": answer})

if __name__ == "__main__":
    main()
