"""psbt_host parse の結果を Bitcoin Core の rpc_psbt.json と突き合わせる。
invalid は全て拒否（parser.wasm が解釈しない MuSig2 フィールドだけは素通しを許す）、valid は受理か、
未対応（PSBT v2）/ utxo 無し / 入力 0 個で拒否。どちらも trap は不可。"""
import json, re, sys

vectors = json.load(open(sys.argv[1]))
UNSUPPORTED, TX, UTXO = 5, 4, 7
# invalid_with_msg[15] の不正箇所は PSBT_IN_MUSIG2_PARTIAL_SIG（0x1c）の値長で、メッセージに musig2 を含まない
MUSIG2_BY_FIELD = {15}
res, cur = {}, None
for line in open(sys.argv[2]):
    m = re.match(r".*/rpc_(\w+?)_(\d+)\.psbt (\w+)", line)
    if m:
        cur = (m.group(1), int(m.group(2)))
        res[cur] = [m.group(3), None]
    elif line.strip().startswith("rc=") and cur:
        res[cur][1] = int(line.split("=")[1])
bad = []
for (kind, i), (status, rc) in sorted(res.items()):
    if status == "trap":
        bad.append((kind, i, "trap"))
    elif kind.startswith("invalid") and status == "accepted":
        msg = vectors["invalid_with_msg"][i][1] if kind == "invalid_with_msg" else ""
        if "musig2" not in msg.lower() and i not in MUSIG2_BY_FIELD:
            bad.append((kind, i, "accepted"))
    elif kind == "valid" and status == "rejected" and rc not in (UNSUPPORTED, UTXO, TX):
        bad.append((kind, i, f"rejected rc={rc}"))
n = {k: sum(1 for key in res if key[0] == k) for k in ("invalid", "invalid_with_msg", "valid")}
print(f"rpc_psbt.json: {n}, unexpected: {bad}")
sys.exit(1 if bad or not res else 0)
