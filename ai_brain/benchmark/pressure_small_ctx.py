#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""小上下文(1000)超限压测: 观察 KV 裁剪 + 缓存命中率变化 + 记忆保持"""
import json, time, urllib.request

LLAMA = "http://localhost:8080/v1/chat/completions"
SYSTEM = ("你是家庭智能助手。"
          "家庭成员：alice(妈妈,喜欢烹饪)、bob(爸爸,喜欢运动)、carol(女儿,12岁)。"
          "回答要求：用中文、简洁。")
QS = [
    "今天晚餐做什么？", "女儿不爱吃蔬菜怎么办？", "周末郊游带什么？",
    "晚上跑步还是瑜伽？", "孩子写作业坐姿不好？", "睡前读什么书好？",
    "天气转凉注意什么？", "早餐怎么搭配？", "水果买什么应季？",
    "运动会后吃什么？", "怎么提高注意力？", "野餐带什么？",
    "煲汤推荐？", "跑鞋怎么选？", "兴趣班报什么？",
]

def ask(msgs):
    payload = {"messages": msgs, "max_tokens": 150}
    req = urllib.request.Request(LLAMA, data=json.dumps(payload).encode(), headers={"Content-Type":"application/json"})
    t0=time.time()
    d = json.loads(urllib.request.urlopen(req, timeout=120).read())
    el=time.time()-t0
    u=d["usage"]; t=d["timings"]
    return {"cached":u["prompt_tokens_details"].get("cached_tokens",0), "prompt":u["prompt_tokens"],
            "pps":t.get("prompt_per_second",0), "total":el,
            "content":d["choices"][0]["message"].get("content","")}

print("="*60)
print("  小上下文(1000)超限压测 | 15轮 | 观察KV裁剪与缓存行为")
print("="*60)
history=[{"role":"system","content":SYSTEM}]
rows=[]
for i in range(15):
    q = QS[i]
    r = ask(history+[{"role":"user","content":q}])
    hit = r["cached"]/r["prompt"]*100 if r["prompt"] else 0
    over = "⚠️超限" if r["prompt"]>1000 else ""
    rows.append(r)
    print(f"  [{i+1:2d}] prompt={r['prompt']:4d} cached={r['cached']:4d} 命中={hit:3.0f}% "
          f"prefill={r['pps']:4.0f}t/s 总{r['total']:.1f}s {over}")
    history += [{"role":"user","content":q},{"role":"assistant","content":r["content"][:120]}]
    time.sleep(0.2)

print("\n"+"="*60)
ok=rows
in_lim=[r for r in ok if r["prompt"]<=1000]
ov=[r for r in ok if r["prompt"]>1000]
print(f"  总轮数: {len(ok)} | 1000内: {len(in_lim)}轮 平均命中 {sum(r['cached']/r['prompt'] for r in in_lim)/max(1,len(in_lim))*100:.0f}%")
if ov:
    print(f"  超限: {len(ov)}轮 平均命中 {sum(r['cached']/r['prompt'] for r in ov)/len(ov)*100:.0f}% (KV被裁剪后命中率变化)")
# 记忆保持测试: 问早期信息
print("\n  [记忆测试] 问第一轮的问题:")
try:
    r = ask(history+[{"role":"user","content":"我们最早聊了什么？"}])
    print(f"  回复: {r['content'][:80]}")
except Exception as e:
    print(f"  失败: {e}")
print("="*60)
