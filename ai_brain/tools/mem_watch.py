#!/usr/bin/env python3
"""
对话过程内存变化实时监控 (Qwen2-VL-2B)
- 采样阶段: 模型加载前 → 加载 → 图片处理 → 推理 → 完成后
- 每0.5秒用tegrastats采样系统内存
用法: python3 ~/mem_watch.py [图片路径]
"""
import re
import subprocess
import sys
import threading
import time

import torch
from PIL import Image
from transformers import Qwen2VLForConditionalGeneration, AutoProcessor

sys.stdout.reconfigure(line_buffering=True)
MODEL_DIR = "/home/rock/qwen2_vl_2b"
IMG = sys.argv[1] if len(sys.argv) > 1 else "/home/rock/vision_test/robot_scene.png"
QUESTION = "请详细描述这张图片里有什么？机器人是什么样子的？"

# ---------- 内存采样器 (tegrastats 后台线程) ----------
samples = []          # (时间秒, RAM MB)
lock = threading.Lock()
stop_flag = threading.Event()

def sampler():
    t0 = time.time()
    while not stop_flag.is_set():
        try:
            out = subprocess.run(["timeout", "1", "/usr/bin/tegrastats", "--interval", "500"],
                                 capture_output=True, text=True, timeout=2).stdout
            line = [l for l in out.strip().split("\n") if l.strip()][-1]
            m = re.search(r"RAM (\d+)/(\d+)MB", line)
            if m:
                with lock:
                    samples.append((round(time.time()-t0, 1), int(m.group(1))))
        except Exception:
            pass
        time.sleep(0.4)

def mem_usage():
    """返回当前时刻之前的平均内存"""
    with lock:
        return samples[-1][1] if samples else None

# ---------- 主流程 ----------
print("=" * 62)
print("  📊 Qwen2-VL-2B 对话过程内存监控")
print("=" * 62)

# 启动采样器
samples.append((0, None))  # 占位
t = threading.Thread(target=sampler, daemon=True)
t.start()
time.sleep(3.0)  # 等采样器取到基线
base = mem_usage()
while base is None:
    time.sleep(0.5)
    base = mem_usage()
print(f"\n  阶段0 [系统基线, 模型未加载]: {base}MB")

# 阶段1: 加载模型
print(f"  阶段1 [加载模型中...]")
t1 = time.time()
model = Qwen2VLForConditionalGeneration.from_pretrained(
    MODEL_DIR, torch_dtype=torch.float16, device_map="cuda:0")
processor = AutoProcessor.from_pretrained(MODEL_DIR)
model.eval()
load_s = mem_usage()
print(f"        加载完成 ({time.time()-t1:.1f}s), 内存: {load_s}MB (+{load_s-base}MB)")

# 阶段2: 图片预处理
image = Image.open(IMG)
print(f"  阶段2 [图片处理中...] {IMG}")
messages = [{"role": "user", "content": [
    {"type": "image"}, {"type": "text", "text": QUESTION}]}]
text = processor.apply_chat_template(messages, tokenize=False, add_generation_prompt=True)
inputs = processor(text=[text], images=[image], return_tensors="pt").to("cuda")
pre_s = mem_usage()
print(f"        处理完成, 输入token: {inputs.input_ids.shape[1]}, 内存: {pre_s}MB")

# 阶段3: 推理生成
print(f"  阶段3 [推理生成中...]")
t2 = time.time()
gen = model.generate(**inputs, max_new_tokens=300, do_sample=False, use_cache=True,
                     pad_token_id=processor.tokenizer.pad_token_id)
infer_s = mem_usage()
dt = time.time() - t2
print(f"        生成完成 ({dt:.1f}s), 内存: {infer_s}MB (+{infer_s-pre_s}MB)")

# 阶段4: 完成后保持
time.sleep(2)
after_s = mem_usage()
answer = processor.batch_decode(gen[:, inputs.input_ids.shape[1]:],
                                skip_special_tokens=True)[0]
print(f"  阶段4 [完成后2秒]: {after_s}MB")

stop_flag.set()
print(f"\n  💬 回答: {answer[:300]}")
print(f"\n  峰值显存(torch): {torch.cuda.max_memory_allocated()//(1024*1024)}MB")

# ---------- 输出时间线 ----------
print("\n" + "=" * 62)
print("  📈 内存变化时间线 (每0.5秒采样)")
print("=" * 62)
print(f"  {'时间(s)':<10}{'内存(MB)':<12}{'变化'}")
with lock:
    snap = list(samples[1:])
for ts, mb in snap:
    if mb is None:
        continue
    delta = f"+{mb-base}" if mb >= base else f"-{base-mb}"
    marker = " █" if ts >= 1.5 and mb > base + 10 else ""
    print(f"  {ts:<10}{mb:<12}{delta}{marker}")
print(f"\n  📌 总结: 基线{base}MB → 加载后{load_s}MB → 推理峰值{infer_s}MB")
print(f"    模型权重占 {load_s-base}MB, 推理KV增长 {infer_s-pre_s}MB")
