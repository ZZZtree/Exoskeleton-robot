#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""9B 模型 200轮超长对话 + 磁盘swap/RAG验证 + 随机抽查"""
import json, random, time, urllib.request, os, gzip

GW = "http://localhost:8000/chat"
USER = "alice"

QS = ["今天晚餐吃什么","周末郊游带什么","晚上跑步还是瑜伽","孩子写作业坐姿","睡前读什么书",
      "天气转凉注意","早餐怎么搭配","水果买什么","运动后吃什么","怎么提高注意力",
      "野餐带什么","煲汤推荐","跑鞋怎么选","兴趣班报什么","出游去哪",
      "考试怎么准备","聚会安排","阳台种什么","水费怎么交","孩子近视怎么办"]

# 10个事实分布在200轮
FACTS = [
    (6,   "记住：家里每周三晚7点开家庭会议", "每周三晚上有什么安排？", ["会议"]),
    (25,  "记住：女儿在学钢琴，老师说很有天赋", "女儿最近在学什么乐器？", ["钢琴"]),
    (50,  "记住：爸爸下月参加马拉松比赛", "爸爸下月要参加什么比赛？", ["马拉松"]),
    (75,  "记住：家里要换新冰箱，预算5000", "家里要换什么家电？", ["冰箱"]),
    (100, "记住：阳台番茄快熟了，周日采摘", "阳台种的什么快熟了？", ["番茄"]),
    (125, "记住：妈妈在学烘焙，会做戚风蛋糕", "妈妈最近在学什么？", ["蛋糕"]),
    (150, "记住：下周末全家去奶奶家过中秋", "下周末全家去哪？", ["奶奶"]),
    (170, "记住：女儿期末考下周五开始", "女儿什么时候考试？", ["周五"]),
    (185, "记住：小区下周停水一天", "小区下周有什么安排？", ["停水"]),
    (195, "记住：爸爸买了蓝色耐克跑鞋", "爸爸的新跑鞋什么颜色？", ["蓝色"]),
]

def ask_stream(q):
    d = json.dumps({"user": USER, "message": q, "stream": True}).encode()
    r = urllib.request.Request(GW, data=d, headers={"Content-Type": "application/json"})
    t0 = time.time(); first=None; buf=[]
    with urllib.request.urlopen(r, timeout=120) as resp:
        for raw in resp:
            line = raw.decode("utf-8","replace").strip()
            if not line.startswith("data:"): continue
            data = line[5:].strip()
            if data == "[DONE]": break
            try: obj=json.loads(data)
            except: continue
            c = obj.get("d","")
            if c:
                if first is None: first=time.time()-t0
                buf.append(c)
    return (first or 0)*1000, "".join(buf)

def ask(q):
    d = json.dumps({"user": USER, "message": q, "stream": False}).encode()
    r = urllib.request.Request(GW, data=d, headers={"Content-Type":"application/json"})
    return json.loads(urllib.request.urlopen(r, timeout=120).read())["reply"]

print("="*64, flush=True)
print("  9B模型 200轮超长对话 | ctx=500 | 磁盘swap+RAG验证", flush=True)
print("="*64, flush=True)
t0=time.time()
fset={f[0]:f for f in FACTS}
for i in range(1,201):
    if i in fset:
        ask(fset[i][1])
    else:
        ask(QS[(i-1)%len(QS)])
    if i%40==0:
        print(f"  轮{i}: {time.time()-t0:.0f}s (avg{(time.time()-t0)/i:.1f}s/轮)", flush=True)
print(f"\n  200轮完成! 总{(time.time()-t0):.0f}s 平均{(time.time()-t0)/200:.1f}s/轮\n", flush=True)

# 随机抽查 8 个 (含早期/中间/近期)
random.seed(42)
pick = random.sample(FACTS, 8)
print("="*64, flush=True)
print(f"  随机抽查 {len(pick)} 个对话内容 (含TTFT)", flush=True)
print("="*64, flush=True)
ok=0
for turn, fact, q, keys in sorted(pick):
    ttft, ans = ask_stream(q)
    c_ok = all(k in ans for k in keys)
    ok += c_ok
    print(f"  [轮{turn:3d}] {q} → TTFT{ttft:5.0f}ms {'✅' if c_ok else '❌'} {ans[:45]}...", flush=True)

# 检查磁盘快照
sz = 0
if os.path.exists("/home/rock/kv_swap/alice.json.gz"):
    sz = os.path.getsize("/home/rock/kv_swap/alice.json.gz")
print(f"\n  抽查结果: {ok}/{len(pick)} 正确", flush=True)
print(f"  磁盘快照: {sz/1024:.0f}KB | 总耗时{time.time()-t0:.0f}s", flush=True)
