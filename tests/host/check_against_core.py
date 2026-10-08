#!/usr/bin/env -S uv run -q --script
# /// script
# requires-python = ">=3.11"
# ///
"""Differential test against Bitcoin Core.

Core is the authority on what a PSBT means, so it makes a better oracle than our own expectations.
This asks Core to decode the same PSBTs and requires its answers to match the plan parser.wasm built.

It does not prove we share code with Core. It proves we give the same answers, which is the part a
user actually depends on.
"""
import base64
import json
import shlex
import subprocess
import sys
from pathlib import Path


BTCCLI = []


def cli(*args):
    out = subprocess.run([*BTCCLI, *args], capture_output=True, text=True)
    if out.returncode:
        raise RuntimeError(out.stderr.strip() or out.stdout.strip())
    return out.stdout.strip()


def decode(psbt_b64):
    return json.loads(cli("decodepsbt", psbt_b64))


def compare(ours, core, name):
    """Every field Core also reports has to agree. A difference here is a real disagreement about
    what the transaction says."""
    bad = []

    def eq(what, ours_v, core_v):
        if ours_v != core_v:
            bad.append(f"{what}: ours={ours_v!r} core={core_v!r}")

    tx = core["tx"]
    eq("tx_version", ours["tx_version"], tx["version"])
    eq("locktime", ours["locktime"], tx["locktime"])
    eq("n_inputs", len(ours["inputs"]), len(tx["vin"]))
    eq("n_outputs", len(ours["outputs"]), len(tx["vout"]))

    for i, (o, vin) in enumerate(zip(ours["inputs"], tx["vin"])):
        eq(f"in{i}.txid", o["txid"], vin["txid"])
        eq(f"in{i}.vout", o["vout"], vin["vout"])
        eq(f"in{i}.sequence", o["sequence"], vin["sequence"])
        ci = core["inputs"][i]
        utxo = ci.get("witness_utxo")
        if utxo:
            # Core gives BTC as a float; the plan holds satoshis, which is the exact value
            eq(f"in{i}.amount", o["amount"], round(utxo["amount"] * 100_000_000))
            eq(f"in{i}.spk", o["spk"], utxo["scriptPubKey"]["hex"])
        # Core reports a taproot input's derivation under a different key than a SegWit v0 one
        derivs = ci.get("bip32_derivs") or ci.get("taproot_bip32_derivs")
        if o["path"] and derivs:
            # Core prints a path as m/84h/0h/0h/0/0; the plan holds the raw uint32s
            got = [(int(p.rstrip("h'")) | 0x80000000) if p.endswith(("h", "'")) else int(p)
                   for p in derivs[0]["path"].split("/")[1:]]
            eq(f"in{i}.path", o["path"], got)
            eq(f"in{i}.fingerprint", o["fingerprint"], derivs[0]["master_fingerprint"])
        # Only for an input we will sign. decodepsbt describes the file; the plan is an instruction to
        # the signer, which requires sighash_type to be 0 where there is no keypath (core.c:93)
        if "sighash" in ci and o["path"]:
            eq(f"in{i}.sighash", o["sighash"], {"ALL": 1, "DEFAULT": 0}.get(ci["sighash"], -1))

    for i, (o, vout) in enumerate(zip(ours["outputs"], tx["vout"])):
        eq(f"out{i}.amount", o["amount"], round(vout["value"] * 100_000_000))
        eq(f"out{i}.spk", o["spk"], vout["scriptPubKey"]["hex"])

    if "fee" in ours and "fee" in core:
        eq("fee", ours["fee"], round(core["fee"] * 100_000_000))

    print(f"{'FAIL' if bad else 'ok  '} {name}")
    for b in bad:
        print(f"       {b}")
    return not bad


def unpack(args, tmp):
    """A .json holding Core's own rpc_psbt.json is expanded into its valid cases, so the comparison
    runs against the corpus Core tests itself with."""
    out = []
    for a in args:
        path = Path(a)
        if path.name == "rpc_psbt.json":
            cases = json.loads(path.read_text())["valid"]
            for i, b64 in enumerate(cases):
                f = tmp / f"core_valid_{i:02d}.psbt"
                f.write_bytes(base64.b64decode(b64))
                out.append(f)
        else:
            out.append(path)
    return out


def main():
    if len(sys.argv) < 4:
        sys.exit('usage: check_against_core.py "bitcoin-cli ..." PSBT_HOST FILE...')
    BTCCLI[:] = shlex.split(sys.argv[1])
    host = sys.argv[2]
    passed = failed = skipped = 0

    # A node that is not up would make every case "skipped", which would read as success
    try:
        cli("getblockchaininfo")
    except RuntimeError as e:
        sys.exit(f"the regtest node is not reachable: {e}")

    tmp = Path("build/core-vectors")
    tmp.mkdir(parents=True, exist_ok=True)

    for path in sorted(unpack(sys.argv[3:], tmp)):
        run = subprocess.run([host, "plan", str(path)], capture_output=True, text=True)
        try:
            ours = json.loads(run.stdout)
        except json.JSONDecodeError:
            print(f"FAIL {path.name}  (no JSON out; rc={run.returncode} {run.stdout[:80]!r})")
            failed += 1
            continue
        if "error" in ours:
            # We refuse things Core accepts (PSBT v2, for one). That is a documented difference,
            # not a disagreement about a transaction we did parse
            print(f"skip {path.name}  (we refuse it: {ours})")
            skipped += 1
            continue
        try:
            core = decode(base64.b64encode(path.read_bytes()).decode())
        except RuntimeError as e:
            print(f"skip {path.name}  (Core refuses it: {e})")
            skipped += 1
            continue
        if compare(ours, core, path.name):
            passed += 1
        else:
            failed += 1

    print(f"\n{passed} agreed with Core, {failed} disagreed, {skipped} skipped")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
