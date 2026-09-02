#!/bin/bash
# ============================================================
# llama.cpp 效果测试脚本
# 测试: 健康检查 / 回答质量 / 生成速度 / 对比 Ollama
# 用法: bash /home/rock/test_llama.sh
# ============================================================

API="http://localhost:8080/v1/chat/completions"
OLLAMA="http://localhost:11434/api/chat"

echo "=============================================="
echo "  1️⃣  健康检查"
echo "=============================================="
if curl -s -m 5 http://localhost:8080/health | grep -q '"ok"'; then
    echo "  ✅ llama-server 运行正常 (8080)"
else
    echo "  ❌ llama-server 未运行! 启动: nohup bash /home/rock/start_llama_server.sh > /tmp/llama_server.log 2>&1 &"
    exit 1
fi
if curl -s -m 3 http://localhost:11434/ >/dev/null 2>&1; then
    echo "  ✅ Ollama 运行正常 (11434)"
else
    echo "  ⚠️  Ollama 未运行"
fi

echo ""
echo "=============================================="
echo "  2️⃣  回答质量测试"
echo "=============================================="
curl -s -m 60 "$API" -H 'Content-Type: application/json' \
    -d '{"messages":[{"role":"user","content":"中国的首都是哪里？用一句话回答"}],"max_tokens":100}' \
    | python3 -c "import sys,json; d=json.load(sys.stdin); print('  Q: 中国的首都是哪里？'); print('  A:', d['choices'][0]['message']['content'].strip())"

echo ""
echo "=============================================="
echo "  3️⃣  生成速度测试 (llama.cpp)"
echo "=============================================="
curl -s -m 60 "$API" -H 'Content-Type: application/json' \
    -d '{"messages":[{"role":"user","content":"请写一段200字左右的短文，介绍你最喜欢的季节"}],"max_tokens":200}' \
    -o /tmp/test_speed.json
python3 -c "
import json
d = json.load(open('/tmp/test_speed.json'))
t = d['timings']
print(f\"  生成: {t['predicted_n']} tokens, {t['predicted_per_second']:.1f} tok/s\")
print(f\"  首token延迟(TTFT): {t.get('prompt_ms',0)/1000:.2f} s\")
"

echo ""
echo "=============================================="
echo "  4️⃣  Ollama 同题速度 (对比)"
echo "=============================================="
curl -s -m 60 "$OLLAMA" -d '{"model":"qwen3.5:2b","messages":[{"role":"user","content":"请写一段200字左右的短文，介绍你最喜欢的季节"}],"think":false,"stream":false}' \
    -o /tmp/ollama_speed.json
python3 -c "
import json
d = json.load(open('/tmp/ollama_speed.json'))
n = d.get('eval_count', 0)
s = d.get('eval_duration', 0) / 1e9
print(f\"  生成: {n} tokens, {n/s:.1f} tok/s\" if s else '  无数据')
"

echo ""
echo "=============================================="
echo "  📊 对比总结"
echo "=============================================="
python3 -c "
import json
l = json.load(open('/tmp/test_speed.json'))['timings']['predicted_per_second']
o = json.load(open('/tmp/ollama_speed.json'))
on = o.get('eval_count', 0); od = o.get('eval_duration', 0)/1e9
os = on/od if od else 0
print(f\"  llama.cpp: {l:.1f} tok/s   vs   Ollama: {os:.1f} tok/s\")
print(f\"  llama.cpp 快 {l/os*100-100:.0f}%\" if os > 0 else '  Ollama 无数据')
"
