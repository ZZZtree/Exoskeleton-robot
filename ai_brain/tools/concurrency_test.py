#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
多用户并发压测: 家庭网关(:8000) 在 1/4/8/16/32 并发下的吞吐与延迟
每个"用户"连续 3 轮对话(带上下文), 统计:
  总墙钟 / 吞吐(req/s) / TTFT 中位与P95 / 端到端中位 / 失败数
输出: /tmp/concurrency_test.txt
"""
import json, time, urllib.request, statistics
from concurrent.futures import ThreadPoolExecutor

OUT = "/tmp/concurrency_test.txt"
GW = "http://localhost:8000/chat"
LEVELS = [1, 4, 8, 16, 32]
ROUNDS = 3
MAXTOK = 48
QS = ["今天晚餐吃什么", "周末郊游带什么", "晚上跑步还是瑜伽", "孩子写作业坐姿",
      "睡前读什么书", "天气转凉注意什么", "早餐怎么搭配", "水果买什么好",
      "运动后吃什么", "怎么提高注意力", "野餐带什么", "煲汤推荐",
      "跑鞋怎么选", "兴趣班报什么", "出游去哪", "考试怎么准备",
      "聚会怎么安排", "阳台种什么", "水费怎么交", "孩子近视怎么办"]

def log(m):
    print(m, flush=True)
    open(OUT, "a").write(m + "\n")

def ask(user, msg):
    d = json.dumps({"user": user, "message": msg, "stream": True, "max_tokens": MAXTOK}).encode()
    r = urllib.request.Request(GW, data=d, headers={"Content-Type": "application/json"})
    t0 = time.time(); ttft = None
    try:
        with urllib.request.urlopen(r, timeout=600) as resp:
            for raw in resp:
                line = raw.decode("utf-8", "replace").strip()
                if not line.startswith("data:"):
                    continue
                data = line[5:].strip()
                if data == "[DONE]":
                    break
                try:
                    o = json.loads(data)
                except Exception:
                    continue
                if o.get("d"):
                    if ttft is None:
                        ttft = time.time() - t0
        return (ttft, time.time() - t0, None)
    except Exception as e:
        return (None, time.time() - t0, str(e)[:50])

def user_loop(uid):
    out = []
    for r in range(ROUNDS):
        out.append(ask(f"u{uid}", QS[(uid * 3 + r) % len(QS)]))
    return out

def run_level(c):
    log(f"\n{'='*68}")
    log(f"  并发度 C={c}  ({c} 个用户 × {ROUNDS} 轮 = {c*ROUNDS} 请求)")
    log("=" * 68)
    t0 = time.time()
    with ThreadPoolExecutor(max_workers=c) as ex:
        all_res = list(ex.map(user_loop, range(c)))
    wall = time.time() - t0
    res = [x for sub in all_res for x in sub]
    ok = [x for x in res if x[2] is None]
    err = [x for x in res if x[2] is not None]
    ttfts = sorted(x[0] * 1000 for x in ok if x[0])
    totals = sorted(x[1] for x in ok)
    def pct(xs, p):
        return xs[min(len(xs) - 1, int(len(xs) * p))] if xs else 0
    log(f"  总墙钟: {wall:.1f}s | 成功 {len(ok)}/{len(res)} | 失败 {len(err)}")
    log(f"  吞吐: {len(ok)/wall:.2f} req/s")
    if ttfts:
        log(f"  TTFT: 中位 {statistics.median(ttfts):.0f}ms | P95 {pct(ttfts,0.95):.0f}ms | 最小 {ttfts[0]:.0f}ms | 最大 {ttfts[-1]:.0f}ms")
    if totals:
        log(f"  端到端(每请求): 中位 {statistics.median(totals):.2f}s | 最大 {totals[-1]:.2f}s")
    if err:
        log(f"  失败样例: {err[0][2]}")
    return {"c": c, "wall": wall, "ok": len(ok), "fail": len(err),
            "tput": len(ok)/wall, "ttft_med": statistics.median(ttfts) if ttfts else 0,
            "ttft_p95": pct(ttfts, 0.95), "e2e_med": statistics.median(totals) if totals else 0}

def main():
    open(OUT, "w").close()
    log("=" * 68)
    log("  多用户并发压测: 家庭网关 :8000 → llama-server :8080 (9B/64K池/16slot统一)")
    log(f"  每用户连续 {ROUNDS} 轮 | max_tokens={MAXTOK} | {time.strftime('%Y-%m-%d %H:%M')}")
    log("=" * 68)

    # 预热
    log("\n[预热] 1 个请求...")
    ask("warmup", "你好")

    rows = [run_level(c) for c in LEVELS]

    log(f"\n{'='*68}")
    log("  📊 并发扩展性汇总")
    log("=" * 68)
    log(f"  {'并发':<6}{'总墙钟':<10}{'成功/总':<12}{'吞吐(req/s)':<14}{'TTFT中位':<12}{'TTFT P95':<12}{'端到端中位'}")
    log(f"  {'-'*80}")
    for r in rows:
        log(f"  {r['c']:<6}{r['wall']:<10.1f}{str(r['ok'])+'/'+str(r['ok']+r['fail']):<12}"
            f"{r['tput']:<14.2f}{r['ttft_med']:<12.0f}{r['ttft_p95']:<12.0f}{r['e2e_med']:.2f}s")

if __name__ == "__main__":
    main()
