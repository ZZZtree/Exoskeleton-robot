#!/bin/bash
# ============================================================
# llama.cpp 推理服务一键启动脚本 (Jetson AGX Orin)
# 优化配置: CUDA 全量 offload + flash-attn + KV cache q8_0 + mlock
# 依赖: /home/rock/llama.cpp/build/bin/llama-server (自编译, 含 Jetson 统一内存 patch)
# ============================================================

MODEL="/home/rock/models/qwen3.5-9b-q4_k_m.gguf"
BIN="/home/rock/llama.cpp/build/bin/llama-server"
PORT="${PORT:-8080}"
CTX="${CTX:-65536}"
THREADS="${THREADS:-8}"

# Jetson 统一内存自动启用已编入 ggml-cuda (integrated GPU 自动识别)
# 如需强制禁用: export GGML_CUDA_DISABLE_UNIFIED_MEMORY=1
# --reasoning off: 关闭 Qwen3.5 的长思考, 直接回答 (需要思考时去掉此参数)

if [ ! -f "$MODEL" ]; then
    echo "错误: 模型不存在 $MODEL"
    echo "下载: curl -L https://huggingface.co/bartowski/Qwen_Qwen3.5-2B-GGUF/resolve/main/Qwen_Qwen3.5-2B-Q4_K_M.gguf -o $MODEL"
    exit 1
fi

echo "启动 llama-server: $MODEL"
echo "  ctx=$CTX threads=$THREADS port=$PORT (4 slots, flash-attn, KV q8_0, mlock)"
echo "  API: http://0.0.0.0:$PORT/v1/chat/completions"

exec "$BIN" \
    -m "$MODEL" \
    -ngl 99 \
    -t "$THREADS" \
    -c "$CTX" \
    --parallel 16 \
    --kv-unified \
    --host 0.0.0.0 \
    --port "$PORT" \
    -fa on \
    -ctk q8_0 -ctv q8_0 \
    -lm mlock \
    --reasoning off \
    --cache-prompt \
    --cache-reuse 256 \
    --slot-save-path /home/rock/kv_swap/
