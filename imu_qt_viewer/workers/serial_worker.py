# -*- coding: utf-8 -*-
"""
串口通信工作线程 — 负责接收 IMU 角度数据。
"""

import time
from typing import Optional

import serial
from PyQt5.QtCore import QThread, pyqtSignal

from ..protocol.at_protocol import find_at_packet, parse_angle_data
from ..config import AT_STARTUP_COMMANDS, SERIAL_TIMEOUT


class SerialReaderThread(QThread):
    """串口读取线程，持续接收并解析 AT 协议 IMU 数据。"""

    data_ready = pyqtSignal(dict)
    status_update = pyqtSignal(str)
    error_occurred = pyqtSignal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.ser: Optional[serial.Serial] = None
        self.running: bool = False
        self.port: Optional[str] = None
        self.baudrate: int = 2000000

    def set_params(self, port: str, baudrate: int) -> None:
        """设置串口参数。"""
        self.port = port
        self.baudrate = baudrate

    def stop(self) -> None:
        """停止线程。"""
        self.running = False

    def run(self) -> None:
        """线程主循环。"""
        self.running = True
        buffer = bytearray()

        try:
            self.status_update.emit(f"串口: 正在打开 {self.port}")
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=SERIAL_TIMEOUT,
            )

            if not self.ser.is_open:
                self.error_occurred.emit(f"串口无法打开 {self.port}")
                return

            # 发送启动指令，切换 IMU 到 AT 协议模式
            self.status_update.emit(f"串口 {self.port} 已打开，发送启动指令...")
            for cmd in AT_STARTUP_COMMANDS:
                try:
                    self.ser.write(cmd)
                    time.sleep(0.05)
                except Exception:
                    pass

            self.status_update.emit(f"串口 {self.port} 启动指令已发送，等待 IMU 数据...")

            while self.running:
                try:
                    data = self.ser.read(1024)
                    if data:
                        buffer.extend(data)
                        while True:
                            packet, buffer = find_at_packet(buffer)
                            if packet is None:
                                break
                            parsed = parse_angle_data(packet)
                            if parsed is None:
                                continue
                            self.data_ready.emit({
                                parsed["imu_addr"]: {
                                    "x": parsed["x_angle"],
                                    "y": parsed["y_angle"],
                                    "z": parsed["z_angle"],
                                    "time": time.time(),
                                }
                            })
                    else:
                        time.sleep(0.001)
                except serial.SerialException as e:
                    self.error_occurred.emit(f"串口错误: {e}")
                    break

        except serial.SerialException as e:
            self.error_occurred.emit(f"串口错误: {e}")
        finally:
            if self.ser and self.ser.is_open:
                self.ser.close()
            self.status_update.emit("串口已关闭")
