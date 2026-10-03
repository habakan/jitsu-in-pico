# /// script
# dependencies = ["embit"]
# ///
"""components/signer/tests 用の期待値を C ヘッダにする。BIP341 は公式 JSON、BIP84 / BIP86 の鍵とスクリプトは embit で独立に計算する。"""
import hashlib, json, sys
from embit import bip32, script

MN = " ".join(["abandon"] * 11 + ["about"])
seed = hashlib.pbkdf2_hmac("sha512", MN.encode(), b"mnemonic", 2048)
root = bip32.HDKey.from_seed(seed)

def c_bytes(b):
    return "{" + ",".join(f"0x{x:02x}" for x in b) + "}"

out = ["/* tools/gen_core_vectors.py が生成 */", f"static const uint8_t TV_SEED[64] = {c_bytes(seed)};",
       f"static const uint32_t TV_FP = 0x{root.my_fingerprint.hex()};"]

spks = {}
for name, path, kind in (("P2WPKH_0_0", "m/84h/0h/0h/0/0", "wpkh"), ("P2WPKH_0_1", "m/84h/0h/0h/0/1", "wpkh"),
                         ("P2WPKH_1_0", "m/84h/0h/0h/1/0", "wpkh"), ("P2WPKH_ACCT1_1_0", "m/84h/0h/1h/1/0", "wpkh"),
                         ("P2TR_0_0", "m/86h/0h/0h/0/0", "tr"), ("P2TR_0_1", "m/86h/0h/0h/0/1", "tr"),
                         ("P2TR_1_0", "m/86h/0h/0h/1/0", "tr")):
    pub = root.derive(path).get_public_key()
    spk = (script.p2wpkh(pub) if kind == "wpkh" else script.p2tr(pub)).data
    spks[name] = spk
    out.append(f"static const uint8_t TV_SPK_{name}[{len(spk)}] = {c_bytes(spk)};")
# BIP86 の文書に載っている m/86'/0'/0'/0/0 の scriptPubKey と一致することを確かめる
assert spks["P2TR_0_0"].hex() == "5120a60869f0dbcf1dc659c9cecbaf8050135ea9e8cdc487053f1dc6880949dc684c"

# base58check の期待値（P2PKH / P2SH、mainnet / testnet）
from embit.networks import NETWORKS
out.append("static const struct { uint8_t len; uint8_t spk[25]; int testnet; const char *addr; } TV_B58[] = {")
for spk_hex in ("76a9148280b37df378db99f66f85c95a783a76ac7a6d5988ac", "a914" + "00" * 19 + "0187",
                "76a914" + "00" * 20 + "88ac"):
    spk = bytes.fromhex(spk_hex)
    for testnet, net in ((0, "main"), (1, "test")):
        addr = script.Script(spk).address(NETWORKS[net])
        out.append(f"    {{{len(spk)}, {c_bytes(spk)}, {testnet}, \"{addr}\"}},")
out.append("};")

d = json.load(open(sys.argv[1]))["keyPathSpending"][0]
raw = bytes.fromhex(d["given"]["rawUnsignedTx"])
out.append(f"static const uint8_t TV341_TX[{len(raw)}] = {c_bytes(raw)};")
out.append("static const struct { uint64_t amount; uint8_t len; uint8_t spk[34]; } TV341_UTXOS[] = {")
for u in d["given"]["utxosSpent"]:
    s = bytes.fromhex(u["scriptPubKey"])
    out.append(f"    {{{u['amountSats']}, {len(s)}, {c_bytes(s)}}},")
out.append("};")
out.append("static const struct { uint32_t index; uint8_t hash_type; uint8_t sighash[32]; uint8_t tweaked[32]; "
           "uint8_t sig_len; uint8_t sig[65]; } TV341_SPENDS[] = {")
for s in d["inputSpending"]:
    g, i, e = s["given"], s["intermediary"], s["expected"]
    sig = bytes.fromhex(e["witness"][0])
    out.append(f"    {{{g['txinIndex']}, {g['hashType']}, {c_bytes(bytes.fromhex(i['sigHash']))}, "
               f"{c_bytes(bytes.fromhex(i['tweakedPrivkey']))}, {len(sig)}, {c_bytes(sig)}}},")
out.append("};")
open(sys.argv[2], "w").write("\n".join(out) + "\n")
