# /// script
# dependencies = []
# ///
"""BOOTSEL で再起動した実機から SRAM を吸い出し、鍵の痕跡が残っていないか探す。
SWD を外しても BOOTSEL は生きているので、物理的に触れる相手はこの経路で RAM を読める。

使い方: uv run tools/security/ram_scan.py [探す値を書いたファイル] [--dump 吸い出し済みの.bin]

--dump を渡すと picotool を使わず、そのファイルを調べる（SWD で読んだものなど）。

探す値のファイルは 1 行 1 項目で `名前: 値` の形。値は文字列として、16 進に見えるものは
バイト列としても探す。`docs/internal/` のテスト用シードの記録をそのまま渡せる。"""
import re
import subprocess
import sys
from pathlib import Path

SRAM_START, SRAM_END = 0x20000000, 0x20082000  # RP2350: 512KB + 2 x 4KB
DUMP = Path("build/ram.bin")


def dump():
    DUMP.parent.mkdir(exist_ok=True)
    r = subprocess.run(["picotool", "save", "-r", hex(SRAM_START), hex(SRAM_END), str(DUMP)],
                       capture_output=True, text=True)
    if r.returncode:
        sys.exit(f"吸い出せません。BOOTSEL を押しながら USB を挿し直してください\n{r.stderr.strip()}")
    return DUMP.read_bytes()


def needles(path):
    """名前と、探すバイト列の組を作る"""
    out = []
    for line in Path(path).read_text().splitlines():
        m = re.match(r"\s*([^:]+):\s*(\S.*?)\s*$", line)
        if not m:
            continue
        name, val = m.group(1).strip(), m.group(2).strip().strip("`|")
        if len(val) < 8:
            continue
        out.append((name, val.encode()))
        if re.fullmatch(r"[0-9a-fA-F]{8,}", val) and len(val) % 2 == 0:
            out.append((name + "（バイト列）", bytes.fromhex(val)))
    return out


args = sys.argv[1:]
if "--dump" in args:
    i = args.index("--dump")
    ram = Path(args[i + 1]).read_bytes()
    print(f"{len(ram)} byte を {args[i + 1]} から読んだ")
    args = args[:i] + args[i + 2:]
else:
    ram = dump()
    print(f"{len(ram)} byte を {hex(SRAM_START)} から読んだ")

found = []
for name, b in needles(args[0] if args else "docs/internal/signet-test-seed.md"):
    at = []
    i = ram.find(b)
    while i >= 0 and len(at) < 4:
        at.append(hex(SRAM_START + i))
        i = ram.find(b, i + 1)
    if at:
        found.append((name, len(b), at))

if not found:
    print("見つからなかった（痕跡なし）")
else:
    print(f"{len(found)} 件の痕跡:")
    for name, n, at in found:
        print(f"  {name}  {n} byte  {', '.join(at)}")
sys.exit(1 if found else 0)
