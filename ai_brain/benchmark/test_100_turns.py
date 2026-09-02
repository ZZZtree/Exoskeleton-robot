#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""100轮超长对话极限测试: 早期记忆检索恢复"""
import json, sys, time, urllib.request

GW = "http://localhost:8000/chat"
OLLAMA = "http://localhost:11434/api/chat"

QS = ["今天晚餐吃什么","周末郊游带什么","晚上跑步还是瑜伽","孩子写作业坐姿","睡前读什么书",
      "天气转凉注意","早餐怎么搭配","水果买什么","运动后吃什么","怎么提高注意力",
      "野餐带什么","煲汤推荐","跑鞋怎么选","兴趣班报什么","出游去哪",
      "考试怎么准备","下周聚会安排","家里米快没了","阳台种什么花","水费怎么交"]

FACT = "记住：我叫爱丽丝，女儿叫小美，今年10岁，喜欢喝咖啡，家里养了猫叫咪咪"
MEMORY_Q = "我叫什么名字？我女儿叫什么？我家养了什么宠物？"

def ask_gw(q, user="alice"):
    d = json.dumps({"user": user, "message": q, "stream": False}).encode()
    r = urllib.request.Request(GW, data=d, headers={"Content-Type": "application/json"})
    t0 = time.time()
    try:
        x = json.loads(urllib.request.urlopen(r, timeout=60).read())
        return x["reply"], time.time()-t0, True
    except Exception as e:
        return str(e), time.time()-t0, False

def ask_ollama(hist, q):
    msgs = hist + [{"role": "user", "content": q}]
    d = json.dumps({"model": "qwen3.5:2b", "messages": msgs, "stream": False,
                    "think": False, "options": {"num_ctx": 1000}}).encode()
    r = urllib.request.Request(OLLAMA, data=d, headers={"Content-Type": "application/json"})
    t0 = time.time()
    try:
        x = json.loads(urllib.request.urlopen(r, timeout=120).read())
        return x["message"]["content"], time.time()-t0, True
    except Exception as e:
        return str(e), time.time()-t0, False

def test_gateway():
    print("="*62, flush=True)
    print("  [改进后] 100轮超长对话 + 磁盘恢复", flush=True)
    print("="*62, flush=True)
    r, el, ok = ask_gw(FACT)
    print(f"  轮1 注入事实: {'OK' if ok else 'FAIL'}", flush=True)
    ok_n = 0
    for i in range(2, 101):
        r, el, ok = ask_gw(QS[(i-2) % len(QS)])
        if ok:
            ok_n += 1
        if i % 20 == 0:
            print(f"  轮{i}: 完成 ({ok_n}/{i-1} 成功, 未中断)", flush=True)
    print(f"  100轮完成: {ok_n+1}/{100} 成功", flush=True)
    print("\n  --- 问早期记忆(第1轮事实) ---", flush=True)
    r, el, ok = ask_gw(MEMORY_Q)
    print(f"  回答: {r[:90]}", flush=True)
    print(f"  恢复耗时: {el:.2f}s", flush=True)
    correct = ("爱丽丝" in r and ("小美" in r or "咪咪" in r or "咖啡" in r))
    print(f"  正确性: {'✅ 正确' if correct else '❌ 错误'}", flush=True)
    return correct, el

def test_ollama():
    print("\n" + "="*62, flush=True)
    print("  [Ollama] 100轮超长对话 + 重新计算", flush=True)
    print("="*62, flush=True)
    h = []
    r, el, ok = ask_ollama(h, FACT)
    print(f"  轮1 注入事实: {'OK' if ok else 'FAIL'}", flush=True)
    h += [{"role": "user", "content": FACT}, {"role": "assistant", "content": r[:60]}]
    for i in range(2, 101):
        q = QS[(i-2) % len(QS)]
        r, el, ok = ask_ollama(h, q)
        if not ok:
            print(f"  轮{i}: ERROR {r[:50]}", flush=True)
            break
        h += [{"role": "user", "content": q}, {"role": "assistant", "content": r[:60]}]
        if i % 20 == 0:
            print(f"  轮{i}: 完成 (历史{len(h)}条)", flush=True)
    print(f"  完成, 历史 {len(h)} 条", flush=True)
    print("\n  --- 问早期记忆(第1轮事实) ---", flush=True)
    r, el, ok = ask_ollama(h, MEMORY_Q)
    print(f"  回答: {r[:90]}", flush=True)
    print(f"  耗时(含重新prefill): {el:.2f}s", flush=True)
    correct = ("爱丽丝" in r and ("小美" in r or "咪咪" in r or "咖啡" in r))
    print(f"  正确性: {'✅ 正确' if correct else '❌ 错误'}", flush=True)
    return correct, el

mode = sys.argv[1] if len(sys.argv) > 1 else "both"
if mode in ("gw", "both"):
    test_gateway()
if mode in ("ollama", "both"):
    test_ollama()
