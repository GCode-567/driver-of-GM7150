"""BT.656 解码：裸字节流 -> RGB 图像（按场输出）。

GM7150 输出的是 8bit 4:2:2 YCbCr，内嵌同步：
    SAV/EAV 标志 = FF 00 00 XY
      XY: bit4 = H (0=SAV, 1=EAV)
          bit5 = F (场标志，隔行时每场翻转)
          bit6 = V (1=场消隐，0=有效视频)
          bit7 = 1
      bit3..0 = 保护位，8bit 模式下为 0

有效数据按 4 字节一组排列：Cb Y0 Cr Y1（两个像素共用一组色度）。
每一行有效数据从 SAV(V=0) 之后开始，到 EAV(V=0) 之前结束。

解码器不依赖外层分包：靠 SAV/EAV 自我同步，丢字节后在下一条同步码自动恢复。
"""

import numpy as np

from .protocol import Image

_SYNC = b"\xff\x00\x00"
_MAX_LINES = 1400


class BT656Decoder:
    """增量式 BT.656 解码器，每次 feed() 返回本次解出的场图像列表。"""

    def __init__(self, limited_range: bool = False):
        self.limited_range = limited_range
        self._buf = bytearray()
        self._line = None            # 当前有效行的字节缓冲，None 表示不在有效行内
        self._field_flag = 0
        self._lines = []
        self._seq = 0
        self.dropped_lines = 0

    def feed(self, data: bytes):
        self._buf += data
        if len(self._buf) > 16 * 1024 * 1024:          # 防御性上限
            del self._buf[:8 * 1024 * 1024]

        images = []
        buf = self._buf
        pos = 0
        while True:
            idx = buf.find(_SYNC, pos)
            if idx < 0:
                cut = max(pos, len(buf) - 3)           # 末尾3字节可能是半个同步码
                if self._line is not None:
                    self._line += buf[pos:cut]
                pos = cut
                break
            if idx + 4 > len(buf):
                if self._line is not None:
                    self._line += buf[pos:idx]
                pos = idx
                break

            if self._line is not None and idx > pos:   # 同步码之前的都是行数据
                self._line += buf[pos:idx]

            xy = buf[idx + 3]
            if xy & 0x0F:                              # 不是合法 SAV/EAV，滑过
                pos = idx + 1
                continue

            is_eav = bool(xy & 0x10)
            v_blank = bool(xy & 0x40)
            field = 1 if xy & 0x20 else 0

            if v_blank:                                # 场消隐：结束本场
                if self._lines:
                    image = self._finish_field()
                    if image is not None:
                        images.append(image)
                self._line = None
            elif is_eav:                               # 有效行结束
                if self._line is not None:
                    self._lines.append(bytes(self._line))
                self._line = None
            else:                                      # 有效行开始
                self._line = bytearray()
                self._field_flag = field
                if len(self._lines) > _MAX_LINES:      # 异常流保护
                    self._lines.clear()

            pos = idx + 4

        self._buf = bytearray(buf[pos:])
        return images

    # ---------- 组装一场 ----------
    def _finish_field(self):
        lines, self._lines = self._lines, []
        if not lines:
            return None
        w_groups = min(len(line) for line in lines) // 4
        if w_groups == 0:
            self.dropped_lines += len(lines)
            return None
        lines = lines[:_MAX_LINES]
        h = len(lines)

        raw = np.frombuffer(b"".join(line[:w_groups * 4] for line in lines), dtype=np.uint8)
        arr = raw.reshape(h, w_groups, 4)

        cb = arr[:, :, 0].astype(np.int16) - 128
        y0 = arr[:, :, 1].astype(np.int16)
        cr = arr[:, :, 2].astype(np.int16) - 128
        y1 = arr[:, :, 3].astype(np.int16)

        y = np.empty((h, w_groups * 2), dtype=np.int16)
        y[:, 0::2] = y0
        y[:, 1::2] = y1

        if self.limited_range:                          # BT.601 16~235 / 16~240
            y = (y - 16) * (255.0 / 219.0)
            cb = cb * (255.0 / 224.0)
            cr = cr * (255.0 / 224.0)

        cb2 = np.repeat(cb, 2, axis=1).astype(np.float32)
        cr2 = np.repeat(cr, 2, axis=1).astype(np.float32)
        yf = y.astype(np.float32)

        r = yf + 1.402 * cr2
        g = yf - 0.344136 * cb2 - 0.714136 * cr2
        b = yf + 1.772 * cb2
        rgb = np.clip(np.stack((r, g, b), axis=-1), 0, 255).astype(np.uint8)

        self._seq += 1
        return Image(
            width=w_groups * 2,
            height=h,
            rgb=rgb.tobytes(),
            seq=self._seq,
            info=f"BT656 场 F={self._field_flag}",
        )
