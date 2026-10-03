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
CARD_W, CARD_H = 206, 74

# 名前, ランタイム, 確認済みか, 楕円上の角度（度、0 が右）
PLATFORMS = [
    ("ベアメタル MCU", "WAMR · RP2350 · OS なし", True, -90),
    ("Android", "Chrome / NDK + WAMR", False, -30),
    ("iOS", "Safari · JavaScriptCore", False, 30),
    ("Node / CI", "V8 · ベクタ照合とファジング", True, 90),
    ("Linux / macOS", "ネイティブ直リンク / wasmtime", True, 150),
    ("Web / PWA", "ブラウザ · 単一 HTML 56KB", True, 210),
]
ARTIFACTS = [("parser.wasm", "15,598 B"), ("qr.wasm", "16,546 B"), ("address.wasm", "3,058 B")]

FONT = "Hiragino Sans, Noto Sans JP, sans-serif"
BG, LINE, DIM, FG = "#13151a", "#3a404d", "#8b93a1", "#e8e8e8"
OK_FILL, OK_LINE, OK_FG = "#15362a", "#2ea86a", "#9ff0c4"
TODO_FILL, TODO_LINE = "#20242c", "#4a515f"
HUB_FILL, HUB_LINE, HUB_FG = "#2a2113", "#ffaa00", "#ffcf6b"

s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" '
     f'font-family="{FONT}"><rect width="{W}" height="{H}" fill="{BG}"/>']

s.append(f'<text x="{W/2}" y="38" fill="{FG}" font-size="20" text-anchor="middle">'
         f'同じ WASM が、どこでも動く</text>')
s.append(f'<text x="{W/2}" y="60" fill="{DIM}" font-size="13" text-anchor="middle">'
         f'import 0 個。WASI も JS のポリフィルも要らない</text>')

# 中心から各カードへの線（カードの下に描く）
pos = []
for name, rt, done, deg in PLATFORMS:
    r = math.radians(deg)
    x, y = CX + RX * math.cos(r), CY + RY * math.sin(r)
    pos.append((x, y))
    s.append(f'<line x1="{CX}" y1="{CY}" x2="{x}" y2="{y}" stroke="{OK_LINE if done else LINE}" '
             f'stroke-width="{2.4 if done else 1.6}" opacity="{0.85 if done else 0.5}"/>')

# 中心（同じ WASM）
hw, hh = 250, 128
s.append(f'<rect x="{CX - hw/2}" y="{CY - hh/2}" width="{hw}" height="{hh}" rx="10" '
         f'fill="{HUB_FILL}" stroke="{HUB_LINE}" stroke-width="2"/>')
s.append(f'<text x="{CX}" y="{CY - hh/2 + 26}" fill="{HUB_FG}" font-size="15" text-anchor="middle">同じ WASM</text>')
for i, (n, sz) in enumerate(ARTIFACTS):
    y = CY - hh/2 + 52 + i * 23
    s.append(f'<text x="{CX - hw/2 + 18}" y="{y}" fill="{FG}" font-size="13">{n}</text>')
    s.append(f'<text x="{CX + hw/2 - 18}" y="{y}" fill="{DIM}" font-size="12" text-anchor="end">{sz}</text>')

# 置き先のカード
for (name, rt, done, deg), (x, y) in zip(PLATFORMS, pos):
    fill, stroke = (OK_FILL, OK_LINE) if done else (TODO_FILL, TODO_LINE)
    s.append(f'<rect x="{x - CARD_W/2}" y="{y - CARD_H/2}" width="{CARD_W}" height="{CARD_H}" rx="8" '
             f'fill="{fill}" stroke="{stroke}" stroke-width="1.6"/>')
    s.append(f'<text x="{x}" y="{y - 8}" fill="{OK_FG if done else FG}" font-size="15" '
             f'text-anchor="middle">{name}</text>')
    s.append(f'<text x="{x}" y="{y + 13}" fill="{DIM}" font-size="11.5" text-anchor="middle">{rt}</text>')
    s.append(f'<text x="{x}" y="{y + 30}" fill="{OK_LINE if done else DIM}" font-size="10.5" '
             f'text-anchor="middle">{"動かして確認済み" if done else "未確認（同じ経路）"}</text>')

s.append(f'<text x="24" y="{H - 22}" fill="{DIM}" font-size="11.5">'
         f'緑＝実際に動かして確認したもの／灰＝同じ経路なので動くはずだが未確認</text>')
s.append("</svg>")

out = sys.argv[1] if len(sys.argv) > 1 else "docs/everywhere.svg"
open(out, "w").write("\n".join(s))
print(f"{out}: {len(PLATFORMS)} 個の置き先、{W}x{H}")
