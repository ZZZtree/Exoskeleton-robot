#!/bin/bash
# ============================================================
# 一键启动: 双模型对比
#   自动检查/启动 llama-server (8080) + Ollama (11434)
#   然后打开流式对比终端
# 用法: bash /home/rock/start_compare.sh
# ============================================================

echo "=== 检查服务状态 ==="

# 1. llama-server (8080)
if curl -s -m 3 http://localhost:8080/health >/dev/null 2>&1; then
    echo "  ✅ llama-server 已在运行 (8080, Q8_0)"
else
    echo "  ⏳ 启动 llama-server..."
    nohup bash /home/rock/start_llama_server.sh > /tmp/llama_server.log 2>&1 &
    sleep 10
    if curl -s -m 3 http://localhost:8080/health >/dev/null 2>&1; then
        echo "  ✅ llama-server 启动成功"
    else
        echo "  ❌ llama-server 启动失败, 查看日志: tail /tmp/llama_server.log"
    fi
fi

# 2. Ollama (11434)
if curl -s -m 3 http://localhost:11434/ >/dev/null 2>&1; then
    echo "  ✅ Ollama 已在运行 (11434)"
else
    echo "  ⚠️  Ollama 未运行 (尝试 systemctl start ollama, 需要 sudo)"
fi

echo ""
echo "=== 打开对比终端 ==="
python3 /home/rock/compare_models.py
