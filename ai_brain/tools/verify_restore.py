#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""决定性验证: llama.cpp 内置 restore 是否真能加速 prefill (KV 磁盘换回)"""
import json, time, urllib.request

BASE = "http://localhost:8080"
LLAMA = BASE + "/v1/chat/completions"

# 长前缀 (几百 token, 模拟多轮长对话积累的上下文)
SYSTEM = "你是家庭智能助手，服务于alice/bob/carol一家。请用中文简洁回答。" * 3
HIST = ""
for i in range(12):
    HIST += f"{i}. 用户问了一些问题，助手给出了相应的回答。家庭成员讨论了买菜、运动、学习等话题。"
MSG = HIST + "最后用户问：明天早餐吃什么？"

def clear_slots():
    for i in range(4):
        try:
            req = urllib.request.Request(BASE + f"/slots/{i}?action=erase", data=b"{}",
                headers={"Content-Type":"application/json"}, method="POST")
            urllib.request.urlopen(req, timeout=10).read()
        except Exception:
            pass
    time.sleep(1)

def ask(id_slot=0):
    payload = {"messages":[{"role":"system","content":SYSTEM},{"role":"user","content":MSG}],
               "max_tokens":30, "id_slot":id_slot}
    req = urllib.request.Request(LLAMA, data=json.dumps(payload).encode(),
                                 headers={"Content-Type":"application/json"})
    d = json.loads(urllib.request.urlopen(req, timeout=120).read())
    t = d["timings"]
    u = d["usage"]
    return (t.get("prompt_per_second",0), t.get("prompt_ms",0), u["prompt_tokens"],
            u["prompt_tokens_details"].get("cached_tokens",0))

def slot_save(fn):
    req = urllib.request.Request(BASE + "/slots/0?action=save",
        data=json.dumps({"filename":fn}).encode(), headers={"Content-Type":"application/json"}, method="POST")
    return json.loads(urllib.request.urlopen(req, timeout=30).read())

def slot_restore(fn):
    req = urllib.request.Request(BASE + "/slots/0?action=restore",
        data=json.dumps({"filename":fn}).encode(), headers={"Content-Type":"application/json"}, method="POST")
    return json.loads(urllib.request.urlopen(req, timeout=30).read())

print("="*60)
print("  验证: 磁盘KV restore 能否加速 prefill")
print(f"  前缀长度: {len(SYSTEM+MSG)} 字符 (~{(len(SYSTEM+MSG))//2} tokens)")
print("="*60)

# 1. 清空 + 冷启动
clear_slots()
pps1, ms1, p1, c1 = ask(0)
print(f"\n[1] 冷启动(磁盘无KV):   prefill={pps1:6.0f} t/s ({ms1:6.0f}ms) cached={c1} prompt={p1}")

# 2. 保存当前 slot0 KV 到磁盘
r = slot_save("verify.bin")
print(f"[2] 保存KV到磁盘:      n_saved={r.get('n_saved')} tokens, {r.get('n_written',0)//1024//1024}MB, {r.get('timings',{}).get('save_ms',0):.0f}ms")

# 3. 清空所有 slot (内存缓存也清掉)
clear_slots()
print("[3] 清空全部 slot (内存KV清零)")

# 4. 从磁盘 restore 到 slot0
r2 = slot_restore("verify.bin")
print(f"[4] 从磁盘restore:    n_restored={r2.get('n_restored')} tokens, {r2.get('n_read',0)//1024//1024}MB, {r2.get('timings',{}).get('restore_ms',0):.0f}ms")

# 5. 发完全相同请求, 测 prefill
pps2, ms2, p2, c2 = ask(0)
print(f"[5] restore后同请求:   prefill={pps2:6.0f} t/s ({ms2:6.0f}ms) cached={c2} prompt={p2}")

print("\n" + "="*60)
ratio = pps2/pps1 if pps1 else 0
if pps2 > pps1 * 1.5:
    print(f"  ✅ restore 有效! prefill 加速 {ratio:.1f}x ({pps1:.0f} → {pps2:.0f} t/s)")
else:
    print(f"  ❌ restore 未加速 (ratio={ratio:.1f}x), KV 未能用于前缀")
print("="*60)
