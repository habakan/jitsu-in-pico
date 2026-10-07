# /// script
# dependencies = ["zxing-cpp", "pillow"]
# ///
"""gen_qr_frames.py が出した PNG を zxing-cpp で読み、quirc の読取限界と比べる基準にする。"""
import glob, sys, zxingcpp
from PIL import Image

for p in sorted(glob.glob(sys.argv[1] + "_*.png")):
    name = p[len(sys.argv[1]) + 1:-4]
    print(name, "ok" if zxingcpp.read_barcodes(Image.open(p)) else "NG")
