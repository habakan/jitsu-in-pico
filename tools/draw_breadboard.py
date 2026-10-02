# /// script
# dependencies = ["pyyaml"]
# ///
"""docs/breadboard.yml から、ブレッドボードの配線図（SVG）を描く。
使い方: uv run tools/draw_breadboard.py docs/breadboard.yml build/breadboard.svg

穴の位置をそのまま絵にするので、組むときに見ながら挿せる。信号の対応は docs/wiring.yml（WireViz）。"""
import re
import sys

import yaml

PITCH, R = 22, 4.5
COLS = ["A", "B", "C", "D", "E", "F", "G", "H", "I", "J"]
# 左から 赤レール、青レール、すき間、A〜E、溝、F〜J
XOF = {"+": 0, "-": 1}
for i, c in enumerate(COLS):
    XOF[c] = 3 + i + (1 if i >= 5 else 0)
WIDTH_COLS = 3 + 10 + 1
NET_COLORS = {"3V3": "#d33", "GND": "#333"}
PALETTE = ["#1a7", "#17c", "#b5a", "#a62", "#07a", "#696", "#c70", "#539"]


def xy(hole, s_off=150):
    """穴の名前から中心座標。レールは "+42" / "-9" """
    m = re.fullmatch(r"([A-J+-])(\d+)", hole)
    if not m:
        return None
    col, row = m.group(1), int(m.group(2))
    return 60 + s_off + XOF[col] * PITCH, 40 + row * PITCH


def main(src, out):
    d = yaml.safe_load(open(src))
    rows, w, h = d["rows"], 210 + (WIDTH_COLS + 11) * PITCH, 60 + (d["rows"] + 1) * PITCH
    s_off = 150  # 左に逃がすラベルのぶん
    s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" font-family="sans-serif">',
         f'<rect width="{w}" height="{h}" fill="#fbfbf8"/>']

    # 穴
    for row in range(1, rows + 1):
        for col in list(XOF):
            x, y = xy(f"{col}{row}")
            fill = "#fdd" if col == "+" else "#ddf" if col == "-" else "#fff"
            s.append(f'<rect x="{x - R}" y="{y - R}" width="{2 * R}" height="{2 * R}" fill="{fill}" stroke="#bbb"/>')
        if row % 5 == 0 or row == 1:
            s.append(f'<text x="{34 + s_off}" y="{40 + row * PITCH + 4}" font-size="11" fill="#888">{row}</text>')
    for col in COLS:
        s.append(f'<text x="{60 + s_off + XOF[col] * PITCH - 4}" y="34" font-size="11" fill="#888">{col}</text>')
    # レールの帯
    for col, color in (("+", "#d33"), ("-", "#36d")):
        x = 60 + s_off + XOF[col] * PITCH
        s.append(f'<line x1="{x}" y1="{40 + PITCH}" x2="{x}" y2="{40 + rows * PITCH}" stroke="{color}" '
                 f'stroke-width="1" opacity="0.5"/>')

    # 部品（基板の上に乗るもの）
    for c in d.get("components", []):
        c0, c1 = XOF[c["columns"][0]], XOF[c["columns"][1]]
        r0, r1 = c["rows"]
        x, y = 60 + s_off + c0 * PITCH - PITCH / 2, 40 + r0 * PITCH - PITCH / 2
        cw = (c1 - c0 + 1) * PITCH + (6 * PITCH if c.get("extend") == "right" else 0)
        s.append(f'<rect x="{x}" y="{y}" width="{cw}" height="{(r1 - r0 + 1) * PITCH}" fill="#2a2a2a" '
                 f'opacity="0.78" rx="4"/>')
        s.append(f'<text x="{x + 6}" y="{y + 16}" font-size="12" fill="#fff">{c["name"]}</text>')
        if c.get("note"):
            s.append(f'<text x="{x + 6}" y="{y + 30}" font-size="10" fill="#bbb">{c["note"]}</text>')

    # 配線。基板の外へ出るものは、穴のある側（A〜E なら左、F〜J とレールなら右）へ逃がす
    colors, ext = {}, {"L": [], "R": []}
    for wire in d["wires"]:
        net = wire.get("net", "")
        color = NET_COLORS.get(net) or colors.setdefault(net, PALETTE[len(colors) % len(PALETTE)])
        a, b = xy(str(wire["from"])), xy(str(wire["to"]))
        if a and b:
            mx, my = (a[0] + b[0]) / 2 + (b[1] - a[1]) * 0.1, (a[1] + b[1]) / 2 - (b[0] - a[0]) * 0.1
            s.append(f'<path d="M{a[0]},{a[1]} Q{mx},{my} {b[0]},{b[1]}" fill="none" stroke="{color}" '
                     f'stroke-width="2.2" opacity="0.85"/>')
        else:
            hole, label = (a, wire["to"]) if a else (b, wire["from"])
            name = str(wire["from"] if a else wire["to"])
            ext["L" if hole[0] <= 60 + s_off + XOF["E"] * PITCH else "R"].append((hole, label, color))

    for side, items in ext.items():
        last = -1e9
        for hole, label, color in sorted(items, key=lambda t: t[0][1]):
            ey = max(hole[1], last + 15)
            last = ey
            ex = 136 if side == "L" else 60 + s_off + (WIDTH_COLS + 1) * PITCH
            s.append(f'<path d="M{hole[0]},{hole[1]} L{ex},{ey}" fill="none" stroke="{color}" stroke-width="1.6" '
                     f'opacity="0.75"/>')
            anchor = ' text-anchor="end"' if side == "L" else ""
            s.append(f'<text x="{ex + (-6 if side == "L" else 6)}" y="{ey + 4}" font-size="11" '
                     f'fill="{color}"{anchor}>{label}</text>')

    s.append("</svg>")
    open(out, "w").write("\n".join(s))
    print(f"{out}: {rows} 行, 配線 {len(d['wires'])} 本, 部品 {len(d.get('components', []))} 個")


main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "build/breadboard.svg")
