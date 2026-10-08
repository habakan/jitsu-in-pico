# /// script
# dependencies = ["zxing-cpp", "pillow"]
# ///
"""LCD に出す QR 画面（psbt_host が書いた PPM）を zxing-cpp で読み、UR のパートと同じ文字列か確かめる。"""
import sys
import zxingcpp
from PIL import Image

prefix, ur_path = sys.argv[1], sys.argv[2]
parts = [l.strip() for l in open(ur_path) if l.strip()]
bad = 0
for i in range(2):
    img = Image.open(f"{prefix}_qr_{i:02d}.ppm")
    found = zxingcpp.read_barcodes(img)
    text = found[0].text if found else None
    ok = text == parts[i]
    bad += not ok
    info = f"version {found[0].version}, {found[0].ec_level}" if found and hasattr(found[0], "version") else ""
    print(f"qr screen {i}: {'ok' if ok else 'NG'} {info} {len(parts[i])} chars")
sys.exit(1 if bad else 0)
