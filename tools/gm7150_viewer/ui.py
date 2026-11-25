"""显示界面（Tkinter）。

左侧显示图像，右侧侧边栏实时显示串口原始字节与链路统计。
只负责显示与交互，不关心数据从哪来：定时从队列取最新图像刷新。
图像统一以RGB24传入，BT.656与分包两种来源都走同一条显示路径。
"""

import os
import tempfile
import time
import tkinter as tk
from tkinter import filedialog


def save_rgb_bmp(path: str, width: int, height: int, rgb: bytes):
    """把24位RGB数据写成BMP（自下而上，每行按4字节对齐）。"""
    row_bytes = width * 3
    row_pad = (4 - row_bytes % 4) % 4
    row_size = row_bytes + row_pad
    image_size = row_size * height
    file_size = 54 + image_size

    header = bytearray()
    header += b"BM"
    header += file_size.to_bytes(4, "little")
    header += (0).to_bytes(4, "little")
    header += (54).to_bytes(4, "little")
    header += (40).to_bytes(4, "little")
    header += width.to_bytes(4, "little", signed=True)
    header += height.to_bytes(4, "little", signed=True)
    header += (1).to_bytes(2, "little")
    header += (24).to_bytes(2, "little")
    header += (0).to_bytes(4, "little")
    header += image_size.to_bytes(4, "little")
    for _ in range(4):                      # 水平/垂直DPI、调色板数、重要色数
        header += (0).to_bytes(4, "little")

    pad = b"\x00" * row_pad
    stride = row_bytes
    with open(path, "wb") as f:
        f.write(header)
        for y in range(height - 1, -1, -1):
            f.write(rgb[y * stride:(y + 1) * stride])
            f.write(pad)


class ViewerWindow(tk.Tk):
    """主窗口：图像区 + 串口数据侧边栏 + 状态栏。"""

    REFRESH_MS = 30
    SIDE_MS = 150          # 侧边栏刷新间隔，避免高频刷新拖慢界面
    HEX_PER_LINE = 12

    def __init__(self, frame_queue, link_desc: str, save_dir=None, reader=None):
        super().__init__()
        self._q = frame_queue
        self._save_dir = save_dir
        self._reader = reader
        self._t0 = time.time()
        self._photo = None
        self._tmp_dir = tempfile.mkdtemp(prefix="gm7150_")
        self._tmp_file = os.path.join(self._tmp_dir, "frame.ppm")
        self._paused = False
        self._last = None
        self._zoom = 1
        self._count = 0
        self._fps = 0.0
        self._fps_mark = time.time()
        self._fps_count = 0
        self._side_mark = 0.0
        self._rate_mark = time.time()
        self._rate_bytes = 0
        self._rate = 0.0

        self.title("GM7150 视频接收器")
        self.configure(bg="#101010")
        self.geometry("1000x660")
        self.minsize(700, 480)

        self.grid_rowconfigure(0, weight=1)
        self.grid_columnconfigure(0, weight=1)

        self.image_label = tk.Label(self, bg="#000000", fg="#888888",
                                    text="等待数据...", font=("Consolas", 12))
        self.image_label.grid(row=0, column=0, sticky="nsew")

        self._build_sidebar()

        bar = tk.Frame(self, bg="#1b1b1b")
        bar.grid(row=1, column=0, columnspan=2, sticky="ew")

        self.status = tk.Label(bar, bg="#1b1b1b", fg="#cccccc", anchor="w",
                               font=("Consolas", 9), text=f"{link_desc} | 等待数据")
        self.status.pack(side="left", padx=8, pady=4)
        self._link_desc = link_desc

        for text, cmd in (("退出", self._on_close),
                          ("保存帧", self._save_current),
                          ("暂停", self._toggle_pause)):
            tk.Button(bar, text=text, command=cmd, width=8).pack(side="right", padx=3, pady=3)

        self.bind("<Escape>", lambda e: self._on_close())
        self.bind("<space>", lambda e: self._toggle_pause())
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.after(self.REFRESH_MS, self._pump)

    # ---------- 侧边栏 ----------
    def _build_sidebar(self):
        side = tk.Frame(self, bg="#161616", width=340)
        side.grid(row=0, column=1, sticky="ns")
        side.grid_propagate(False)

        tk.Label(side, text="串口原始数据 (HEX)", bg="#161616", fg="#dddddd",
                 anchor="w", font=("Microsoft YaHei", 10, "bold")).pack(
            fill="x", padx=8, pady=(8, 4))

        self.side_info = tk.Label(side, text="已收 0 字节 | 0.0 KB/s",
                                  bg="#161616", fg="#7fb8d8", anchor="w",
                                  font=("Consolas", 9))
        self.side_info.pack(fill="x", padx=8)

        self.hex_text = tk.Text(side, bg="#0b0b0b", fg="#9cdcfe", bd=0,
                                highlightthickness=0, font=("Consolas", 9),
                                padx=6, pady=4, wrap="none")
        self.hex_text.pack(fill="both", expand=True, padx=8, pady=6)
        self.hex_text.config(state="disabled")

        tk.Label(side, text="每行 12 字节，滚动显示最近 288 字节",
                 bg="#161616", fg="#666666", anchor="w",
                 font=("Consolas", 8)).pack(fill="x", padx=8, pady=(0, 8))

    def _refresh_sidebar(self):
        if self._reader is None:
            return
        now = time.time()
        rx = self._reader.rx_bytes
        elapsed = now - self._rate_mark
        if elapsed >= 0.5:
            self._rate = (rx - self._rate_bytes) / elapsed / 1024.0
            self._rate_mark, self._rate_bytes = now, rx
        self.side_info.config(text=f"已收 {rx} 字节 | {self._rate:.1f} KB/s "
                                   f"| 丢弃帧 {self._reader.dropped}")

        data = self._reader.raw_tail(self.HEX_PER_LINE * 24)
        lines = [" ".join(f"{b:02X}" for b in data[i:i + self.HEX_PER_LINE])
                 for i in range(0, len(data), self.HEX_PER_LINE)]
        self.hex_text.config(state="normal")
        self.hex_text.delete("1.0", "end")
        self.hex_text.insert("1.0", "\n".join(lines))
        self.hex_text.see("end")
        self.hex_text.config(state="disabled")

    # ---------- 周期刷新 ----------
    def _pump(self):
        if not self._paused:
            image = None
            while not self._q.empty():          # 只显示最新一帧
                try:
                    image = self._q.get_nowait()
                except Exception:
                    break
            if image is not None:
                self._show(image)
            elif self._count == 0:
                self._hint()

        now = time.time()
        if now - self._side_mark >= self.SIDE_MS / 1000.0:
            self._side_mark = now
            self._refresh_sidebar()

        self.after(self.REFRESH_MS, self._pump)

    def _hint(self):
        """还没出图时，提示是"没收到字节"还是"收到但解不出"（极易定位问题）。"""
        if time.time() - self._t0 < 2.0:
            return
        rx = self._reader.rx_bytes if self._reader is not None else 0
        if rx == 0:
            self.status.config(text=f"{self._link_desc} | 未收到任何字节：检查 PA9→RXD、GND 共地、波特率")
        else:
            self.status.config(text=f"{self._link_desc} | 已收 {rx} 字节，未解出图像："
                                    f"检查波特率是否一致 / 是否已烧录新固件")

    def _show(self, image):
        try:
            with open(self._tmp_file, "wb") as f:
                f.write(f"P6\n{image.width} {image.height}\n255\n".encode())
                f.write(image.rgb)
            self._photo = tk.PhotoImage(file=self._tmp_file)
            # 按窗口大小整数倍放大，避免小分辨率图像在窗口里显示过小
            self.image_label.update_idletasks()
            avail_w = self.image_label.winfo_width()
            avail_h = self.image_label.winfo_height()
            if avail_w > 1 and avail_h > 1:
                zoom = min(avail_w // image.width, avail_h // image.height, 8)
                if zoom > 1:
                    self._photo = self._photo.zoom(zoom)
                    self._zoom = zoom
        except (tk.TclError, ValueError):
            return
        self.image_label.config(image=self._photo, text="")
        self._last = image
        self._count += 1
        self._fps_count += 1

        now = time.time()
        if now - self._fps_mark >= 1.0:
            self._fps = self._fps_count / (now - self._fps_mark)
            self._fps_mark, self._fps_count = now, 0

        self.status.config(
            text=f"{self._link_desc} | {image.info} | {image.width}x{image.height} "
                 f"x{self._zoom} | #{image.seq} | {self._fps:.1f} fps | 累计 {self._count}")

    # ---------- 交互 ----------
    def _toggle_pause(self):
        self._paused = not self._paused
        self.status.config(text=f"{self._link_desc} | 已暂停（空格继续）" if self._paused
                           else f"{self._link_desc} | 继续接收")

    def _save_current(self):
        if self._last is None:
            return
        image = self._last
        name = f"frame_{image.seq:06d}.bmp"
        if self._save_dir:
            path = os.path.join(self._save_dir, name)
        else:
            path = filedialog.asksaveasfilename(
                defaultextension=".bmp", filetypes=[("BMP", "*.bmp")], initialfile=name)
            if not path:
                return
        save_rgb_bmp(path, image.width, image.height, image.rgb)
        self.status.config(text=f"已保存：{path}")

    def _on_close(self):
        self.destroy()
