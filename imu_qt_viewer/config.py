# -*- coding: utf-8 -*-
"""
全局配置常量 — 集中管理所有硬编码值、协议定义、颜色方案。
"""

from typing import Dict, List, Optional, Tuple

import pyqtgraph as pg

# ============================================================
# AT 协议常量
# ============================================================
AT_HEADER: bytes = bytes([0x41, 0x54])          # "AT"
AT_FOOTER: bytes = bytes([0x0D, 0x0A])          # CR LF
DATA_LEN_TO_ADDR: Dict[int, int] = {0x02: 0x10, 0x04: 0x20, 0x06: 0x30, 0x08: 0x40}
ANGLE_SCALE: float = 180.0 / 32768.0
AT_STARTUP_COMMANDS: List[bytes] = [
    bytes([0x41, 0x54, 0x2B, 0x43, 0x47, 0x0D, 0x0A]),  # AT+CG
    bytes([0x41, 0x54, 0x2B, 0x41, 0x54, 0x0D, 0x0A]),  # AT+AT
]

# ============================================================
# IMU 地址配置
# ============================================================
IMU_ADDRESSES: List[Tuple[int, str]] = [
    (0x10, "IMU 0x10"),
    (0x20, "IMU 0x20"),
    (0x30, "IMU 0x30"),
    (0x40, "IMU 0x40"),
]

# IMU 地址 → 电机关节映射 (用于匹配度计算)
IMU_TO_JOINTS: Dict[int, Optional[List[str]]] = {
    0x10: None,
    0x20: ["rh"],
    0x30: ["rk"],
    0x40: ["ra"],
}

# ============================================================
# 电机关节定义
# ============================================================
MOTOR_JOINTS: List[str] = ["rh", "rk", "ra"]

JOINT_DISPLAY: Dict[str, Tuple[str, str]] = {
    "rh": ("髋", "#FF8844"),
    "rk": ("膝", "#44FF88"),
    "ra": ("踝", "#4488FF"),
}

# ============================================================
# 曲线颜色 (使用 Qt 颜色对象)
# ============================================================
CURVE_COLORS: Dict[str, pg.QtGui.QColor] = {
    "X": pg.mkColor("#FF4444"),
    "Y": pg.mkColor("#44FF44"),
    "Z": pg.mkColor("#4488FF"),
}

R_HIP: pg.QtGui.QColor = pg.mkColor("#FF8844")
R_KNEE: pg.QtGui.QColor = pg.mkColor("#44FF88")
R_ANKLE: pg.QtGui.QColor = pg.mkColor("#4488FF")
L_HIP: pg.QtGui.QColor = pg.mkColor("#FF4488")
L_KNEE: pg.QtGui.QColor = pg.mkColor("#88FF44")
L_ANKLE: pg.QtGui.QColor = pg.mkColor("#44AAFF")

# ============================================================
# 默认网络参数
# ============================================================
DEFAULT_UDP_IP: str = "172.20.15.38"
DEFAULT_UDP_PORT: int = 12345
DEFAULT_SEND_PORT: int = 12345
DEFAULT_BAUDRATE: int = 2000000

BAUD_RATES: List[str] = ["9600", "38400", "115200", "460800", "921600", "2000000"]

# ============================================================
# 采样与刷新频率
# ============================================================
PLOT_REFRESH_MS: int = 33        # ~30 FPS
RESPONSE_SEND_MS: int = 50       # 响应模式发送间隔
MATCH_TICK_MS: int = 100         # 匹配度计算间隔 (10 Hz)
SERIAL_TIMEOUT: float = 0.01     # 串口读取超时
UDP_TIMEOUT: float = 0.5         # UDP 接收超时
UDP_BUFFER_SIZE: int = 4096      # UDP 缓冲区大小

# ============================================================
# 匹配度配置
# ============================================================
MATCH_SAMPLE_WINDOW: int = 5     # 滑动窗口采样点数

# ============================================================
# 全局样式表
# ============================================================
APP_STYLESHEET: str = """
QMainWindow { background: #0D0D1A; }
QWidget { background: #0D0D1A; color: #CCC; }
QLabel { color: #CCC; }
QComboBox {
    background: #1E1E30; color: #CCC;
    border: 1px solid #555; border-radius: 3px; padding: 3px;
}
QComboBox::drop-down { border: none; }
QComboBox QAbstractItemView {
    background: #1E1E30; color: #CCC;
    selection-background-color: #3A3A5C;
}
QPushButton {
    background: #3A3A5C; color: #CCC;
    border: 1px solid #555; border-radius: 4px; padding: 4px 12px;
}
QPushButton:hover { background: #4A4A6C; }
QStatusBar { background: #16162B; color: #AAA; }
"""
