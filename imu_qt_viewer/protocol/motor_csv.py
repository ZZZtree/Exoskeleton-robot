# -*- coding: utf-8 -*-
"""
电机遥测 CSV 协议解析模块。
"""

from typing import Optional, Dict


def parse_motor_csv(line: bytes) -> Optional[Dict[str, float]]:
    """解析电机遥测数据行 (CSV 格式)。

    格式: timestamp_ms, 右髋, 右膝, 右踝[, 左髋, 左膝, 左踝]

    Args:
        line: 单行字节数据。

    Returns:
        包含 ts, r_hip, r_knee, r_ankle, l_hip, l_knee, l_ankle 的字典，或 None。
    """
    line_str = line.decode("utf-8", errors="replace").strip()
    if not line_str:
        return None

    parts = line_str.split(",")
    if len(parts) < 4:
        return None

    try:
        ts = float(parts[0])
        r_hip = float(parts[1])
        r_knee = float(parts[2])
        r_ankle = float(parts[3])

        if len(parts) >= 7:
            l_hip = float(parts[4])
            l_knee = float(parts[5])
            l_ankle = float(parts[6])
        else:
            l_hip = r_hip
            l_knee = r_knee
            l_ankle = r_ankle

        return {
            "ts": ts,
            "r_hip": r_hip,
            "r_knee": r_knee,
            "r_ankle": r_ankle,
            "l_hip": l_hip,
            "l_knee": l_knee,
            "l_ankle": l_ankle,
        }
    except ValueError:
        return None
