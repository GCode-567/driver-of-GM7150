"""帧协议解析。

与下位机 Core/Src/video_stream.c 保持一致（小端）：
    0xA5 0x5A | type(1) flags(1) seq(2) w(2) h(2) len(4) hdr_crc(2)
    | payload(len) | crc(2)

两个 CRC 均为 CRC16-CCITT-FALSE（多项式 0x1021，初值 0xFFFF）。
"""

from dataclasses import dataclass

SYNC = b"\xA5\x5A"
HEADER_SIZE = 16
TRAILER_SIZE = 2
MAX_PAYLOAD = 4 * 1024 * 1024

TYPE_GRAY8 = 0x01


def crc16(data: bytes) -> int:
    """CRC16-CCITT-FALSE。"""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


@dataclass
class Frame:
    """一帧图像。payload 为逐行像素，长度 = w * h。"""
    seq: int
    width: int
    height: int
    payload: bytes
    type: int = TYPE_GRAY8


@dataclass
class Image:
    """送显的RGB图像（24bit，逐行，无填充）。"""
    width: int
    height: int
    rgb: bytes
    seq: int = 0
    info: str = ""


class PacketDecoder:
    """自定义分包模式：解出8bit灰度帧并转成RGB，供界面统一显示。

    适用于下位机无法直接送BT.656、先用分包转发灰度图的情况。
    """

    def __init__(self):
        self._parser = StreamParser()

    def feed(self, data: bytes):
        images = []
        for frame in self._parser.feed(data):
            if frame.type != TYPE_GRAY8:
                continue
            rgb = bytearray(len(frame.payload) * 3)
            rgb[0::3] = frame.payload
            rgb[1::3] = frame.payload
            rgb[2::3] = frame.payload
            images.append(Image(frame.width, frame.height, bytes(rgb),
                                frame.seq, "分包 灰度"))
        return images


class StreamParser:
    """增量解析字节流，自动重同步，容忍丢字节/半帧。"""

    def __init__(self):
        self._buf = bytearray()

    def reset(self):
        self._buf.clear()

    def feed(self, data: bytes):
        """喂入刚收到的字节，返回本次解析出的完整帧列表。"""
        self._buf += data
        if len(self._buf) > 16 * MAX_PAYLOAD:   # 防御：异常数据不无限堆积
            del self._buf[:-MAX_PAYLOAD]

        frames = []
        while True:
            frame = self._try_parse()
            if frame is None:
                break
            frames.append(frame)
        return frames

    def _try_parse(self):
        buf = self._buf
        idx = buf.find(SYNC)
        if idx < 0:
            # 末尾可能是半个同步字，保留1字节
            if len(buf) > 1:
                del buf[:-1]
            return None
        if idx > 0:
            del buf[:idx]

        if len(buf) < HEADER_SIZE:
            return None

        hdr = bytes(buf[:HEADER_SIZE])
        if crc16(hdr[2:14]) != int.from_bytes(hdr[14:16], "little"):
            del buf[:1]                      # 帧头不可信，滑过同步字继续找
            return None

        width = int.from_bytes(hdr[6:8], "little")
        height = int.from_bytes(hdr[8:10], "little")
        length = int.from_bytes(hdr[10:14], "little")
        if width == 0 or height == 0 or length != width * height or length > MAX_PAYLOAD:
            del buf[:1]
            return None

        total = HEADER_SIZE + length + TRAILER_SIZE
        if len(buf) < total:
            return None                      # 数据还没收全

        payload = bytes(buf[HEADER_SIZE:HEADER_SIZE + length])
        trailer = int.from_bytes(buf[HEADER_SIZE + length:total], "little")
        if crc16(payload) != trailer:
            del buf[:1]                      # 负载损坏，重新同步
            return None

        del buf[:total]
        return Frame(
            seq=int.from_bytes(hdr[4:6], "little"),
            width=width,
            height=height,
            payload=payload,
            type=hdr[2],
        )
