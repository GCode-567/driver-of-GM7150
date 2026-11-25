"""数据链路层：从物理接口读取字节流，解析成帧后投递到队列。

当前只实现串口（UART/USB-TTL）。后续若改用 USB CDC 或网络，
只需新增一个类，保持 start()/stop()/stats() 接口一致即可，UI 无需改动。
"""

import queue
import threading

import serial
from serial.tools import list_ports


def list_serial_ports():
    return list(list_ports.comports())


def pick_serial_port():
    """未指定串口时自动选择；多个则列出让人指定。"""
    ports = list_serial_ports()
    if not ports:
        raise RuntimeError("未检测到串口，请检查 USB-TTL 是否插好并安装驱动")
    if len(ports) == 1:
        print(f"自动选择串口：{ports[0].device} ({ports[0].description})")
        return ports[0].device
    print("检测到多个串口，请指定其中一个：")
    for p in ports:
        print(f"  {p.device}  {p.description}")
    raise RuntimeError("请用参数指定串口，例如：python -m gm7150_viewer COM3 115200")


class SerialLink(threading.Thread):
    """串口接收线程：读字节 -> 解码器 -> 放入队列（队列满时丢旧帧）。

    decoder 只需实现 feed(bytes) -> list[Image]：
        BT656Decoder    （裸BT.656码流，默认）
        PacketDecoder   （自定义分包，灰度）
    """

    def __init__(self, port: str, baud: int, frame_queue: "queue.Queue",
                 decoder, max_queued: int = 3):
        super().__init__(daemon=True)
        self._ser = serial.Serial(port, baud, timeout=0.5)
        self._decoder = decoder
        self._queue = frame_queue
        self._max_queued = max_queued
        self._running = True
        self.rx_bytes = 0
        self.dropped = 0
        self._lock = threading.Lock()
        self._tail = bytearray()          # 最近收到的原始字节，供界面显示

    @property
    def description(self):
        return f"{self._ser.port} @ {self._ser.baudrate}"

    def raw_tail(self, n: int = 288) -> bytes:
        """取最近 n 个原始字节（供串口数据监视用）。"""
        with self._lock:
            return bytes(self._tail[-n:])

    def run(self):
        while self._running:
            try:
                data = self._ser.read(4096)
            except (serial.SerialException, OSError):
                break
            if not data:
                continue
            self.rx_bytes += len(data)
            with self._lock:
                self._tail += data
                if len(self._tail) > 2048:
                    del self._tail[:-1024]
            for image in self._decoder.feed(data):
                self._put(image)

    def _put(self, image):
        while self._queue.qsize() > self._max_queued:   # 处理不过来就丢旧帧
            try:
                self._queue.get_nowait()
                self.dropped += 1
            except queue.Empty:
                break
        self._queue.put(image)

    def stop(self):
        self._running = False
        try:
            self._ser.close()
        except Exception:
            pass
