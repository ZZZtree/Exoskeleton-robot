#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""50轮长对话实战压测: 验证前缀缓存 + 缓存表在真实场景的表现"""
import json, time, urllib.request

LLAMA = "http://localhost:8080/v1/chat/completions"
GW = "http://localhost:8000/chat"

SYSTEM = ("你是家庭智能助手。"
          "家庭成员：alice(妈妈,喜欢烹饪和园艺)、bob(爸爸,喜欢运动和数码)、carol(女儿,12岁学生)。"
          "家庭规则：1.晚上10点后保持安静 2.饮食清淡健康 3.周日上午家庭活动。"
          "常用信息：家在北京市朝阳区，水费支付宝缴纳，物业电话12345678。"
          "回答要求：用中文、简洁、有帮助。")

# 家庭日常问题池 (50轮循环使用, 每轮上下文累积)
QS = [
    "今天晚餐做什么清淡的菜？", "女儿不爱吃蔬菜怎么办？", "冰箱里有鸡蛋和西红柿，能做什么？",
    "最近水果该买什么应季的？", "周末想给家人煲汤，有什么推荐？", "早餐怎么搭配更有营养？",
    "晚上想运动，跑步还是瑜伽好？", "跑步膝盖疼怎么缓解？", "运动后吃什么补充能量？",
    "孩子写作业坐姿不好怎么办？", "怎么帮孩子提高注意力？", "睡前给孩子读什么书好？",
    "国庆全家出游，三天两夜去哪？", "去郊外野餐要带什么？", "给孩子报什么兴趣班好？",
    "家里水费怎么交？", "物业电话是多少来着？", "最近天气转凉要注意什么？",
    "晚上十点后家里要保持安静吗？", "周日上午的家庭活动安排什么好？",
]

def ask_direct(history):
    """直接调 llama-server, 返回完整指标。"""
    payload = {"messages": history, "max_tokens": 120}
    req = urllib.request.Request(LLAMA, data=json.dumps(payload).encode(),
                                 headers={"Content-Type":"application/json"})
    t0 = time.time()
    d = json.loads(urllib.request.urlopen(req, timeout=300).read())
    el = time.time()-t0
    u = d["usage"]; t = d["timings"]
    cached = u["prompt_tokens_details"].get("cached_tokens",0)
    return {"cached": cached, "prompt": u["prompt_tokens"],
            "pps": t.get("prompt_per_second",0), "total": el,
            "content": d["choices"][0]["message"].get("content","")}

def main():
    print("="*66)
    print("  50轮长对话实战压测 (家庭场景, 上下文累积)")
    print("="*66)
    history = [{"role":"system","content":SYSTEM}]
    rows = []
    for i in range(50):
        q = QS[i % len(QS)]
        msgs = history + [{"role":"user","content":q}]
        try:
            r = ask_direct(msgs)
        except Exception as e:
            print(f"  [{i+1:2d}] 失败: {e}"); rows.append(None); break
        hit = r["cached"]/r["prompt"]*100 if r["prompt"] else 0
        rows.append(r)
        if (i+1) % 5 == 0 or i == 0:
            print(f"  [{i+1:2d}] prompt={r['prompt']:4d} cached={r['cached']:4d} "
                  f"命中={hit:3.0f}% prefill={r['pps']:4.0f}t/s 总{r['total']:.1f}s")
        history += [{"role":"user","content":q},
                    {"role":"assistant","content":r["content"][:150]}]
        time.sleep(0.2)

    ok = [r for r in rows if r]
    avg_hit = sum(r["cached"]/r["prompt"] for r in ok)/len(ok)*100
    avg_pps = sum(r["pps"] for r in ok)/len(ok)
    avg_total = sum(r["total"] for r in ok)/len(ok)
    # 分阶段命中率
    n = len(ok)
    s1 = sum(r["cached"]/r["prompt"] for r in ok[:n//3])/(n//3)*100
    s2 = sum(r["cached"]/r["prompt"] for r in ok[n//3:2*n//3])/(n//3)*100
    s3 = sum(r["cached"]/r["prompt"] for r in ok[2*n//3:])/(n-len(ok[:2*n//3]))*100
    last_prompt = ok[-1]["prompt"]
    print("\n" + "="*66)
    print("  📊 压测总结")
    print(f"  完成轮数: {len(ok)} | 最终上下文: {last_prompt} tokens")
    print(f"  平均命中率: {avg_hit:.1f}%")
    print(f"  命中率趋势: 前1/3={s1:.0f}% → 中1/3={s2:.0f}% → 后1/3={s3:.0f}%")
    print(f"  平均prefill: {avg_pps:.0f} tok/s | 平均每轮总耗时: {avg_total:.1f}s")
    print(f"  (16K上限={16384} tokens)")
    print("="*66)

if __name__ == "__main__":
    main()
