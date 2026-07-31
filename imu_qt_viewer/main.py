# -*- coding: utf-8 -*-
"""
应用入口 — 创建 QApplication 并启动主窗口。
"""

import sys

from PyQt5.QtWidgets import QApplication
from PyQt5.QtGui import QFont

from .ui.main_window import IMUViewerMainWindow


def main() -> None:
    """应用主入口函数。"""
    app = QApplication(sys.argv)
    app.setStyle("Fusion")
    app.setFont(QFont("Arial", 9))

    window = IMUViewerMainWindow()
    window.show()

    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
