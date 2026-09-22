"""BDF の 8x16 フォントから ASCII 0x20〜0x7e を取り出し、1 行 1 byte（MSB が左端）の C 配列にする。
Spleen（BSD-2-Clause）を想定。生成物は build/ に置き、リポジトリにはフォントを入れない。"""
import sys

W, H, ASCENT = 8, 16, 12  # Spleen 8x16: FONTBOUNDINGBOX 8 16 0 -4
glyphs, cur, bitmap = {}, None, None
for line in open(sys.argv[1], encoding="latin-1"):
    t = line.split()
    if not t:
        continue
    if t[0] == "ENCODING":
        cur = int(t[1])
    elif t[0] == "BBX":
        bw, bh, bx, by = map(int, t[1:5])
    elif t[0] == "BITMAP":
        bitmap = []
    elif t[0] == "ENDCHAR":
        if cur is not None and 0x20 <= cur <= 0x7E:
            rows = [0] * H
            top = ASCENT - (by + bh)  # BBX の下端オフセットから、セル内の開始行を出す
            for i, v in enumerate(bitmap):
                if 0 <= top + i < H:
                    rows[top + i] = (v >> bx) & 0xFF if bx >= 0 else (v << -bx) & 0xFF
            glyphs[cur] = rows
        bitmap = None
    elif bitmap is not None:
        bitmap.append(int(t[0], 16) >> (len(t[0]) * 4 - 8) if len(t[0]) > 2 else int(t[0], 16))

missing = [c for c in range(0x20, 0x7F) if c not in glyphs]
assert not missing, missing
out = [f"/* tools/gen_font.py が {sys.argv[1].split('/')[-1]} から生成（Spleen, BSD-2-Clause） */",
       f"#define FONT_W {W}", f"#define FONT_H {H}", "static const unsigned char font8x16[95][16] = {"]
for c in range(0x20, 0x7F):
    out.append("    {" + ",".join(f"0x{r:02x}" for r in glyphs[c]) + "},")
out.append("};")
open(sys.argv[2], "w").write("\n".join(out) + "\n")
