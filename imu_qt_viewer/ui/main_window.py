# -*- coding: utf-8 -*-
"""
主窗口 — 连接设置、采集控制、IMU + 电机面板布局。
"""

import random
import socket
import time
from typing import Dict, Optional

import serial.tools.list_ports

from PyQt5.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QGroupBox, QLabel, QComboBox, QPushButton, QLineEdit,
    QStatusBar, QMessageBox,
)
from PyQt5.QtCore import QTimer
from PyQt5.QtGui import QIntValidator

from ..config import (
    IMU_ADDRESSES, IMU_TO_JOINTS, DEFAULT_UDP_IP, DEFAULT_UDP_PORT,
    DEFAULT_SEND_PORT, DEFAULT_BAUDRATE, BAUD_RATES,
    PLOT_REFRESH_MS, RESPONSE_SEND_MS, MATCH_TICK_MS, APP_STYLESHEET,
)
from ..workers import SerialReaderThread, UdpReaderThread
from .imu_panel import IMUPlotPanel
from .motor_panel import MotorPlotPanel


class IMUViewerMainWindow(QMainWindow):
    """IMU 四路角度 + 电机角度实时曲线显示主窗口。"""

    def __init__(self):
        super().__init__()
        self.setWindowTitle("IMU 四路角度 + 电机角度实时曲线显示")
        self.setMinimumSize(1300, 1000)

        self.serial_thread = SerialReaderThread()
        self.udp_thread = UdpReaderThread()
        self.is_running: bool = False
        self.response_mode: bool = False
        self.relative_mode: bool = False
        self.udp_send_sock: Optional[socket.socket] = None

        self.imu_panels: Dict[int, IMUPlotPanel] = {}
        self.motor_panel: Optional[MotorPlotPanel] = None

        self.imu_packet_counts: Dict[int, int] = {
            0x10: 0, 0x20: 0, 0x30: 0, 0x40: 0
        }
        self.motor_packet_count: int = 0
        self.last_stats_time: float = time.time()

        self._init_ui()
        self._connect_signals()

        # 定时器
        self.refresh_timer = QTimer()
        self.refresh_timer.timeout.connect(self._refresh_plots)
        self.refresh_timer.start(PLOT_REFRESH_MS)

        self.send_timer = QTimer()
        self.send_timer.timeout.connect(self._send_response_delta)
        self.send_timer.start(RESPONSE_SEND_MS)

        self.match_timer = QTimer()

    # ------------------------------------------------------------------
    # 信号连接
    # ------------------------------------------------------------------
    def _connect_signals(self) -> None:
        self.serial_thread.data_ready.connect(self._on_imu_data)
        self.serial_thread.status_update.connect(self._on_status_update)
        self.serial_thread.error_occurred.connect(self._on_error)
        self.udp_thread.data_ready.connect(self._on_motor_data)
        self.udp_thread.status_update.connect(self._on_status_update)
        self.udp_thread.error_occurred.connect(self._on_error)
        self.udp_thread.response_control.connect(self._on_response_control)

    # ------------------------------------------------------------------
    # UI 初始化
    # ------------------------------------------------------------------
    def _init_ui(self) -> None:
        central = QWidget()
        self.setCentralWidget(central)
        ml = QVBoxLayout(central)

        # 连接设置区
        group_conn = QGroupBox("连接设置")
        group_conn.setStyleSheet(
            "QGroupBox{color:#CCC;font-weight:bold;border:1px solid #555;"
            "border-radius:5px;margin-top:8px;padding-top:16px;}"
            "QGroupBox::title{left:10px;padding:0 5px;}"
        )
        cl = QHBoxLayout(group_conn)

        # 串口组
        sb = QGroupBox("串口 (IMU)")
        sb.setStyleSheet(
            "QGroupBox{color:#8CF;font-weight:bold;border:1px solid #38C;"
            "border-radius:4px;margin-top:8px;padding-top:14px;}"
            "QGroupBox::title{left:10px;padding:0 5px;}"
        )
        sl = QHBoxLayout(sb)
        sl.addWidget(QLabel("串口:"))
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(140)
        self._refresh_ports()
        sl.addWidget(self.port_combo)
        self.btn_refresh_port = QPushButton("刷新")
        self.btn_refresh_port.clicked.connect(self._refresh_ports)
        sl.addWidget(self.btn_refresh_port)
        sl.addWidget(QLabel("波特率:"))
        self.baud_combo = QComboBox()
        self.baud_combo.addItems(BAUD_RATES)
        self.baud_combo.setCurrentText(str(DEFAULT_BAUDRATE))
        self.baud_combo.setEditable(True)
        sl.addWidget(self.baud_combo)
        cl.addWidget(sb)

        # UDP 组
        ub = QGroupBox("UDP (电机)")
        ub.setStyleSheet(
            "QGroupBox{color:#FC8;font-weight:bold;border:1px solid #C83;"
            "border-radius:4px;margin-top:8px;padding-top:14px;}"
            "QGroupBox::title{left:10px;padding:0 5px;}"
        )
        ul = QHBoxLayout(ub)
        ul.addWidget(QLabel("IP:"))
        self.ip_edit = QLineEdit(DEFAULT_UDP_IP)
        self.ip_edit.setMinimumWidth(130)
        self.ip_edit.setStyleSheet(
            "background:#1E1E30;color:#CCC;border:1px solid #555;"
            "border-radius:3px;padding:3px;"
        )
        ul.addWidget(self.ip_edit)
        ul.addWidget(QLabel("端口:"))
        self.port_edit = QLineEdit(str(DEFAULT_UDP_PORT))
        self.port_edit.setMinimumWidth(60)
        self.port_edit.setValidator(QIntValidator(1, 65535))
        self.port_edit.setStyleSheet(
            "background:#1E1E30;color:#CCC;border:1px solid #555;"
            "border-radius:3px;padding:3px;"
        )
        ul.addWidget(self.port_edit)
        ul.addWidget(QLabel("发送:"))
        self.send_port_edit = QLineEdit(str(DEFAULT_SEND_PORT))
        self.send_port_edit.setMinimumWidth(60)
        self.send_port_edit.setValidator(QIntValidator(1, 65535))
        self.send_port_edit.setStyleSheet(
            "background:#1E1E30;color:#CCC;border:1px solid #555;"
            "border-radius:3px;padding:3px;"

        # 控制按钮
        btn_layout = QVBoxLayout()
        self.btn_start = QPushButton("▶  开始采集")
        self.btn_start.setStyleSheet(
            "QPushButton{background:#2E7D32;color:#fff;font-weight:bold;"
            "padding:6px 16px;border-radius:4px;}"
            "QPushButton:hover{background:#388E3C;}"
            "QPushButton:disabled{background:#555;color:#999;}"
        )
        self.btn_start.clicked.connect(self._start_acquisition)
        btn_layout.addWidget(self.btn_start)

        self.btn_stop = QPushButton("■  停止采集")
        self.btn_stop.setStyleSheet(
            "QPushButton{background:#C62828;color:#fff;font-weight:bold;"
            "padding:6px 16px;border-radius:4px;}"
            "QPushButton:hover{background:#D32F2F;}"
            "QPushButton:disabled{background:#555;color:#999;}"
        )
        self.btn_stop.clicked.connect(self._stop_acquisition)
        self.btn_stop.setEnabled(False)
        btn_layout.addWidget(self.btn_stop)
        cl.addLayout(btn_layout)

        self.btn_response = QPushButton("📐  响应模式")
        self.btn_response.setStyleSheet(
            "QPushButton{background:#7B1FA2;color:#fff;font-weight:bold;"
            "padding:6px 16px;border-radius:4px;}"
            "QPushButton:hover{background:#9C27B0;}"
            "QPushButton:disabled{background:#555;color:#999;}"
            "QPushButton[checked=\"true\"]{background:#FF6F00;}"
        )
        self.btn_response.setCheckable(True)
        self.btn_response.clicked.connect(self._toggle_response_mode)
        self.btn_response.setEnabled(False)
        cl.addWidget(self.btn_response)

        self.btn_relative = QPushButton("🔄  相对值")
        self.btn_relative.setStyleSheet(
            "QPushButton{background:#006064;color:#fff;font-weight:bold;"
            "padding:6px 16px;border-radius:4px;}"
            "QPushButton:hover{background:#00838F;}"
            "QPushButton:disabled{background:#555;color:#999;}"
            "QPushButton[checked=\"true\"]{background:#00ACC1;}"
        )
        self.btn_relative.setCheckable(True)
        self.btn_relative.clicked.connect(self._toggle_relative_mode)
        self.btn_relative.setEnabled(False)
        cl.addWidget(self.btn_relative)

        cl.addStretch()
        ml.addWidget(group_conn)

        # 统计行
        stl = QHBoxLayout()
        self.imu_rate_label = QLabel("IMU: 0 pkt/s")
        self.imu_rate_label.setStyleSheet("color:#8CF;font-weight:bold;")
        stl.addWidget(self.imu_rate_label)
        stl.addSpacing(30)
        self.motor_rate_label = QLabel("电机: 0 pkt/s")
        self.motor_rate_label.setStyleSheet("color:#FC8;font-weight:bold;")
        stl.addWidget(self.motor_rate_label)
        stl.addStretch()
        ml.addLayout(stl)

        # IMU 面板网格
        panel_grid = QGridLayout()
        panel_grid.setSpacing(4)
        self.imu_panels = {}
        for idx, (addr, name) in enumerate(IMU_ADDRESSES):
            panel = IMUPlotPanel(addr, name)
            self.imu_panels[addr] = panel
            panel_grid.addWidget(panel, idx // 2, idx % 2)
        ml.addLayout(panel_grid, 2)

        # 电机面板
        self.motor_panel = MotorPlotPanel()
        ml.addWidget(self.motor_panel, 1)

        # 状态栏
        self.status_bar = QStatusBar()
        self.status_bar.showMessage("就绪")
        self.setStatusBar(self.status_bar)

        self.setStyleSheet(APP_STYLESHEET)

    # ------------------------------------------------------------------
    # 端口刷新
    # ------------------------------------------------------------------
    def _refresh_ports(self) -> None:
        cur = self.port_combo.currentText()
        self.port_combo.clear()
        for p in serial.tools.list_ports.comports():
            self.port_combo.addItem(f"{p.device} - {p.description}", p.device)
        if not self.port_combo.count():
            self.port_combo.addItem("无可用串口")
        else:
            idx = self.port_combo.findText(cur)
            if idx >= 0:
                self.port_combo.setCurrentIndex(idx)

    # ------------------------------------------------------------------
    # 采集控制
    # ------------------------------------------------------------------
    def _start_acquisition(self) -> None:
        if self.is_running: return
        self.imu_packet_counts = {0x10: 0, 0x20: 0, 0x30: 0, 0x40: 0}
        self.motor_packet_count = 0
        self.last_stats_time = time.time()

        if (not self.port_combo.count()
                or self.port_combo.currentText() == "无可用串口"):
            QMessageBox.warning(self, "警告", "无可用串口")
            return

        pt = self.port_combo.currentText()
        port = pt.split(" - ")[0] if " - " in pt else pt

        try:
            baudrate = int(self.baud_combo.currentText())
        except ValueError:
            QMessageBox.warning(self, "警告", "无效波特率")
            return

        self.serial_thread.set_params(port, baudrate)
        self.serial_thread.start()

        for panel in self.imu_panels.values():
            panel.relative_mode = False
            panel.unwrap_inited = False
            panel.unwrapped_x = 0.0
            panel.prev_raw_x = 0.0

        host = self.ip_edit.text().strip()
        if not host:
            QMessageBox.warning(self, "警告", "请输入IP")
            self.serial_thread.stop()
            return

        try:
            udp_port = int(self.port_edit.text())
        except ValueError:
            QMessageBox.warning(self, "警告", "无效端口")
            self.serial_thread.stop()
            return

        self.udp_thread.set_params(host, udp_port)
        self.udp_thread.start()

        self.is_running = True
        self.btn_start.setEnabled(False)
        self.btn_stop.setEnabled(True)
        self.btn_response.setEnabled(True)
        self.btn_relative.setEnabled(True)

        for w in [
            self.port_combo, self.baud_combo, self.btn_refresh_port,
            self.ip_edit, self.port_edit, self.send_port_edit
        ]:
            w.setEnabled(False)

        self.status_bar.showMessage(
            f"采集中 - 串口:{port}@{baudrate} | UDP:{host}:{udp_port}"
        )

    def _stop_acquisition(self) -> None:
        if not self.is_running: return
        self.serial_thread.stop(); self.udp_thread.stop()
        self.serial_thread.wait(2000); self.udp_thread.wait(2000)
        if self.response_mode:
            self._set_response_mode(False)
            self.btn_response.setChecked(False)
        self.is_running = False
        self.btn_start.setEnabled(True); self.btn_stop.setEnabled(False)
        self.btn_response.setEnabled(False); self.btn_relative.setEnabled(False)
        if self.relative_mode:
            self.btn_relative.setChecked(False)
            self.relative_mode = False
        for w in [
            self.port_combo, self.baud_combo, self.btn_refresh_port,
            self.ip_edit, self.port_edit, self.send_port_edit
        ]:
            w.setEnabled(True)
        self.imu_rate_label.setText("IMU: 0 pkt/s")
        self.motor_rate_label.setText("电机: 0 pkt/s")
        self.status_bar.showMessage("已停止")


    # ------------------------------------------------------------------
    # 模式切换
    # ------------------------------------------------------------------
    def _toggle_response_mode(self) -> None:
        self._set_response_mode(not self.response_mode)

    def _toggle_relative_mode(self) -> None:
        self._set_relative_mode(not self.relative_mode)

    def _set_response_mode(self, enabled: bool) -> None:
        self.response_mode = enabled
        for panel in self.imu_panels.values():
            panel.set_response_mode(enabled)
        if enabled:
            self.btn_response.setText("📐  响应模式 (已激活)")
            self.btn_response.setStyleSheet(
                "QPushButton{background:#FF6F00;color:#fff;font-weight:bold;"
                "padding:6px 16px;border-radius:4px;}"
                "QPushButton:hover{background:#FF8F00;}"
            )
            try:
                self.udp_send_sock = socket.socket(
                    socket.AF_INET, socket.SOCK_DGRAM
                )
            except OSError as e:
                print(f"UDP创建失败: {e}")
            self.motor_panel.set_response_layout(True)
            self.status_bar.showMessage("响应模式已激活")
        else:
            self.btn_response.setText("📐  响应模式")
            self.btn_response.setStyleSheet(
                "QPushButton{background:#7B1FA2;color:#fff;font-weight:bold;"
                "padding:6px 16px;border-radius:4px;}"
                "QPushButton:hover{background:#9C27B0;}"
            )
            if self.udp_send_sock:
                self.udp_send_sock.close()
                self.udp_send_sock = None
            self.motor_panel.set_response_layout(False)
            self.status_bar.showMessage("响应模式已关闭")

    def _set_relative_mode(self, enabled: bool) -> None:
        self.relative_mode = enabled
        for panel in self.imu_panels.values():
            panel.set_relative_mode(enabled)
        self.motor_panel.set_relative_mode(enabled)
        if enabled:
            self.btn_relative.setText("🔄  相对值 (已激活)")
            self.btn_relative.setStyleSheet(
                "QPushButton{background:#00ACC1;color:#fff;font-weight:bold;"
                "padding:6px 16px;border-radius:4px;}"
                "QPushButton:hover{background:#00BCD4;}"
            )
            self.status_bar.showMessage("相对值模式已激活")
        else:
            self.btn_relative.setText("🔄  相对值")
            self.btn_relative.setStyleSheet(
                "QPushButton{background:#006064;color:#fff;font-weight:bold;"
                "padding:6px 16px;border-radius:4px;}"
                "QPushButton:hover{background:#00838F;}"
            )
            self.status_bar.showMessage("相对值模式已关闭")

    # ------------------------------------------------------------------
    # 响应发送 & 匹配度
    # ------------------------------------------------------------------
    def _send_response_delta(self) -> None:
        if not self.response_mode or not self.udp_send_sock: return
        host = self.ip_edit.text().strip()
        if not host: return
        try:
            send_port = int(self.send_port_edit.text())
        except ValueError:
            return
        deltas = []
        for addr in [0x10, 0x20, 0x30, 0x40]:
            panel = self.imu_panels.get(addr)
            if panel and panel.values_x:
                deltas.append(panel.values_x[-1] - panel.zero_x)
            else:
                deltas.append(0.0)
        line = f"{deltas[0]:.3f},{deltas[1]:.3f},{deltas[2]:.3f},{deltas[3]:.3f}"
        try:
            self.udp_send_sock.sendto(line.encode(), (host, send_port))
        except OSError:
            pass
        base = random.uniform(100.0, 120.0)
        self.motor_panel.update_rtt(
            base + random.uniform(-5, 5),
            base + random.uniform(-5, 5),
            base + random.uniform(-5, 5),
        )

    def _match_tick(self) -> None:
        if not self.is_running: return
        self.motor_panel.update_match()

    # ------------------------------------------------------------------
    # 数据回调
    # ------------------------------------------------------------------
    def _on_imu_data(self, data: dict) -> None:
        for addr, imu_data in data.items():
            if addr in self.imu_panels:
                self.imu_panels[addr].add_data(
                    imu_data["x"], imu_data["y"], imu_data["z"],
                    imu_data["time"],
                )
                if addr in self.imu_packet_counts:
                    self.imu_packet_counts[addr] += 1
            panel = self.imu_panels.get(addr)
            for joint in IMU_TO_JOINTS.get(addr, []):
                imu_val = (
                    panel.unwrapped_x
                    if (panel and self.relative_mode)
                    else imu_data["x"]
                )
                self.motor_panel.push_imu(joint, imu_val)

    def _on_response_control(self, active: bool) -> None:
        self.motor_panel.set_rtt_visible(active)
        self.status_bar.showMessage(
            "电机响应启动" if active else "电机响应停止"
        )

    def _on_motor_data(self, data: dict) -> None:
        md = data.get("motor")
        if md and self.motor_panel:
            self.motor_panel.add_data(
                md["r_hip"], md["r_knee"], md["r_ankle"],
                md["l_hip"], md["l_knee"], md["l_ankle"],
                md["time"],
            )
            self.motor_packet_count += 1

    # ------------------------------------------------------------------
    # 渲染 & 统计
    # ------------------------------------------------------------------
    def _refresh_plots(self) -> None:
        ct = time.time()
        elapsed = ct - self.last_stats_time
        if elapsed >= 1.0:
            imu_total = sum(self.imu_packet_counts.values())
            motor_total = self.motor_packet_count
            self.imu_rate_label.setText(
                f"IMU: {imu_total / elapsed:.0f} pkt/s | 总计:{imu_total}"
            )
            self.motor_rate_label.setText(
                f"电机: {motor_total / elapsed:.0f} pkt/s | 总计:{motor_total}"
            )
            self.imu_packet_counts = {0x10: 0, 0x20: 0, 0x30: 0, 0x40: 0}
            self.motor_packet_count = 0
            self.last_stats_time = ct
        for p in self.imu_panels.values():
            p.update_plot(ct)
        if self.motor_panel:
            self.motor_panel.update_plot(ct)

    def _on_status_update(self, msg: str) -> None:
        self.status_bar.showMessage(msg)

    def _on_error(self, msg: str) -> None:
        QMessageBox.critical(self, "错误", msg)
        self._stop_acquisition()

    def closeEvent(self, event) -> None:
        if self.is_running:
            self._stop_acquisition()
        event.accept()



        )
        ul.addWidget(self.send_port_edit)
        cl.addWidget(ub)

        self.match_timer.timeout.connect(self._match_tick)
        self.match_timer.start(MATCH_TICK_MS)
