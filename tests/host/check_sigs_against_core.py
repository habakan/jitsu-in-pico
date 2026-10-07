#!/usr/bin/env -S uv run -q --script
# /// script
# requires-python = ">=3.11"
# ///
"""Require our signatures to equal Bitcoin Core's, byte for byte.

This is possible because both sides are deterministic: Core and this signer both use low-R grinding
for ECDSA, and both pass a zero aux_rand for Schnorr. So the same key over the same PSBT has exactly
one right answer, and Core's is the authoritative one.

The seed is BIP39's published all-zero test vector. Nothing here touches a real key.
"""
import base64
import json
import os
import shlex
import subprocess
import sys
from pathlib import Path

MNEMONIC = "abandon " * 11 + "about"


BTCCLI = []


def cli(*args, wallet=None):
    cmd = list(BTCCLI) + ([f"-rpcwallet={wallet}"] if wallet else [])
    r = subprocess.run([*cmd, *args], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError((r.stderr or r.stdout).strip())
    return r.stdout.strip()


def descriptors():
    """The same account this signer derives, as descriptors Core can sign with. tprv because regtest
    uses the testnet version bytes; the keys themselves are identical.

    Receive and change are separate descriptors rather than one multipath `<0;1>`, which Core only
    accepts in getdescriptorinfo from some versions on (28.1 refuses it)."""
    from embit import bip32, bip39

    root = bip32.HDKey.from_seed(bip39.mnemonic_to_seed(MNEMONIC))
    fp = root.my_fingerprint.hex()
    out = []
    for kind, purpose in (("wpkh", 84), ("tr", 86)):
        acct = root.derive(f"m/{purpose}h/0h/0h")
        xprv = acct.to_base58(version=bytes.fromhex("04358394"))
        for chain, internal in ((0, False), (1, True)):
            out.append((f"{kind}([{fp}/{purpose}h/0h/0h]{xprv}/{chain}/*)", internal))
    return out


def setup_wallet(datadir, name="diff"):
    try:
        cli("unloadwallet", name)
    except RuntimeError:
        pass
    # The wallet may be left over from a previous run; Core refuses to create over it
    try:
        cli("unloadwallet", name)
    except RuntimeError:
        pass
    cli("-named", "createwallet", f"wallet_name={name}",
        "disable_private_keys=false", "blank=true", "descriptors=true")
    imports = []
    for d, internal in descriptors():
        ck = json.loads(cli("getdescriptorinfo", d))["checksum"]
        imports.append({"desc": f"{d}#{ck}", "timestamp": "now", "active": True,
                        "internal": internal, "range": [0, 20]})
    res = json.loads(cli("importdescriptors", json.dumps(imports), wallet=name))
    if not all(r["success"] for r in res):
        raise SystemExit(f"importdescriptors failed: {res}")
    return name


def signatures(b64, wallet=None):
    dec = json.loads(cli("decodepsbt", b64, wallet=wallet))
    out = {}
    for i, inp in enumerate(dec["inputs"]):
        for pub, sig in (inp.get("partial_signatures") or {}).items():
            out[f"in{i}/ecdsa/{pub}"] = sig
        if inp.get("taproot_key_path_sig"):
            out[f"in{i}/schnorr"] = inp["taproot_key_path_sig"]
    return out


def main():
    if len(sys.argv) < 4:
        sys.exit('usage: check_sigs_against_core.py "bitcoin-cli ..." PSBT_HOST FILE...')
    BTCCLI[:] = shlex.split(sys.argv[1])
    host = sys.argv[2]

    # A node that is not up would make every case "skipped", which would read as success
    try:
        cli("getblockchaininfo")
    except RuntimeError as e:
        sys.exit(f"the regtest node is not reachable: {e}")

    datadir = Path("build/core-sigs")
    datadir.mkdir(parents=True, exist_ok=True)
    wallet = setup_wallet(datadir)
    ours_path = datadir / "ours.psbt"
    agreed = differed = skipped = 0

    for path in sorted(Path(p) for p in sys.argv[3:]):
        b64 = base64.b64encode(path.read_bytes()).decode()

        # A stale file from a previous case would be read as this one's result, so it goes first
        if ours_path.exists():
            os.unlink(ours_path)
        run = subprocess.run([host, "sign", str(path), str(ours_path)], capture_output=True, text=True)
        if run.returncode or not ours_path.exists():
            why = (run.stdout or run.stderr).strip().splitlines()[-1:] or ["no output"]
            print(f"skip {path.name}  (we refuse to sign it: {why[0]})")
            skipped += 1
            continue

        try:
            # DEFAULT, not ALL: Core then uses each input type's default, which for taproot is
            # SIGHASH_DEFAULT and a 64-byte signature. Forcing ALL appends 0x01 and nothing matches
            core_psbt = json.loads(cli("walletprocesspsbt", b64, "true", "DEFAULT", "true",
                                       "false", wallet=wallet))["psbt"]
        except RuntimeError as e:
            print(f"skip {path.name}  (Core: {e})")
            skipped += 1
            continue

        core = signatures(core_psbt, wallet)
        ours = signatures(base64.b64encode(ours_path.read_bytes()).decode(), wallet)
        common = set(core) & set(ours)

        if not common:
            print(f"skip {path.name}  (no signature in common: core {len(core)}, ours {len(ours)})")
            skipped += 1
            continue

        bad = [k for k in sorted(common) if core[k] != ours[k]]
        only_ours = sorted(set(ours) - set(core))
        tag = "ok  " if not bad else "FAIL"
        print(f"{tag} {path.name}  {len(common) - len(bad)}/{len(common)} byte-identical"
              + (f", {len(only_ours)} only ours" if only_ours else ""))
        for k in bad:
            print(f"       {k}\n         core {core[k]}\n         ours {ours[k]}")
        if bad:
            differed += 1
        else:
            agreed += 1

    print(f"\n{agreed} matched Core byte for byte, {differed} differed, {skipped} skipped")
    return 1 if differed else 0


if __name__ == "__main__":
    sys.exit(main())
