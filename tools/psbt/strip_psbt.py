# /// script
# dependencies = ["embit"]
# ///
"""segwit 入力の前トランザクション（non-witness UTXO）を落とす。QR で渡せる大きさにするため。
使い方: uv run tools/psbt/strip_psbt.py in.psbt out.psbt

単署名の P2WPKH では、入力額を偽られると署名が無効になるだけなので落として差し支えない
（同じ入力に違う額で二度署名させられるマルチシグとは事情が違う）。"""
import sys
from embit.psbt import PSBT

src, dst = sys.argv[1], sys.argv[2]
p = PSBT.parse(open(src, "rb").read())
before = len(p.serialize())
for i in p.inputs:
    if i.witness_utxo is not None:
        i.non_witness_utxo = None
out = p.serialize()
open(dst, "wb").write(out)
print(f"{before} -> {len(out)} byte")
