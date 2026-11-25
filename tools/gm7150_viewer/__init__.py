"""GM7150 上位机：接收单片机图像/视频数据并显示。

模块划分：
    protocol.py  帧协议解析（与下位机 video_stream.c 对应）
    link.py      数据链路（当前 UART，后续可扩展 USB/网络）
    ui.py        显示界面（Tkinter）
    app.py       命令行入口与模块装配

直接运行：
    python -m gm7150_viewer            # 自动选串口，115200
    python tools/serial_viewer.py      # 兼容旧入口
"""

from .app import main

__all__ = ["main"]
