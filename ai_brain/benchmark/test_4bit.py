#!/usr/bin/env python3
"""测试 bitsandbytes 4bit 量化加载 Qwen2-VL-2B, 测量内存节省"""
import sys
import time
import torch
from transformers import Qwen2VLForConditionalGeneration, AutoProcessor, BitsAndBytesConfig

sys.stdout.reconfigure(line_buffering=True)
MODEL_DIR = "/home/rock/qwen2_vl_2b"

def mb():
    torch.cuda.synchronize()
    return torch.cuda.memory_allocated() // (1024 * 1024)

print("以 4bit (NF4) 量化加载 Qwen2-VL-2B...")
t0 = time.time()
qcfg = BitsAndBytesConfig(load_in_4bit=True,
                          bnb_4bit_quant_type="nf4",
                          bnb_4bit_compute_dtype=torch.float16)
try:
    model = Qwen2VLForConditionalGeneration.from_pretrained(
        MODEL_DIR, quantization_config=qcfg, device_map="cuda:0")
    processor = AutoProcessor.from_pretrained(MODEL_DIR)
    model.eval()
    w = mb()
    print(f"  加载耗时: {time.time()-t0:.1f}s")
    print(f"  4bit 权重显存: {w}MB")
    print(f"  (对比 fp16: 4215MB)")
    print(f"  💾 节省: {4215-w}MB ({(4215-w)/4215*100:.0f}%)")
except Exception as e:
    print(f"  ❌ 4bit 加载失败: {type(e).__name__}: {e}")
    print("  (Jetson aarch64 上 bitsandbytes 内核可能不兼容)")
    sys.exit(1)

# 测试推理是否正常
print("\n测试推理...")
from PIL import Image
img = Image.open("/home/rock/vision_test/test_scene.png")
messages = [{"role": "user", "content": [
    {"type": "image"}, {"type": "text", "text": "这张图里有什么？"}]}]
text = processor.apply_chat_template(messages, tokenize=False, add_generation_prompt=True)
inputs = processor(text=[text], images=[img], return_tensors="pt").to("cuda")
torch.cuda.reset_peak_memory_stats()
t1 = time.time()
gen = model.generate(**inputs, max_new_tokens=50, do_sample=False, use_cache=True,
                     pad_token_id=processor.tokenizer.pad_token_id)
torch.cuda.synchronize()
peak = torch.cuda.max_memory_allocated() // (1024 * 1024)
ans = processor.batch_decode(gen[:, inputs.input_ids.shape[1]:],
                             skip_special_tokens=True)[0]
print(f"  生成耗时: {time.time()-t1:.1f}s")
print(f"  峰值显存: {peak}MB")
print(f"  回答: {ans[:100]}")
print(f"\n  ✅ 4bit 量化可用! 权重 {4215}MB → {w}MB, 节省 {4215-w}MB")
