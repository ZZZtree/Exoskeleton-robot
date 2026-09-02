#!/bin/bash
# ============================================================
# Jetson AGX Orin 性能模式切换 (提高 LLM 推理速度)
# 用法: sudo bash /home/rock/turbo_mode.sh
#   MAXN = 60W 全速 (推荐推理用)
#   切回省电: sudo bash /home/rock/turbo_mode.sh --back
# ============================================================

if [ "$1" == "--back" ]; then
    echo "切回 30W 省电模式..."
    nvpmodel -m 2
    echo "完成: $(nvpmodel -q)"
    exit 0
fi

echo "当前: $(nvpmodel -q)"
echo "切换到 MAXN 模式 (60W)..."
nvpmodel -m 0
echo "锁定最高频率 (jetson_clocks)..."
jetson_clocks
echo ""
echo "✅ 完成! 当前模式: $(nvpmodel -q)"
echo "   CPU: $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq 2>/dev/null | awk '{printf "%.0f MHz", $1/1000}')"
echo ""
echo "⚠️  注意: MAXN 60W 功耗高发热大, 请确保散热良好"
echo "   如需切回省电: sudo bash /home/rock/turbo_mode.sh --back"
