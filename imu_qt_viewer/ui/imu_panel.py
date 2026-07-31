# -*- coding: utf-8 -*-
"""
IMU 曲线面板 — 单个 IMU 模块的三轴角度实时曲线显示控件。
"""

from collections import deque
from typing import Optional

from PyQt5.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QLabel
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QFont

import pyqtgraph as pg

from ..config import CURVE_COLORS


class IMUPlotPanel(QWidget):
    """单个 IMU 的三轴角度曲线图。

    支持三种显示模式：
      - 普通模式: X/Y/Z 三轴角度显示
      - 相对值模式: 仅 X 轴累计值
      - 响应模式: 仅 X 轴 delta 值
    """

    def __init__(self, imu_addr: int, title: str, parent: Optional[QWidget] = None):
        super().__init__(parent)
        self.imu_addr: int = imu_addr
        self.title: str = title

        self.times_x: deque = deque()
        self.times_y: deque = deque()
        self.times_z: deque = deque()
        self.values_x: deque = deque()
        self.values_y: deque = deque()
        self.values_z: deque = deque()
        self.start_timestamp: Optional[float] = None

        self.response_mode: bool = False
        self.zero_x: float = 0.0

        self.relative_mode: bool = False
        self.unwrapped_x: float = 0.0
        self.prev_raw_x: float = 0.0
        self.unwrap_inited: bool = False

        self._init_ui()

    def _init_ui(self) -> None:
        layout = QVBoxLayout(self)
        layout.setContentsMargins(2, 2, 2, 2)
        self.title_label = QLabel(self.title)
        self.title_label.setAlignment(Qt.AlignCenter)
        self.title_label.setFont(QFont("Arial", 11, QFont.Bold))
        self.title_label.setStyleSheet(
            "background-color:#2D2D3D; color:#EEEEEE; padding:4px; border-radius:4px;"
        )
        layout.addWidget(self.title_label)
        self.plot_widget = pg.PlotWidget()
        self.plot_widget.setBackground("#1A1A2E")
        self.plot_widget.showGrid(x=True, y=True, alpha=0.3)
        self.plot_widget.setLabel("bottom", "时间", units="s")
        self.plot_widget.setLabel("left", "角度", units="°")
        self.plot_widget.setYRange(-180, 180)
        self.plot_widget.getPlotItem().getAxis("left").setTicks(
            [[(y, str(y)) for y in range(180, -181, -45)]]
        )
        self.plot_widget.enableAutoRange(axis="x", enable=True)
        self.plot_widget.setXRange(0, 1)
        self.plot_widget.addLegend(offset=(-10, 10))
        self.curve_x = self.plot_widget.plot(
            [], [], pen=pg.mkPen(CURVE_COLORS["X"], width=2), name="X轴"
        )
        self.curve_y = self.plot_widget.plot(
            [], [], pen=pg.mkPen(CURVE_COLORS["Y"], width=2), name="Y轴"
        )
        self.curve_z = self.plot_widget.plot(
            [], [], pen=pg.mkPen(CURVE_COLORS["Z"], width=2), name="Z轴"
        )
        layout.addWidget(self.plot_widget)
        vl = QHBoxLayout()
        self.vlx = QLabel("X: ---")
        self.vly = QLabel("Y: ---")
        self.vlz = QLabel("Z: ---")
        for lbl, color in [
            (self.vlx, "#FF4444"), (self.vly, "#44FF44"), (self.vlz, "#4488FF")
        ]:
            lbl.setFont(QFont("Consolas", 10))
            lbl.setStyleSheet(
                f"color:{color}; background-color:#1E1E30; padding:2px 8px; border-radius:3px;"
            )
            lbl.setMinimumWidth(130)
            lbl.setAlignment(Qt.AlignCenter)
            vl.addWidget(lbl)
        layout.addLayout(vl)

    # ------------------------------------------------------------------
    # 模式切换
    # ------------------------------------------------------------------
    def set_relative_mode(self, enabled: bool) -> None:
        """切换相对值模式。"""
        self.relative_mode = enabled
        self.unwrap_inited = False
        self.unwrapped_x = 0.0
        self.prev_raw_x = 0.0
        if enabled:
            self.curve_y.setVisible(False)
            self.curve_z.setVisible(False)
            self.plot_widget.setLabel("left", "累计角度", units="°")
            self.vly.setText("Y: ---")
            self.vlz.setText("Z: ---")
        else:
            self.curve_y.setVisible(True)
            self.curve_z.setVisible(True)
            self.plot_widget.setLabel("left", "角度", units="°")
            self.plot_widget.setYRange(-180, 180)
            self.plot_widget.getPlotItem().getAxis("left").setTicks(
                [[(y, str(y)) for y in range(180, -181, -45)]]
            )
            self.vly.setText("Y: ---")
            self.vlz.setText("Z: ---")
        self.times_x.clear(); self.times_y.clear(); self.times_z.clear()
        self.values_x.clear(); self.values_y.clear(); self.values_z.clear()
        self.start_timestamp = None

    def set_response_mode(self, enabled: bool) -> None:
        """切换响应模式。"""
        self.response_mode = enabled
        if enabled:
            if self.values_x: self.zero_x = self.values_x[-1]
            else: self.zero_x = 0.0
            self.title_label.setText(f"{self.title} [响应]")
            self.title_label.setStyleSheet(
                "background-color:#3D3D2D; color:#FFD700; padding:4px; border-radius:4px;"
            )
            self.curve_y.setVisible(False); self.curve_z.setVisible(False)
            self.plot_widget.setLabel("left", "Δ角度", units="°")
        else:
            self.zero_x = 0.0
            self.title_label.setText(self.title)
            self.title_label.setStyleSheet(
                "background-color:#2D2D3D; color:#EEEEEE; padding:4px; border-radius:4px;"
            )
            self.curve_y.setVisible(True); self.curve_z.setVisible(True)
            self.plot_widget.setLabel("left", "角度", units="°")
        self.vly.setText("Y: ---"); self.vlz.setText("Z: ---")
        self.times_x.clear(); self.times_y.clear(); self.times_z.clear()
        self.values_x.clear(); self.values_y.clear(); self.values_z.clear()
        self.start_timestamp = None

    # ------------------------------------------------------------------
    # 数据与渲染
    # ------------------------------------------------------------------
    def add_data(self, x_angle: float, y_angle: float, z_angle: float,
                 timestamp: float) -> None:
        """追加一帧 IMU 角度数据。"""
        if self.start_timestamp is None:
            self.start_timestamp = timestamp
        if self.relative_mode:
            if not self.unwrap_inited:
                self.unwrapped_x = 0.0
                self.prev_raw_x = x_angle
                self.unwrap_inited = True
            else:
                delta = x_angle - self.prev_raw_x
                if delta > 180: delta -= 360
                elif delta < -180: delta += 360
                self.unwrapped_x += delta
                self.prev_raw_x = x_angle
            self.times_x.append(timestamp)
            self.values_x.append(self.unwrapped_x)
        else:
            self.times_x.append(timestamp)
            self.values_x.append(x_angle)
        self.times_y.append(timestamp); self.times_z.append(timestamp)
        self.values_y.append(y_angle); self.values_z.append(z_angle)

    def update_plot(self, current_time: float) -> None:
        """刷新曲线显示。"""
        if not self.times_x or self.start_timestamp is None: return
        rx = [t - self.start_timestamp for t in self.times_x]
        ry = [t - self.start_timestamp for t in self.times_y]
        rz = [t - self.start_timestamp for t in self.times_z]
        ce = rx[-1] if rx else 0
        self.plot_widget.setXRange(0, max(ce, 1), padding=0.02)
        if self.relative_mode:
            self.curve_x.setData(rx, list(self.values_x))
            self.curve_y.setData([], []); self.curve_z.setData([], [])
            v = self.values_x[-1]
            self.vlx.setText(f"相对: {v:+7.2f}°")
            self.vly.setText("Y: ---"); self.vlz.setText("Z: ---")
            ymin = min(self.values_x) if self.values_x else -360
            ymax = max(self.values_x) if self.values_x else 360
            pad = (ymax - ymin) * 0.1 + 10
            self.plot_widget.setYRange(ymin - pad, ymax + pad)
        elif self.response_mode:
            dx = [v - self.zero_x for v in self.values_x]
            self.curve_x.setData(rx, dx)
            self.curve_y.setData([], []); self.curve_z.setData([], [])
            self.vlx.setText(f"ΔX: {dx[-1]:+7.2f}°")
        else:
            self.curve_x.setData(rx, list(self.values_x))
            self.curve_y.setData(ry, list(self.values_y))
            self.curve_z.setData(rz, list(self.values_z))
            self.vlx.setText(f"X: {self.values_x[-1]:+7.2f}°")
            self.vly.setText(f"Y: {self.values_y[-1]:+7.2f}°")
            self.vlz.setText(f"Z: {self.values_z[-1]:+7.2f}°")

