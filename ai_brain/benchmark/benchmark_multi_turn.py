#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
多轮长对话 A/B 对比: 改进后的固定slot vs 改进前的动态slot
============================================================
验证 SWAP 策略是否有效:
  - 缓存命中率 (cached_tokens / prompt_tokens)
  - prefill 速度 (prompt_per_second)
  - 每轮总耗时

用法: python3 /home/rock/benchmark_multi_turn.py
"""
import json
import time
import urllib.request

LLAMA = "http://localhost:8080/v1/chat/completions"

SYSTEM = ("你是家庭智能助手。"
          "家庭成员：alice(妈妈,喜欢烹饪和园艺)、bob(爸爸,喜欢运动和数码)、carol(女儿,12岁学生)。"
          "家庭规则：1.晚上10点后保持安静 2.饮食清淡健康 3.周日上午家庭活动。"
          "常用信息：家在北京市朝阳区，水费支付宝缴纳，物业电话12345678。")

# 多轮长对话: 每轮是"连续对话"的一部分, 历史逐步累积
TURNS = [
    "今天想给家人做一顿饭，有什么清淡健康又简单的推荐吗？",
    "女儿最近有点挑食，不爱吃蔬菜，怎么办？",
    "那我们周三晚餐就做你说的那些吧，需要提前准备什么食材？",
    "对了，家里米快没了，一般多久买一次比较合适？",
    "周日上午的家庭活动，你有什么适合一家三口的建议吗？",
    "天气开始转凉了，给女儿换季衣物有什么要注意的？",
    "我最近肩颈酸痛，晚上十点后还能做点舒缓运动吗？",
    "下个月老同学要来北京，想带他们逛逛朝阳区，有推荐路线吗？",
    "物业费和水费都该交了，能帮我整理一下本月要交的费用吗？",
    "国庆假期计划全家出游，你能给个三天两夜的行程建议吗？",
]


def ask(messages, id_slot):
    """发请求, 返回 (cached, prompt_tokens, prefill_speed, total_s, answer)。"""
    payload = {"messages": messages, "max_tokens": 40, "id_slot": id_slot}
    req = urllib.request.Request(
        LLAMA, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"})
    t0 = time.time()
    with urllib.request.urlopen(req, timeout=300) as resp:
        d = json.loads(resp.read())
    total = time.time() - t0
    u = d["usage"]
    t = d["timings"]
    cached = u["prompt_tokens_details"].get("cached_tokens", 0)
    ans = d["choices"][0]["message"].get("content", "")
    return cached, u["prompt_tokens"], t.get("prompt_per_second", 0), total, ans


def run_multi_turn(name, id_slot):
    """跑一轮多轮长对话, 返回 (结果列表, 汇总)。"""
    print(f"\n{'='*62}")
    print(f"  {name}  (id_slot={id_slot})")
    print(f"{'='*62}")
    history = []
    rows = []
    for i, q in enumerate(TURNS, 1):
        messages = ([{"role": "system", "content": SYSTEM}]
                    + history
                    + [{"role": "user", "content": q}])
        cached, prompt, pps, total, ans = ask(messages, id_slot)
        hit = cached / prompt * 100 if prompt else 0
        rows.append({"turn": i, "prompt": prompt, "cached": cached,
                     "hit": hit, "prefill_s": pps, "total_s": total})
        print(f"  [{i:2d}] prompt={prompt:4d} cached={cached:4d} "
              f"命中={hit:4.0f}% prefill={pps:5.0f}t/s 总{total:5.1f}s")
        # 累积历史 (模拟长对话)
        history.append({"role": "user", "content": q})
        history.append({"role": "assistant", "content": ans[:200]})
        time.sleep(0.3)
    avg_hit = sum(r["hit"] for r in rows) / len(rows)
    avg_pps = sum(r["prefill_s"] for r in rows) / len(rows)
    total_all = sum(r["total_s"] for r in rows)
    return rows, {"avg_hit": avg_hit, "avg_prefill": avg_pps, "total_s": total_all}


def main():
    print("=" * 62)
    print("  多轮长对话 A/B 对比: 固定slot(SWAP后) vs 动态slot(SWAP前)")
    print(f"  10 轮对话 | 系统提示 {len(SYSTEM)} 字符 | 16K 上下文")
    print("=" * 62)

    # 顺序: 先动态(前) 后固定(后), 避免缓存残留影响
    rows_a, sum_a = run_multi_turn("A. SWAP前(动态slot, id_slot=-1)", -1)
    rows_b, sum_b = run_multi_turn("B. SWAP后(固定slot, id_slot=0)", 0)

    print(f"\n{'='*62}")
    print("  📊 对比总结")
    print(f"{'='*62}")
    print(f"  {'指标':<24}{'A.动态slot(SWAP前)':<20}{'B.固定slot(SWAP后)'}")
    print(f"  {'-'*58}")
    print(f"  {'平均缓存命中率':<20}{sum_a['avg_hit']:<22.1f}{sum_b['avg_hit']:.1f}%")
    print(f"  {'平均prefill速度':<20}{sum_a['avg_prefill']:<22.0f}{sum_b['avg_prefill']:.0f} tok/s")
    print(f"  {'10轮总耗时':<20}{sum_a['total_s']:<22.1f}{sum_b['total_s']:.1f} s")
    print()
    if sum_a["avg_hit"] > 0:
        print(f"  → 缓存命中率提升: {sum_b['avg_hit']-sum_a['avg_hit']:+.1f} 个百分点")
    print(f"  → 总耗时缩短: {sum_a['total_s']-sum_b['total_s']:.1f} s "
          f"({(sum_a['total_s']-sum_b['total_s'])/sum_a['total_s']*100:.0f}%)")


if __name__ == "__main__":
    main()
