# /// script
# dependencies = []
# ///
"""「同じ WASM がどこでも動く」図を SVG にする。Mermaid だと縦に伸びて中心が見えないため、
中心から放射する形を手で組む。使い方: uv run tools/draw_everywhere.py docs/everywhere.svg"""
import math
import sys

W, H = 900, 620
CX, CY = W / 2, H / 2 + 10
RX, RY = 310, 205          # 置き先を並べる楕円
CARD_W, CARD_H = 236, 78

# 名前, ランタイム, 確認済みか, 楕円上の角度（度、0 が右）, アイコン
# ブランドのロゴは商標があるので使わず、種類が分かる図形を描く
PLATFORMS = [
    ("Bare metal MCU", "WAMR · RP2350 · no OS", True, -90, "chip"),
    ("Android", "Chrome · single HTML", True, -30, "phone"),
    ("iOS", "Safari · JavaScriptCore", False, 30, "phone"),
    ("Node / CI", "V8 · vectors and fuzzing", True, 90, "terminal"),
    ("Linux / macOS", "native link / wasmtime", True, 150, "laptop"),
    ("Web / PWA", "browser · single 56KB HTML", True, 210, "globe"),
]

def icon(kind, x, y, size, color):
    """24x24 で描いた図形を (x, y) に size で置く"""
    k = size / 24
    d = {
        "chip": ['<rect x="7" y="7" width="10" height="10" rx="1.5"/>'] +
                [f'<line x1="{a}" y1="{b}" x2="{c}" y2="{e}"/>' for a, b, c, e in
                 [(10, 7, 10, 4), (14, 7, 14, 4), (10, 17, 10, 20), (14, 17, 14, 20),
                  (7, 10, 4, 10), (7, 14, 4, 14), (17, 10, 20, 10), (17, 14, 20, 14)]],
        "phone": ['<rect x="7" y="3" width="10" height="18" rx="2"/>',
                  '<line x1="10.5" y1="5.5" x2="13.5" y2="5.5"/>',
                  '<circle cx="12" cy="18" r="0.9"/>'],
        "globe": ['<circle cx="12" cy="12" r="8.5"/>', '<ellipse cx="12" cy="12" rx="4" ry="8.5"/>',
                  '<line x1="3.5" y1="12" x2="20.5" y2="12"/>',
                  '<path d="M5.5 7 Q12 10 18.5 7"/>', '<path d="M5.5 17 Q12 14 18.5 17"/>'],
        "laptop": ['<rect x="4" y="5" width="16" height="10" rx="1.5"/>',
                   '<path d="M2 18.5 h20"/>', '<path d="M9.5 15.5 h5"/>'],
        "terminal": ['<rect x="3" y="4.5" width="18" height="15" rx="2"/>',
                     '<path d="M7 9.5 l3 2.5 l-3 2.5"/>', '<line x1="12.5" y1="15" x2="17" y2="15"/>'],
    }[kind]
    body = "".join(d)
    return (f'<g transform="translate({x},{y}) scale({k})" fill="none" stroke="{color}" '
            f'stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">{body}</g>')
ARTIFACTS = [("parser.wasm", "15,598 B"), ("qr.wasm", "16,546 B"), ("address.wasm", "3,058 B")]

FONT = "Hiragino Sans, Noto Sans JP, sans-serif"
BG, LINE, DIM, FG = "#13151a", "#3a404d", "#8b93a1", "#e8e8e8"
OK_FILL, OK_LINE, OK_FG = "#15362a", "#2ea86a", "#9ff0c4"
TODO_FILL, TODO_LINE = "#20242c", "#4a515f"
HUB_FILL, HUB_LINE, HUB_FG = "#2a2113", "#ffaa00", "#ffcf6b"

s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
     f'font-family="{FONT}"><rect width="{W}" height="{H}" fill="{BG}"/>']

s.append(f'<text x="{W/2}" y="38" fill="{FG}" font-size="20" text-anchor="middle">'
         f'The same WASM runs everywhere</text>')
s.append(f'<text x="{W/2}" y="60" fill="{DIM}" font-size="13" text-anchor="middle">'
         f'Zero imports. No WASI, no JS polyfill.</text>')

# 中心から各カードへの線（カードの下に描く）
pos = []
for name, rt, done, deg, _ in PLATFORMS:
    r = math.radians(deg)
    x, y = CX + RX * math.cos(r), CY + RY * math.sin(r)
    pos.append((x, y))
    s.append(f'<line x1="{CX}" y1="{CY}" x2="{x}" y2="{y}" stroke="{OK_LINE if done else LINE}" '
             f'stroke-width="{2.4 if done else 1.6}" opacity="{0.85 if done else 0.5}"/>')

# 中心（同じ WASM）
hw, hh = 250, 128
s.append(f'<rect x="{CX - hw/2}" y="{CY - hh/2}" width="{hw}" height="{hh}" rx="10" '
         f'fill="{HUB_FILL}" stroke="{HUB_LINE}" stroke-width="2"/>')
s.append(f'<text x="{CX}" y="{CY - hh/2 + 26}" fill="{HUB_FG}" font-size="15" text-anchor="middle">the same WASM</text>')
for i, (n, sz) in enumerate(ARTIFACTS):
    y = CY - hh/2 + 52 + i * 23
    s.append(f'<text x="{CX - hw/2 + 18}" y="{y}" fill="{FG}" font-size="13">{n}</text>')
    s.append(f'<text x="{CX + hw/2 - 18}" y="{y}" fill="{DIM}" font-size="12" text-anchor="end">{sz}</text>')

# 置き先のカード。左にアイコン、右に文字
for (name, rt, done, deg, kind), (x, y) in zip(PLATFORMS, pos):
    fill, stroke = (OK_FILL, OK_LINE) if done else (TODO_FILL, TODO_LINE)
    left = x - CARD_W / 2
    s.append(f'<rect x="{left}" y="{y - CARD_H/2}" width="{CARD_W}" height="{CARD_H}" rx="8" '
             f'fill="{fill}" stroke="{stroke}" stroke-width="1.6"/>')
    s.append(icon(kind, left + 14, y - 17, 34, OK_LINE if done else DIM))
    tx = left + 60
    s.append(f'<text x="{tx}" y="{y - 12}" fill="{OK_FG if done else FG}" font-size="15">{name}</text>')
    s.append(f'<text x="{tx}" y="{y + 7}" fill="{DIM}" font-size="11.5">{rt}</text>')
    s.append(f'<text x="{tx}" y="{y + 24}" fill="{OK_LINE if done else DIM}" font-size="10.5">'
             f'{"verified on real hardware" if done else "not tried yet (same path)"}</text>')

s.append(f'<text x="24" y="{H - 22}" fill="{DIM}" font-size="11.5">'
         f'green = actually run and verified / grey = same path, should work, not tried yet</text>')
s.append("</svg>")

out = sys.argv[1] if len(sys.argv) > 1 else "docs/everywhere.svg"
open(out, "w").write("\n".join(s))
print(f"{out}: {len(PLATFORMS)} 個の置き先、{W}x{H}")
