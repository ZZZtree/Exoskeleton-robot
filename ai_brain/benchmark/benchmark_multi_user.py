#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""多用户交错对话 A/B 对比: 固定slot vs 动态slot (真实家庭场景)"""
import json, time, urllib.request

LLAMA = "http://localhost:8080/v1/chat/completions"
SYSTEM = "你是家庭智能助手。家庭成员：alice(妈妈,喜欢烹饪)、bob(爸爸,喜欢运动)、carol(女儿,12岁)。回答用中文简洁。"

# 每用户 4 个连续问题 (模拟各自的多轮对话)
USER_QS = {
  "alice": ["今天晚餐做什么好？", "明天早餐呢？", "周末请朋友来吃饭怎么安排？", "最近有什么应季水果？"],
  "bob":   ["晚上跑步要注意什么？", "最近想换跑鞋，有什么建议？", "周末想去爬山，推荐哪里？", "运动后吃什么补充能量？"],
  "carol": ["明天要考试了，怎么复习？", "做完作业可以玩多久游戏？", "下周学校春游要带什么？", "怎么和同学相处更好？"],
}
USERS = ["alice", "bob", "carol"]

def clear_slots():
    req = urllib.request.Request(LLAMA.replace("/v1/chat/completions", "/slots"),
                                 data=b"{}", headers={"Content-Type":"application/json"}, method="POST")
    try: urllib.request.urlopen(req, timeout=8).read()
    except Exception: pass
    time.sleep(1)

def slot_of(u):
    import hashlib
    return int(hashlib.md5(u.encode()).hexdigest()[:8], 16) % 4

def run(name, fixed):
    print(f"\n=== {name} ===")
    clear_slots()
    hist = {u: [] for u in USERS}
    rows = []
    # 交错轮次: 每轮 3 用户各问 1 个问题 (模拟家庭同时使用)
    for rnd in range(4):
        for u in USERS:
            q = USER_QS[u][rnd]
            msgs = [{"role":"system","content":SYSTEM}] + hist[u] + [{"role":"user","content":q}]
            payload = {"messages": msgs, "max_tokens": 30}
            if fixed: payload["id_slot"] = slot_of(u)
            req = urllib.request.Request(LLAMA, data=json.dumps(payload).encode(), headers={"Content-Type":"application/json"})
            t0=time.time()
            d = json.loads(urllib.request.urlopen(req, timeout=300).read())
            el = time.time()-t0
            u_ = d["usage"]
            cached = u_["prompt_tokens_details"].get("cached_tokens",0)
            prompt = u_["prompt_tokens"]
            hit = cached/prompt*100 if prompt else 0
            rows.append(hit)
            print(f"  轮{rnd+1} {u:<6} prompt={prompt:4d} cached={cached:4d} 命中={hit:3.0f}% {el:.1f}s")
            hist[u] += [{"role":"user","content":q}, {"role":"assistant","content":d["choices"][0]["message"].get("content","")[:100]}]
            time.sleep(0.2)
    avg = sum(rows)/len(rows)
    first = rows[:3]
    print(f"  → {name} 平均命中: {avg:.1f}% | 第1轮(冷启动): {sum(first)/len(first):.0f}%")
    return avg

print("="*60)
print("  多用户交错对话对比: 固定slot(SWAP后) vs 动态slot(SWAP前)")
print("  12 个请求 | 3 用户交错 | 每轮前清空KV池(公平)")
print("="*60)
a = run("A. 动态slot (SWAP前)", fixed=False)
b = run("B. 固定slot (SWAP后)", fixed=True)
print(f"\n{'='*60}")
print(f"  平均命中率:  A动态={a:.1f}%  vs  B固定={b:.1f}%")
print(f"  → 固定slot 提升 {b-a:+.1f} 个百分点")
print(f"{'='*60}")
