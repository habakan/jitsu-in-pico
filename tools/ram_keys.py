# /// script
# dependencies = ["coincurve", "embit"]
# ///
"""RAM のダンプから鍵の残骸を、値ではなく「鍵としての性質」で探す。

`ram_scan.py` は既知の値を探すので、こちらが想定していない残骸は見つけられない。
こちらは 32 byte の窓を総当たりで秘密鍵とみなし、導出した公開鍵が既知のものと一致するかを見る。
**値を知らなくても見つかる**ので、導出の途中の子鍵や、署名の nonce のような想定外の残骸を拾える。

    uv run tools/ram_keys.py build/ram_swd.bin                    # 鍵の残骸を探す
    uv run tools/ram_keys.py build/ram_sign.bin --sig <R の 32 byte を 16 進で>   # nonce を探す

nonce が残っていると、公開された署名と組にして秘密鍵が復元できる。最も危ない残骸。"""
import re
import sys
from pathlib import Path

import coincurve
from embit import bip39, bip32, networks, script

ORDER = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
SRAM_START = 0x20000000
GAP = 20  # 受取・釣りとも先頭から何番目まで見るか


def targets_from_seed(path):
    """テスト用シードの記録から、照合する公開鍵の一覧を作る"""
    m = re.search(r"^## ニーモニック\s*\n+```\n(.+?)\n```", Path(path).read_text(), re.M | re.S)
    if not m:
        sys.exit(f"ニーモニックが読めない: {path}")
    net = networks.NETWORKS["signet"]
    root = bip32.HDKey.from_seed(bip39.mnemonic_to_seed(m.group(1).strip()), version=net["xprv"])
    out = {}
    def add(name, hd):
        pub = hd.to_public().key.serialize()
        out[pub] = name                       # 圧縮公開鍵
        out[pub[1:]] = name + "（x-only）"     # taproot の出力鍵の形
    add("マスター", root)
    # mainnet と signet の両方を見る。どちらのビルドのダンプでも使えるように
    for coin in (0, 1):
        acct = f"m/84h/{coin}h/0h"
        add(f"口座 {acct}", root.derive(acct))
        for chain, label in ((0, "受取"), (1, "釣り")):
            for i in range(GAP):
                add(f"{label} {acct}/{chain}/{i}", root.derive(f"{acct}/{chain}/{i}"))
    return out


def scan(ram, want, align):
    """32 byte の窓を秘密鍵とみなし、導出した公開鍵が want にあるか見る"""
    hits, checked = [], 0
    for off in range(0, len(ram) - 32, align):
        w = ram[off:off + 32]
        if w[0] == 0 and w[:8] == b"\0" * 8:   # ゼロ埋めと小さい整数は鍵ではない
            continue
        v = int.from_bytes(w, "big")
        if not (0 < v < ORDER):
            continue
        checked += 1
        pub = coincurve.PrivateKey(w).public_key.format()
        for form in (pub, pub[1:]):
            if form in want:
                hits.append((SRAM_START + off, want[form]))
                break
    return hits, checked


ram = Path(sys.argv[1]).read_bytes()
align = 4 if "--align4" in sys.argv else 1
print(f"{len(ram)} byte を {sys.argv[1]} から読んだ（{align} byte ごと）")

if "--sig" in sys.argv:
    # R から k を探す。k*G == R となる 32 byte が残っていれば、署名と組にして鍵が復元できる
    r = bytes.fromhex(sys.argv[sys.argv.index("--sig") + 1])
    want = {}
    for prefix in (b"\x02", b"\x03"):
        want[prefix + r] = "署名の nonce (k)"
    want[r] = "署名の nonce (k)"
    hits, checked = scan(ram, want, align)
    print(f"{checked} 個の候補を検証した")
    print("nonce は残っていない" if not hits else f"**nonce が残っている**: {hits}")
    sys.exit(1 if hits else 0)

want = targets_from_seed(sys.argv[2] if len(sys.argv) > 2 and not sys.argv[2].startswith("--")
                         else "docs/internal/signet-test-seed.md")
hits, checked = scan(ram, want, align)
print(f"{checked} 個の候補を検証した（照合先 {len(want) // 2} 鍵）")
if not hits:
    print("鍵の残骸なし")
else:
    print(f"{len(hits)} 件:")
    for off, name in hits:
        print(f"  0x{off:08x}  {name}")
sys.exit(1 if hits else 0)
