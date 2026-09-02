#!/usr/bin/env python3
"""
精确测量 Qwen2-VL-2B 的模型+上下文内存开销
分开测量: ①权重 ②上下文KV ③生成峰值
"""
import os
import sys
import time
import torch
from PIL import Image
from transformers import Qwen2VLForConditionalGeneration, AutoProcessor

sys.stdout.reconfigure(line_buffering=True)
MODEL_DIR = "/home/rock/qwen2_vl_2b"
IMG_DIR = "/home/rock/vision_test"

def mb():
    torch.cuda.synchronize()
    return torch.cuda.memory_allocated() // (1024 * 1024)

print("加载模型...")
t0 = time.time()
model = Qwen2VLForConditionalGeneration.from_pretrained(
    MODEL_DIR, torch_dtype=torch.float16, device_map="cuda:0")
processor = AutoProcessor.from_pretrained(MODEL_DIR)
model.eval()
mem_weights = mb()
print(f"  ① 模型权重: {mem_weights}MB ({time.time()-t0:.1f}s)\n")

# 重建接近基准末尾的上下文 (5张图片 + 约2000tokens文字对话)
history_text = []
for i in range(30):
    history_text.append({"role": "user",
                         "content": f"第{i+1}轮的用户提问，关于日常生活的话题内容。"})
    history_text.append({"role": "assistant",
                         "content": f"第{i+1}轮的模型回答，包含合理的建议和详细的说明。"})
images = [Image.open(os.path.join(IMG_DIR, f)) for f in
          ["test_image.png", "test_scene.png", "robot_scene.png"]]

# 图片轮消息
for i, img in enumerate(images):
    history_text.append({"role": "user", "content": [
        {"type": "image"},
        {"type": "text", "text": f"第{i+26}轮: 请描述这张图片。"}]})
    history_text.append({"role": "assistant", "content": "这是一张测试图片的回答。"})

# 构建输入
text = processor.apply_chat_template(history_text, tokenize=False,
                                     add_generation_prompt=True)
inputs = processor(text=[text], images=images, return_tensors="pt").to("cuda")
ctx_len = inputs.input_ids.shape[1]
mem_prefill_input = mb()
print(f"  上下文: {ctx_len} tokens + {len(images)}张图片")
print(f"  ② 预处理后(含图片特征,未算KV): {mem_prefill_input}MB"
      f" (+{mem_prefill_input-mem_weights}MB 图片特征)\n")

# 前向传播(预填充) → 创建上下文KV
t1 = time.time()
with torch.no_grad():
    model(**inputs)
torch.cuda.synchronize()
mem_after_prefill = mb()
kv_context = mem_after_prefill - mem_prefill_input
print(f"  预填充耗时: {time.time()-t1:.1f}s")
print(f"  ③ 预填充后(权重+图片+上下文KV): {mem_after_prefill}MB")
print(f"     上下文KV缓存: {kv_context}MB (+{kv_context}MB)\n")

# 生成 → 峰值
torch.cuda.reset_peak_memory_stats()
t2 = time.time()
gen = model.generate(**inputs, max_new_tokens=100, do_sample=False,
                     use_cache=True,
                     pad_token_id=processor.tokenizer.pad_token_id)
torch.cuda.synchronize()
peak = torch.cuda.max_memory_allocated() // (1024 * 1024)
gen_time = time.time() - t2
print(f"  ④ 生成峰值内存: {peak}MB (权重+上下文KV+生成KV)")
print(f"     生成耗时: {gen_time:.1f}s, 生成 {gen.shape[1]-ctx_len} tokens")
print(f"     生成KV增长: {peak-mem_after_prefill}MB\n")

print("=" * 56)
print("  📊 模型+上下文总开销 (torch精确统计)")
print("=" * 56)
print(f"  模型权重(固定):        {mem_weights}MB")
print(f"  图片特征(5图):        +{mem_prefill_input-mem_weights}MB")
print(f"  上下文KV({ctx_len}tok): +{kv_context}MB")
print(f"  生成KV(100tok):       +{peak-mem_after_prefill}MB")
print(f"  ─────────────────────────")
print(f"  合计峰值:              {peak}MB")
print(f"  其中上下文相关动态开销: {peak-mem_weights}MB")
