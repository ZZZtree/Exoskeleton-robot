# -*- coding: utf-8 -*-
"""
UDP 通信工作线程 — 负责接收电机遥测数据及响应控制指令。
"""

import socket
import time
from typing import Optional

from PyQt5.QtCore import QThread, pyqtSignal

from ..protocol.motor_csv import parse_motor_csv
from ..config import UDP_TIMEOUT, UDP_BUFFER_SIZE


class UdpReaderThread(QThread):
    """UDP 读取线程，持续接收电机遥测 CSV 数据。"""

    data_ready = pyqtSignal(dict)
    status_update = pyqtSignal(str)
    error_occurred = pyqtSignal(str)
    response_control = pyqtSignal(bool)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.sock: Optional[socket.socket] = None
        self.running: bool = False
        self.host: Optional[str] = None
        self.port: Optional[int] = None

    def set_params(self, host: str, port: int) -> None:
        """设置 UDP 参数。"""
        self.host = host
        self.port = port

    def stop(self) -> None:
        """停止线程。"""
        self.running = False

    def run(self) -> None:
        """线程主循环。"""
        self.running = True

        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.settimeout(UDP_TIMEOUT)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.sock.bind(("", self.port))
            self.status_update.emit(f"UDP 端口 {self.port} 已绑定")

            while self.running:
                try:
                    data, addr = self.sock.recvfrom(UDP_BUFFER_SIZE)
                    if not data:
                        continue

                    stripped = data.strip()
                    if stripped == b"start":
                        self.response_control.emit(True)
                        continue
                    elif stripped == b"stop":
                        self.response_control.emit(False)
                        continue

                    for line in data.split(b"\n"):
                        parsed = parse_motor_csv(line)
                        if parsed is None:
                            continue
                        self.data_ready.emit({
                            "motor": {
                                "r_hip": parsed["r_hip"],
                                "r_knee": parsed["r_knee"],
                                "r_ankle": parsed["r_ankle"],
                                "l_hip": parsed["l_hip"],
                                "l_knee": parsed["l_knee"],
                                "l_ankle": parsed["l_ankle"],
                                "time": time.time(),
                            }
                        })

                except socket.timeout:
                    continue
                except OSError as e:
                    if self.running:
                        self.error_occurred.emit(f"UDP 错误: {e}")
                    break

        except OSError as e:
            self.error_occurred.emit(f"UDP 绑定失败: {e}")
        finally:
            if self.sock:
                self.sock.close()
            self.status_update.emit("UDP 已关闭")
