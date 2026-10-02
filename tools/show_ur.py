# /// script
# dependencies = ["segno", "pillow"]
# ///
"""UR のパート（1 行 1 パート）をアニメーション GIF にして、実機のカメラに読ませる。
使い方: uv run tools/show_ur.py build/psbt/scan.ur [出力.gif] [1 枚あたりのミリ秒]

パートは純粋なものだけを周回させる。実機側（app）の出し方と同じで、どの受信側でも完成できる。"""
import os
import subprocess
import sys

import segno
from PIL import Image

src = sys.argv[1]
out = sys.argv[2] if len(sys.argv) > 2 else "build/psbt/scan.gif"
ms = int(sys.argv[3]) if len(sys.argv) > 3 else 500
parts = [p for p in open(src).read().split() if p]
seq_len = int(parts[0].split("/")[1].split("-")[1])

frames = []
for p in parts[:seq_len]:
    qr = segno.make(p, error="l")
    png = qr.png_data_uri(scale=10, border=4)
    import base64, io

    frames.append(Image.open(io.BytesIO(base64.b64decode(png.split(",", 1)[1]))).convert("P"))
frames[0].save(out, save_all=True, append_images=frames[1:], duration=ms, loop=0)
print(f"{out}: {seq_len} parts, {ms}ms each, {frames[0].size[0]}px")
subprocess.run(["open", "-a", "Safari", os.path.abspath(out)], check=False)  # プレビューは GIF を動かさない
