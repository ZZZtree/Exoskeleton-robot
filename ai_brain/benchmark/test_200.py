#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""200轮复杂对话压力测试 + 记忆抽查 (含TTFT)"""
import json, sys, time, urllib.request

GW = "http://localhost:8000/chat"
USER = "alice"

# 复杂问题池 (家庭/生活/知识混合)
QS = [
    "今天晚餐做什么清淡的菜？", "女儿不爱吃蔬菜怎么引导？", "周末郊游需要准备什么？",
    "晚上跑步还是瑜伽对膝盖好？", "孩子写作业坐姿怎么纠正？", "睡前读什么书培养兴趣？",
    "天气转凉换季要注意什么？", "早餐怎么搭配最营养？", "现在买什么水果应季？",
    "运动后吃什么补充能量？", "怎么帮孩子提高专注力？", "野餐需要带哪些装备？",
    "秋冬煲什么汤滋补？", "跑步鞋怎么选才合适？", "兴趣班给孩子报几个合适？",
    "出游计划怎么安排合理？", "孩子考前怎么复习高效？", "家庭聚会怎么组织？",
    "家里大米快没了多久买一次？", "阳台适合种什么花草？", "水费电费怎么网上交？",
    "孩子近视了要注意什么？", "怎么安排家庭每周活动？", "老年人饮食要注意什么？",
    "冰箱结霜怎么处理？", "周末亲子活动有哪些推荐？", "怎么培养孩子阅读习惯？",
    "秋季养肺吃什么好？", "家庭应急物资该备什么？", "怎么教孩子管理零花钱？",
]

# 分布在各个轮次的关键事实 (10个抽查点)
FACTS = [
    (6,  "记住：家里每周三晚上7点开家庭会议", "每周三晚上有什么安排？", ["周三", "会议"]),
    (20, "记住：女儿小美最近在学钢琴，老师说很有天赋", "女儿最近在学什么乐器？", ["钢琴"]),
    (45, "记住：爸爸下个月要参加马拉松比赛，最近在训练", "爸爸下个月要参加什么比赛？", ["马拉松"]),
    (70, "记住：家里打算下个月换新冰箱，预算5000块", "家里打算换什么家电？", ["冰箱"]),
    (95, "记住：阳台种的番茄快熟了，周日可以采摘", "阳台种的什么快熟了？", ["番茄"]),
    (120, "记住：妈妈最近在学烘焙，学会了做戚风蛋糕", "妈妈最近在学什么？", ["烘焙", "蛋糕"]),
    (145, "记住：下周末全家要去奶奶家过中秋节", "下周末全家要去哪里？", ["奶奶"]),
    (170, "记住：女儿期末考试下周五开始，要复习数学", "女儿什么时候期末考试？", ["周五", "考试"]),
    (190, "记住：小区下周停水一天，要提前储水", "小区下周有什么安排？", ["停水"]),
    (198, "记住：爸爸的新跑鞋到了，是蓝色耐克", "爸爸的新跑鞋是什么颜色？", ["蓝色"]),
]

def ask_stream(q):
    """流式请求, 返回 (首token时间ms, 完整回答)。"""
    d = json.dumps({"user": USER, "message": q, "stream": True}).encode()
    r = urllib.request.Request(GW, data=d, headers={"Content-Type": "application/json"})
    t0 = time.time(); first = None; buf = []
    with urllib.request.urlopen(r, timeout=120) as resp:
        for raw in resp:
            line = raw.decode("utf-8", "replace").strip()
            if not line.startswith("data:"): continue
            data = line[5:].strip()
            if data == "[DONE]": break
            try: obj = json.loads(data)
            except Exception: continue
            c = obj.get("d", "")
            if c:
                if first is None: first = time.time() - t0
                buf.append(c)
    return (first or 0) * 1000, "".join(buf)

def ask(q):
    d = json.dumps({"user": USER, "message": q, "stream": False}).encode()
    r = urllib.request.Request(GW, data=d, headers={"Content-Type": "application/json"})
    x = json.loads(urllib.request.urlopen(r, timeout=120).read())
    return x["reply"]

def main():
    print("="*66, flush=True)
    print("  200轮复杂对话压力测试 + 10点记忆抽查", flush=True)
    print("="*66, flush=True)
    fact_set = {f[0]: f for f in FACTS}
    fact_done = set()
    t_start = time.time()
    ok = 0
    for i in range(1, 201):
        if i in fact_set and i not in fact_done:
            q = fact_set[i][1]
            ask(q); fact_done.add(i)
        else:
            ask(QS[(i-1) % len(QS)])
        ok += 1
        if i % 25 == 0:
            el = time.time()-t_start
            print(f"  轮{i}: 完成 ({el:.0f}s, 平均{(el)/i:.2f}s/轮)", flush=True)
    total = time.time()-t_start
    print(f"\n  200轮完成! 总耗时{total:.0f}s 平均{total/200:.2f}s/轮\n", flush=True)
    print("="*66, flush=True)
    print("  记忆抽查 (10个早期对话, 含TTFT)", flush=True)
    print("="*66, flush=True)
    all_ok = 0
    for turn, fact, q, keys in FACTS:
        ttft, ans = ask_stream(q)
        c_ok = all(k in ans for k in keys)
        all_ok += c_ok
        print(f"  [轮{turn:3d}] {q} → TTFT {ttft:6.0f}ms | {'✅' if c_ok else '❌'} {ans[:40]}...", flush=True)
    print(f"\n  抽查结果: {all_ok}/10 正确 | 总测试{(time.time()-t_start):.0f}s", flush=True)

if __name__ == "__main__":
    main()
