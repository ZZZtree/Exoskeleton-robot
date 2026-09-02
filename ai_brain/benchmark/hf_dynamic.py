#!/usr/bin/env python3
"""
HuggingFace 动态 KV 分配演示 (Qwen2-1.5B-Instruct)
测量不同生成长度下的峰值内存, 展示 KV cache 动态增长
用法: python3 ~/hf_dynamic.py
"""
import sys
import time
import torch
from transformers import AutoModelForCausalLM, AutoTokenizer

sys.stdout.reconfigure(line_buffering=True)
MODEL_DIR = "/home/rock/qwen2_1_5b"

print(f"🚀 加载 Qwen2-1.5B-Instruct (fp16)")
t0 = time.time()
model = AutoModelForCausalLM.from_pretrained(
    MODEL_DIR, torch_dtype=torch.float16, device_map="cuda:0")
tok = AutoTokenizer.from_pretrained(MODEL_DIR)
model.eval()
print(f"  模型加载完成 ({time.time()-t0:.1f}s)")
print(f"  加载后显存(仅权重): {torch.cuda.memory_allocated()//(1024*1024)}MB\n")

prompt = "请用一段话介绍人工智能的发展历史。"
inputs = tok(prompt, return_tensors="pt").to("cuda")
print(f"  提示词token数: {inputs.input_ids.shape[1]}")
print()

print("=" * 62)
print("  📊 动态 KV 分配演示 (峰值内存 = 权重 + 动态增长的KV)")
print("=" * 62)
print(f"  {'生成长度':<12}{'峰值显存MB':<12}{'推理时间s'}")
results = [(0, torch.cuda.memory_allocated() // (1024 * 1024))]
print(f"  {'0':<12}{results[0][1]:<12}{'-':<8}  (仅权重)")
for n in [100, 250, 500, 800, 1200]:
    torch.cuda.reset_peak_memory_stats()
    torch.cuda.synchronize()
    t1 = time.time()
    gen = model.generate(**inputs, max_new_tokens=n, use_cache=True,
                         do_sample=False, pad_token_id=tok.eos_token_id)
    torch.cuda.synchronize()
    dt = time.time() - t1
    peak = torch.cuda.max_memory_allocated() // (1024 * 1024)
    results.append((n, peak))
    print(f"  {n:<12}{peak:<12}{dt:<8.1f}")
    if n > 0:
        text = tok.decode(gen[0][inputs.input_ids.shape[1]:], skip_special_tokens=True)
        print(f"    回答预览: {text[:55]}...")
    print()

print("=" * 62)
print("  📈 分析")
base = results[0][1]
for n, peak in results[1:]:
    print(f"  生成 {n:>5} tokens → 峰值 {peak:>4}MB (KV增长 {peak-base:>3}MB)")
print(f"\n  ⚡ 对比 Ollama qwen2.5vl:3b (32K预分配): 常驻 12.3GB 恒定不变")
print(f"  ⚡ 对比 HF Qwen2-1.5B: 权重 {base}MB 起, KV 随使用动态增长")
print(f"\n  💡 结论: 短对话 HF 省内存; HF 不为用不到的上下文预留空间")
