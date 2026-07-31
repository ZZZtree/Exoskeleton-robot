# IMU Viewer — 外骨骼机器人实时数据可视化系统

四路 IMU 角度 + 电机遥测实时曲线显示，支持响应模式与相对值模式。

## 项目结构

```
imu_qt_viewer/
├── __init__.py          # 包版本信息
├── __main__.py          # python -m 入口
├── main.py              # main() 启动函数
├── config.py            # 全局配置常量
├── protocol/            # 协议解析层
│   ├── at_protocol.py   # AT 协议解析
│   └── motor_csv.py     # 电机 CSV 协议解析
├── workers/             # 通信线程层
│   ├── serial_worker.py # 串口通信线程
│   └── udp_worker.py    # UDP 通信线程
├── ui/                  # 界面层
│   ├── main_window.py   # 主窗口
│   ├── imu_panel.py     # IMU 曲线面板
│   └── motor_panel.py   # 电机曲线面板
├── requirements.txt     # Python 依赖
└── README.md
```

## 架构设计

- **协议层 (protocol/)** — 纯数据解析，无状态，可独立单元测试
- **通信层 (workers/)** — QThread 封装，通过信号与 UI 解耦
- **界面层 (ui/)** — PyQt5 + pyqtgraph 实时渲染，模式切换

## 快速开始

```bash
pip install -r requirements.txt
python -m imu_qt_viewer
```

## 数据流

```
串口 → SerialReaderThread → AT协议解析 → IMU数据 → IMUPlotPanel (4路)
UDP  → UdpReaderThread    → CSV解析   → 电机数据 → MotorPlotPanel
                                              ↑
                              IMU_TO_JOINTS ─┘  匹配度计算
```
