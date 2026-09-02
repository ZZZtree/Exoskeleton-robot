#!/usr/bin/env python3
"""
验证: 30轮对话后, 上下文真实内存开销 (排除页缓存)
测量: 模型权重 vs 完整30轮上下文(1张图+长文字) 的torch精确内存
"""
import subprocess
import sys
import time
import torch
from PIL import Image
from transformers import Qwen2VLForConditionalGeneration, AutoProcessor

sys.stdout.reconfigure(line_buffering=True)
MODEL_DIR = "/home/rock/qwen2_vl_2b"

def mb():
    torch.cuda.synchronize()
    return torch.cuda.memory_allocated() // (1024 * 1024)

# 清理页缓存, 获取真实可用内存
subprocess.run(["bash", "-c", "echo 'rock' | sudo -S sh -c 'sync; echo 3 > /proc/sys/vm/drop_caches'"],
               capture_output=True, timeout=10)
import re
def avail():
    with open("/proc/meminfo") as f:
        for line in f:
            if line.startswith("MemAvailable:"):
                return int(line.split()[1]) // 1024  # kB → MB
    return None

a0 = avail()
print(f"清理缓存后系统可用内存: {a0}MB")

print("加载模型...")
t0 = time.time()
model = Qwen2VLForConditionalGeneration.from_pretrained(
    MODEL_DIR, torch_dtype=torch.float16, device_map="cuda:0")
processor = AutoProcessor.from_pretrained(MODEL_DIR)
model.eval()
w = mb()
a1 = avail()
print(f"  ① 权重: {w}MB | 系统可用: {a1}MB (减少 {a0-a1}MB)\n")

# 重建约30轮对话上下文 (2000+ tokens文字 + 1张图片, 匹配基准第30轮)
history = []
for i in range(25):
    history.append({"role": "user", "content": f"第{i+1}轮问题: 日常生活相关提问内容"})
    history.append({"role": "assistant", "content": f"第{i+1}轮回答: 详细合理的建议回复"})
history.append({"role": "user", "content": [
    {"type": "image"}, {"type": "text", "text": "第26轮: 请描述这张图片"}]})
history.append({"role": "assistant", "content": "图片描述: 这是一个场景图"})
img = Image.open("/home/rock/vision_test/test_scene.png")

text = processor.apply_chat_template(history, tokenize=False, add_generation_prompt=True)
inputs = processor(text=[text], images=[img], return_tensors="pt").to("cuda")
ctx = inputs.input_ids.shape[1]
print(f"上下文: {ctx} tokens + 1图 (接近基准第30轮)")

torch.cuda.reset_peak_memory_stats()
t1 = time.time()
gen = model.generate(**inputs, max_new_tokens=50, do_sample=False, use_cache=True,
                     pad_token_id=processor.tokenizer.pad_token_id)
torch.cuda.synchronize()
peak = torch.cuda.max_memory_allocated() // (1024 * 1024)
a2 = avail()
print(f"  ② 生成峰值: {peak}MB | 系统可用: {a2}MB (再减少 {a1-a2}MB)")
print(f"\n  ✅ 模型+30轮上下文真实峰值: {peak}MB")
print(f"     其中权重: {w}MB, 上下文相关: {peak-w}MB")
