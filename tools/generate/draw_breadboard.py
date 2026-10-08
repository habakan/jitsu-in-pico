# /// script
# dependencies = ["pyyaml"]
# ///
"""docs/breadboard.yml から、ブレッドボードの配線図（SVG）を描く。
使い方: uv run tools/generate/draw_breadboard.py docs/breadboard.yml build/breadboard.svg

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


def xy(hole, s_off=260):
    """穴の名前から中心座標。レールは "+42" / "-9" """
    m = re.fullmatch(r"([A-J+-])(\d+)", hole)
    if not m:
        return None
    col, row = m.group(1), int(m.group(2))
    return 60 + s_off + XOF[col] * PITCH, 40 + row * PITCH


def main(src, out):
    d = yaml.safe_load(open(src))
    rows, w, h = d["rows"], 420 + (WIDTH_COLS + 11) * PITCH, 60 + (d["rows"] + 1) * PITCH
    s_off = 260  # 左にぶら下がる部品のぶん
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

    # 基板の外にぶら下がる部品（カメラ、Debug Probe）。ピンの位置を覚えておく
    ext_pin = {}
    for e in d.get("external", []):
        ncol = e.get("columns", 1)
        rows_n = (len(e["pins"]) + ncol - 1) // ncol
        bw = 110 + (ncol - 1) * (PITCH + 34)
        x0 = (60 + s_off + (WIDTH_COLS + 1) * PITCH + 90) if e["side"] == "right" else (s_off - bw - 30)
        y0 = 40 + e["at"] * PITCH
        s.append(f'<rect x="{x0}" y="{y0 - 18}" width="{bw}" height="{rows_n * PITCH + 26}" fill="#2a2a2a" '
                 f'opacity="0.78" rx="4"/>')
        s.append(f'<text x="{x0 + 6}" y="{y0 - 5}" font-size="12" fill="#fff">{e["name"]}</text>')
        # 実物のシルクと同じ並びにする（奇数が左列、偶数が右列）。ピン番号も出す
        for i, name in enumerate(e["pins"]):
            col, row = i % ncol, i // ncol
            px, py = x0 + 44 + col * (PITCH + 34), y0 + row * PITCH + 10
            s.append(f'<rect x="{px - R}" y="{py - R}" width="{2 * R}" height="{2 * R}" fill="#fff" stroke="#999"/>')
            if name != "NC":
                ext_pin[f'{e.get("id", e["name"].split()[0])}.{name}'] = (px, py)
            anchor, tx = ("end", px - 8) if col == 0 else ("start", px + 8)
            s.append(f'<text x="{tx}" y="{py + 4}" font-size="9" fill="#ccc" text-anchor="{anchor}">'
                     f'{i + 1} {name}</text>')

    # 配線。基板の外へ出るものは、穴のある側（A〜E なら左、F〜J とレールなら右）へ逃がす
    colors, ext = {}, {"L": [], "R": []}
    for wire in d["wires"]:
        net = wire.get("net", "")
        color = NET_COLORS.get(net) or colors.setdefault(net, PALETTE[len(colors) % len(PALETTE)])
        fr, to = str(wire["from"]), str(wire["to"])
        a, b = xy(fr) or ext_pin.get(fr), xy(to) or ext_pin.get(to)
        if a and b:
            on_board = xy(fr) and xy(to)
            k = 0.1 if on_board else 0.03
            mx, my = (a[0] + b[0]) / 2 + (b[1] - a[1]) * k, (a[1] + b[1]) / 2 - (b[0] - a[0]) * k
            s.append(f'<path d="M{a[0]},{a[1]} Q{mx},{my} {b[0]},{b[1]}" fill="none" stroke="{color}" '
                     f'stroke-width="{3.6 if on_board else 3.0}" opacity="{0.9 if on_board else 0.8}" '
                     f'stroke-linecap="round"/>')
        elif a or b:
            hole, label = (a, to) if a else (b, fr)
            ext["L" if hole[0] <= 60 + s_off + XOF["E"] * PITCH else "R"].append((hole, label, color))

    for side, items in ext.items():
        last = -1e9
        for hole, label, color in sorted(items, key=lambda t: t[0][1]):
            ey = max(hole[1], last + 15)
            last = ey
            ex = 246 if side == "L" else 60 + s_off + (WIDTH_COLS + 1) * PITCH
            s.append(f'<path d="M{hole[0]},{hole[1]} L{ex},{ey}" fill="none" stroke="{color}" stroke-width="3.0" '
                     f'opacity="0.8" stroke-linecap="round"/>')
            anchor = ' text-anchor="end"' if side == "L" else ""
            s.append(f'<text x="{ex + (-6 if side == "L" else 6)}" y="{ey + 4}" font-size="11" '
                     f'fill="{color}"{anchor}>{label}</text>')

    s.append("</svg>")
    open(out, "w").write("\n".join(s))
    print(f"{out}: {rows} 行, 配線 {len(d['wires'])} 本, 部品 {len(d.get('components', []))} 個")


main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "build/breadboard.svg")
