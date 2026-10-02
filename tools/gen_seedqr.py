# /// script
# dependencies = ["segno", "embit"]
# ///
"""ニーモニックから SeedQR（SeedSigner 互換）を作る。実機の読み取り試験用。
使い方: uv run tools/gen_seedqr.py "word1 word2 ... word12" [出力.png]

標準 SeedQR（単語番号を 4 桁ずつ並べた数字列）を出す。BIP39 のチェックサムも確かめる。
**ここで作った QR は秘密そのもの。画面に出した履歴やファイルの扱いに注意する。**"""
import subprocess
import sys

import segno
from embit import bip39

mnemonic = " ".join(sys.argv[1].split())
out = sys.argv[2] if len(sys.argv) > 2 else "build/psbt/seedqr.png"
words = mnemonic.split()

assert len(words) in (12, 24), f"12 か 24 語にする（今は {len(words)} 語）"
assert bip39.mnemonic_is_valid(mnemonic), "BIP39 のチェックサムが合わない"
digits = "".join(f"{bip39.WORDLIST.index(w):04d}" for w in words)

qr = segno.make(digits, error="l")
qr.save(out, scale=12, border=4)
print(f"{out}: {len(words)} words, {len(digits)} digits, QR v{qr.version}")
subprocess.run(["open", out], check=False)
