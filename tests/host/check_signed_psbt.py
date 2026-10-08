# /// script
# dependencies = ["embit"]
# ///
"""署名済み PSBT を embit で独立に検証する。ECDSA は embit 自身の RFC6979 署名とバイト一致、
Schnorr は embit が計算した BIP341 sighash に対して出力鍵で検証する。"""
import glob, hashlib, sys
from embit import bip32, ec
from embit.psbt import PSBT

MN = " ".join(["abandon"] * 11 + ["about"])
root = bip32.HDKey.from_seed(hashlib.pbkdf2_hmac("sha512", MN.encode(), b"mnemonic", 2048))
failed = 0
for signed_path in sorted(glob.glob(sys.argv[1] + "/own_*.signed")):
    raw_orig = open(signed_path[: -len(".signed")] + ".psbt", "rb").read()
    signed, orig, ref = PSBT.parse(open(signed_path, "rb").read()), PSBT.parse(raw_orig), PSBT.parse(raw_orig)
    ref.sign_with(root)
    report = []
    for i, (s, o, r) in enumerate(zip(signed.inputs, orig.inputs, ref.inputs)):
        new_ecdsa = {k: v for k, v in s.partial_sigs.items() if k not in o.partial_sigs}
        tap_sig = s.unknown.get(b"\x13")
        for pub, sig in new_ecdsa.items():
            ok = sig == r.partial_sigs.get(pub) and pub.verify(ec.Signature.parse(sig[:-1]), signed.sighash(i, sig[-1]))
            report.append(f"in{i} ecdsa {'ok' if ok else 'NG'}")
            failed += not ok
        if tap_sig:
            spk = s.witness_utxo.script_pubkey.data
            ht = tap_sig[64] if len(tap_sig) == 65 else 0
            ok = ec.PublicKey.from_xonly(spk[2:]).schnorr_verify(ec.SchnorrSig.parse(tap_sig[:64]), signed.sighash(i, ht))
            report.append(f"in{i} schnorr {'ok' if ok else 'NG'}")
            failed += not ok
        if not new_ecdsa and not tap_sig:
            report.append(f"in{i} unsigned")
    # 署名済み PSBT は、元の PSBT に署名のキーと値を足しただけであること
    same = signed.tx.serialize() == orig.tx.serialize() and len(signed.inputs) == len(orig.inputs)
    failed += not same
    print(signed_path.split("/")[-1], " ".join(report), "" if same else "TX CHANGED")
sys.exit(1 if failed else 0)
