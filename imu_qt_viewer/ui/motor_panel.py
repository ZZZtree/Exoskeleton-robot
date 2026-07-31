# -*- coding: utf-8 -*-
"""
电机曲线面板 — 电机角度 (髋/膝/踝) + RTT + 匹配度显示控件。
"""

from collections import deque
from typing import Dict, Optional

from PyQt5.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QLabel
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QFont

import pyqtgraph as pg

from ..config import R_HIP, R_KNEE, R_ANKLE, JOINT_DISPLAY, MATCH_SAMPLE_WINDOW


class MotorPlotPanel(QWidget):
    """电机角度 (髋/膝/踝) + RTT + 匹配度。

    接收 UDP 遥测数据并以曲线形式实时展示右腿三关节角度，
    同时维护 RTT 标签和基于 IMU 的匹配度滑动窗口计算。
    """

    def __init__(self, parent: Optional[QWidget] = None):
        super().__init__(parent)
        self.times: Dict[str, deque] = {k: deque() for k in ["rh", "rk", "ra"]}
        self.vals: Dict[str, deque] = {k: deque() for k in ["rh", "rk", "ra"]}
        self.start_timestamp: Optional[float] = None

        self.match_samples: Dict[str, deque] = {
            k: deque(maxlen=MATCH_SAMPLE_WINDOW) for k in ["rh", "rk", "ra"]
        }
        self.last_motor: Dict[str, float] = {k: 0.0 for k in ["rh", "rk", "ra"]}
        self.last_imu: Dict[str, float] = {k: 0.0 for k in ["rh", "rk", "ra"]}
        self.initialized: bool = False

        self.relative_mode: bool = False
        self.unwrapped: Dict[str, float] = {k: 0.0 for k in ["rh", "rk", "ra"]}
        self.unwrap_prev: Dict[str, float] = {k: 0.0 for k in ["rh", "rk", "ra"]}
        self.unwrap_inited: bool = False

        self._init_ui()

    def _init_ui(self) -> None:
        layout = QVBoxLayout(self)
        layout.setContentsMargins(2, 2, 2, 2)
        tl = QLabel("电机角度 (UDP 遥测)")
        tl.setAlignment(Qt.AlignCenter)
        tl.setFont(QFont("Arial", 11, QFont.Bold))
        tl.setStyleSheet(
            "background-color:#3D2D2D; color:#FFCC88; padding:4px; border-radius:4px;"
        )
        layout.addWidget(tl)

        self.right_plot = pg.PlotWidget()
        self.right_plot.setBackground("#1A1A2E")
        self.right_plot.showGrid(x=True, y=True, alpha=0.3)
        self.right_plot.setLabel("bottom", "时间", units="s")
        self.right_plot.setLabel("left", "角度", units="°")
        self.right_plot.setYRange(-180, 180)
        self.right_plot.getPlotItem().getAxis("left").setTicks(
            [[(y, str(y)) for y in range(180, -181, -45)]]
        )
        self.right_plot.enableAutoRange(axis="x", enable=True)
        self.right_plot.setXRange(0, 1)
        self.right_plot.addLegend(offset=(-10, 10))
        self.right_plot.setTitle("髋/膝/踝", color="#EEE")

        self.curves = {
            "rh": self.right_plot.plot([], [], pen=pg.mkPen(R_HIP, width=2), name="髋"),
            "rk": self.right_plot.plot([], [], pen=pg.mkPen(R_KNEE, width=2), name="膝"),
            "ra": self.right_plot.plot([], [], pen=pg.mkPen(R_ANKLE, width=2), name="踝"),
        }
        layout.addWidget(self.right_plot)

        self.val_labels: Dict[str, QLabel] = {}
        self.match_labels: Dict[str, QLabel] = {}
        self.match_widgets: Dict[str, QLabel] = {}
        info_grid = QGridLayout()
        info_grid.setSpacing(1)

        for col, (k, (name, color)) in enumerate(JOINT_DISPLAY.items()):
            lb = QLabel(f"{name}: ---")
            lb.setFont(QFont("Consolas", 8))
            lb.setStyleSheet(
                f"color:{color}; background-color:#1E1E30; "
                f"padding:1px 4px; border-radius:3px;"
            )
            lb.setMinimumWidth(100); lb.setAlignment(Qt.AlignCenter)
            self.val_labels[k] = lb; info_grid.addWidget(lb, 0, col)

            mlb = QLabel("---%")
            mlb.setFont(QFont("Consolas", 8))
            mlb.setStyleSheet(
                f"color:{color}; background-color:#1A1A28; "
                f"padding:1px 4px; border-radius:3px;"
            )
            mlb.setMinimumWidth(100); mlb.setAlignment(Qt.AlignCenter)
            self.match_labels[k] = mlb; self.match_widgets[k] = mlb
            info_grid.addWidget(mlb, 1, col)

        layout.addLayout(info_grid)

        self.rtt_row = QHBoxLayout()
        rt = QLabel("RTT")
        rt.setFont(QFont("Consolas", 8, QFont.Bold))
        rt.setStyleSheet(
            "color:#00FF88; background-color:#1E1E30; "
            "padding:1px 6px; border-radius:3px;"
        )
        rt.setFixedWidth(50); rt.setAlignment(Qt.AlignCenter)
        self.rtt_row.addWidget(rt)

        self.rtt_labels: Dict[str, QLabel] = {}
        for k, (_, color) in JOINT_DISPLAY.items():
            lb = QLabel("--- ms")
            lb.setFont(QFont("Consolas", 8))
            lb.setStyleSheet(
                f"color:{color}; background-color:#1A1A28; "
                f"padding:1px 4px; border-radius:3px;"
            )

    # ------------------------------------------------------------------
    # 数据接口
    # ------------------------------------------------------------------
    def add_data(self, r_hip: float, r_knee: float, r_ankle: float,
                 l_hip: float, l_knee: float, l_ankle: float,
                 timestamp: float) -> None:
        """追加一帧电机遥测数据。"""
        if self.start_timestamp is None:
            self.start_timestamp = timestamp
        for k, v in [("rh", r_hip), ("rk", r_knee), ("ra", r_ankle)]:
            if self.relative_mode:
                if not self.unwrap_inited:
                    self.unwrapped[k] = 0.0
                    self.unwrap_prev[k] = v
                else:
                    delta = v - self.unwrap_prev[k]
                    if delta > 180: delta -= 360
                    elif delta < -180: delta += 360
                    self.unwrapped[k] += delta
                    self.unwrap_prev[k] = v
                self.times[k].append(timestamp)
                self.vals[k].append(self.unwrapped[k])
            else:
                self.times[k].append(timestamp)
                self.vals[k].append(v)
            self.last_motor[k] = self.unwrapped[k] if self.relative_mode else v
        if self.relative_mode and not self.unwrap_inited:
            self.unwrap_inited = True

    def push_imu(self, joint: str, angle: float) -> None:
        """将 IMU 角度存入，用于计算采样点误差比。"""
        if joint not in ["rh", "rk", "ra"]: return
        self.last_imu[joint] = angle
        m = self.last_motor.get(joint, 0.0)
        if abs(m) > 1e-6:
            error_ratio = abs(m - angle) / abs(m)
        else:
            error_ratio = 0.0
        self.match_samples[joint].append(error_ratio)
        if not self.initialized:
            if all(len(self.match_samples[k]) > 0 for k in ["rh", "rk", "ra"]):
                self.initialized = True

            lb.setMinimumWidth(100); lb.setAlignment(Qt.AlignCenter)
            lb.setVisible(False)
            self.rtt_labels[k] = lb; self.rtt_row.addWidget(lb)

    # ------------------------------------------------------------------
    # 模式切换
    # ------------------------------------------------------------------
    def set_rtt_visible(self, visible: bool) -> None:
        for lb in self.rtt_labels.values():
            lb.setVisible(visible)

    def set_response_layout(self, response_active: bool) -> None:
        for w in self.match_widgets.values():
            w.setVisible(not response_active)

    def set_relative_mode(self, enabled: bool) -> None:
        self.relative_mode = enabled
        self.unwrap_inited = False
        for k in ["rh", "rk", "ra"]:
            self.unwrapped[k] = 0.0
            self.unwrap_prev[k] = 0.0
        if enabled:
            self.right_plot.setLabel("left", "累计角度", units="°")
            self.right_plot.setYRange(-360, 360)
        else:
            self.right_plot.setLabel("left", "角度", units="°")
            self.right_plot.setYRange(-180, 180)
            self.right_plot.getPlotItem().getAxis("left").setTicks(
                [[(y, str(y)) for y in range(180, -181, -45)]]
            )
        self.times = {k: deque() for k in ["rh", "rk", "ra"]}
        self.vals = {k: deque() for k in ["rh", "rk", "ra"]}
        self.start_timestamp = None
        for k in ["rh", "rk", "ra"]:
            self.match_samples[k].clear()
            self.last_motor[k] = 0.0; self.last_imu[k] = 0.0
        self.initialized = False

    # ------------------------------------------------------------------
    # 渲染与匹配度
    # ------------------------------------------------------------------
    def update_rtt(self, rh: float, rk: float, ra: float) -> None:
        for k, v in [("rh", rh), ("rk", rk), ("ra", ra)]:
            if k in self.rtt_labels:
                self.rtt_labels[k].setText(f"{v:.1f} ms")

    def update_match(self) -> None:
        """计算并更新匹配度百分比 (10 Hz 调用)。"""
        if not self.initialized: return
        for joint, lb in self.match_labels.items():
            samples = self.match_samples[joint]
            if len(samples) == 0:
                lb.setText("---%"); continue
            total = sum(samples)
            match = 1.0 - total / len(samples)
            match = max(0.0, min(100.0, match * 100.0))
            lb.setText(f"{match:.1f}%")

    def update_plot(self, current_time: float) -> None:
        """刷新曲线显示。"""
        if not self.times["rh"] or self.start_timestamp is None: return
        ce_max = 0.0
        for k in ["rh", "rk", "ra"]:
            rt = [t - self.start_timestamp for t in self.times[k]]
            self.curves[k].setData(rt, list(self.vals[k]))
            if rt: ce_max = max(ce_max, rt[-1])
        self.right_plot.setXRange(0, max(ce_max, 1), padding=0.02)
        if self.relative_mode:
            all_vals = [v for k in ["rh", "rk", "ra"] for v in self.vals[k]]
            if all_vals:
                ymin, ymax = min(all_vals), max(all_vals)
                pad = (ymax - ymin) * 0.1 + 10
                self.right_plot.setYRange(ymin - pad, ymax + pad)
        for k, lb in self.val_labels.items():
            if self.vals[k]:
                name = lb.text().split(":")[0]
                lb.setText(f"{name}: {self.vals[k][-1]:+7.2f}°")


        self.rtt_row.addStretch()
        layout.addLayout(self.rtt_row)
