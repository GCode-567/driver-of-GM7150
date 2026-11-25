"""兼容入口：保留旧文件名，实际逻辑在 gm7150_viewer 包中。

    python tools/serial_viewer.py            # 自动选串口，115200
    python tools/serial_viewer.py COM3 2000000
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from gm7150_viewer.app import main  # noqa: E402

if __name__ == "__main__":
    sys.exit(main())
