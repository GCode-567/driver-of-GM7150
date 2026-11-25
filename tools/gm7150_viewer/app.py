"""程序入口：解析参数、建立链路、启动界面。"""

import argparse
import queue
import sys

from . import link
from .bt656 import BT656Decoder
from .protocol import PacketDecoder
from .ui import ViewerWindow


def build_parser():
    ap = argparse.ArgumentParser(
        prog="gm7150_viewer",
        description="GM7150 图像/视频接收显示（默认按BT.656解码，不带参数时自动选串口）")
    ap.add_argument("port", nargs="?", default=None, help="串口号，如 COM3；省略则自动选择")
    ap.add_argument("baud", nargs="?", type=int, default=921600,
                    help="波特率，默认 921600（与固件 usart.c 一致）")
    ap.add_argument("--mode", choices=("bt656", "packet"), default="bt656",
                    help="bt656=裸BT.656码流（默认）；packet=自定义分包灰度")
    ap.add_argument("--limited", action="store_true",
                    help="BT.656按BT.601有限范围(16~235)解码；默认按扩展范围(0~255)")
    ap.add_argument("--save", default=None, help="按“保存帧”时默认存到该目录")
    ap.add_argument("--list", action="store_true", help="列出可用串口后退出")
    return ap


def main(argv=None):
    args = build_parser().parse_args(argv)

    if args.list:
        ports = link.list_serial_ports()
        if not ports:
            print("未检测到串口")
        for p in ports:
            print(f"{p.device}  {p.description}")
        return 0

    try:
        port = args.port or link.pick_serial_port()
    except RuntimeError as exc:
        print(exc, file=sys.stderr)
        return 1

    if args.save:
        import os
        os.makedirs(args.save, exist_ok=True)

    frame_queue = queue.Queue()
    decoder = BT656Decoder(limited_range=args.limited) if args.mode == "bt656" else PacketDecoder()
    try:
        reader = link.SerialLink(port, args.baud, frame_queue, decoder)
    except Exception as exc:
        print(f"打开串口 {port} 失败：{exc}", file=sys.stderr)
        return 1

    reader.start()
    print(f"已打开 {reader.description}，解码模式 {args.mode}，等待数据...")

    window = ViewerWindow(frame_queue, reader.description, args.save, reader=reader)
    try:
        window.mainloop()
    finally:
        reader.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
