# /// script
# dependencies = ["embit"]
# ///
"""署名までの一巡を検査する PSBT を build/psbt/ に書く。自分の seed 向けの PSBT を embit で組む。
PSBT の解析器単体の検査（Bitcoin Core の rpc_psbt.json など）は wasm-psbt-parser 側にある。"""
import hashlib, os, sys
from embit import bip32, script
from embit.psbt import PSBT, DerivationPath
from embit.transaction import Transaction, TransactionInput, TransactionOutput

MN = " ".join(["abandon"] * 11 + ["about"])
root = bip32.HDKey.from_seed(hashlib.pbkdf2_hmac("sha512", MN.encode(), b"mnemonic", 2048))
FP = root.my_fingerprint
H = 0x80000000
out_dir = sys.argv[1]
os.makedirs(out_dir, exist_ok=True)

def key(path):
    return root.derive(path).key.get_public_key()

def path_list(path):
    return [int(p[:-1]) + H if p.endswith("h") else int(p) for p in path.split("/")[1:]]

def spk(path):
    return script.p2tr(key(path)) if path.startswith("m/86h") else script.p2wpkh(key(path))

def prev_tx(target_spk, amount, vout, salt):
    outs = [TransactionOutput(1000 + i, script.Script(b"\x00\x14" + bytes([salt]) * 20)) for i in range(vout)]
    outs.append(TransactionOutput(amount, target_spk))
    return Transaction(vin=[TransactionInput(bytes([salt]) * 32, 0)], vout=outs)

def build(name, inputs, outputs):
    """inputs: (path または None=他人, amount, vout, nwu)。outputs: (path または None=外部, amount)"""
    vin, prevs = [], []
    for n, (path, amount, vout, _) in enumerate(inputs):
        s = spk(path) if path else script.p2wpkh(key("m/0h/%d" % n)) # 他人の鍵の代わり（fingerprint を載せない）
        ptx = prev_tx(s, amount, vout, 0x30 + n)
        prevs.append((ptx, vout, s, amount))
        vin.append(TransactionInput(ptx.txid(), vout, sequence=0xfffffffd))
    vout = []
    for n, (path, amount) in enumerate(outputs):
        vout.append(TransactionOutput(amount, spk(path) if path else script.p2wpkh(key("m/1h/%d" % n))))
    psbt = PSBT(Transaction(version=2, vin=vin, vout=vout))
    for inp, (path, _, _, nwu), (ptx, v, s, amount) in zip(psbt.inputs, inputs, prevs):
        inp.witness_utxo = TransactionOutput(amount, s)
        if nwu:
            inp.non_witness_utxo = ptx
        if path and path.startswith("m/86h"):
            inp.taproot_bip32_derivations[key(path)] = ([], DerivationPath(FP, path_list(path)))
        elif path:
            inp.bip32_derivations[key(path)] = DerivationPath(FP, path_list(path))
    for o, (path, _) in zip(psbt.outputs, outputs):
        if path and path.startswith("m/86h"):
            o.taproot_bip32_derivations[key(path)] = ([], DerivationPath(FP, path_list(path)))
        elif path:
            o.bip32_derivations[key(path)] = DerivationPath(FP, path_list(path))
    open(os.path.join(out_dir, name + ".psbt"), "wb").write(psbt.serialize())

A = "m/84h/0h/0h"
T = "m/86h/0h/0h"
build("own_p2wpkh_1in", [(A + "/0/0", 100000, 0, False)], [(None, 60000), (A + "/1/0", 39000)])
build("own_p2wpkh_2in_nwu", [(A + "/0/0", 100000, 1, True), (A + "/0/1", 50000, 0, True)],
      [(None, 60000), (A + "/1/0", 89000)])
build("own_p2wpkh_2in_no_nwu", [(A + "/0/0", 100000, 1, False), (A + "/0/1", 50000, 0, False)],
      [(None, 60000), (A + "/1/0", 89000)])
build("own_p2tr_2in", [(T + "/0/0", 70000, 0, False), (T + "/0/1", 70000, 2, False)],
      [(None, 100000), (T + "/1/0", 39000)])
build("own_mixed_nwu", [(A + "/0/0", 100000, 0, True), (T + "/0/0", 70000, 1, True)],
      [(None, 100000), (A + "/0/5", 20000), (T + "/1/0", 49000)])
build("own_with_foreign_input", [(A + "/0/0", 100000, 0, True), (None, 30000, 0, True)],
      [(None, 100000), (A + "/1/0", 29000)])
