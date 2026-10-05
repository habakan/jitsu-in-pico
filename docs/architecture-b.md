# Architecture B: isolate the parser in WASM, keep keys and signing native

Version 0.3 | 2026-09-22 | Status: §8 decided; the native core and parser.wasm are implemented (§10)

design.md §3, §7 and §8 are written for plan A, where the whole of the signing logic goes into
`bitcoin-signer.wasm`. This document defines plan B, which replaces it. Once the open decisions are
settled they get folded back into design.md.

## 1. The point

Give WASM one job: when the parser of untrusted input is taken over, it still cannot reach the key.
Under plan A the PSBT parser and the key sit in the same module, so a bug in the parser leaks the key
through a nonce or through the output QR.

## 2. What this defends against, and what it does not

| attack | under plan B |
|---|---|
| a crafted PSBT or UR takes over the parser and reads the key | defended. The key never enters the WASM linear memory |
| a compromised parser shows A on screen and signs B | defended. What is displayed and what goes into the sighash are built natively from the same struct (§5) |
| a compromised parser honestly displays a transaction the user should not accept | not defended. That is what the user's confirmation is for |
| a compromised parser corrupts the output PSBT | not defended. A signature commits to the transaction, so a corrupted PSBT is merely invalid — denial of service |
| the QR decoder (quirc) is taken over | not defended. quirc stays native and stays in the TCB (§7) |
| the SeedQR parser is taken over | out of scope, because that parser is not in WASM (§4) |

## 3. The shape of it

```
Camera ─> quirc (native) ─> QR payload bytes
                                 │
                                 ▼
             ┌──────── parser.wasm (no keys) ───────┐
             │ UR / BBQr reassembly, CBOR, PSBT     │
             │ parsing, then a fixed-length Plan    │
             └───────────────┬──────────────────────┘
                             │ the host copies the plan and non_witness_utxo, with checks
                             ▼
             ┌──────── signing core (native) ───────┐
             │ check the Plan, derive keys, confirm  │
             │ ownership, build the display, sighash,│
             │ sign                                  │
             └───────────────┬──────────────────────┘
                             │ the host writes back the signatures (plan_sig_t)
                             ▼
             parser.wasm: insert partial_sig into the original PSBT and serialize
                             │
                             ▼
                     qrcodegen (native) ─> LCD
```

parser.wasm has no imports at all. Everything crosses through buffers it exports, which the host reads
and writes. The host checks every address and length it returns against the bounds of the linear memory
with `wasm_runtime_validate_app_addr` before copying anything.

## 4. Who does what

| what | where | why |
|---|---|---|
| decoding the QR image (quirc) | native | over ten seconds per frame in WASM (qr-feasibility.md) |
| UR / BBQr reassembly, CBOR, PSBT parsing | parser.wasm | the most complex parsing of untrusted input, and light because no crypto is involved |
| parsing xpubs and descriptors (multisig) | parser.wasm | untrusted input; the result crosses as a fixed-length record, like the Plan |
| parsing a SeedQR or typed words | native | the input is the secret itself, so putting it in WASM would defeat the isolation |
| checking the Plan, deriving keys, confirming ownership, sighash, signing | native | it touches keys |
| building address strings (bech32 / base58) | native | the Plan carries only the scriptPubKey's bytes, so the parser cannot fake a string |
| the wording and numbers on the review screens | native | so that what is shown and what is signed come from the same struct |
| serializing the signed PSBT | parser.wasm | a signature commits to the transaction, so a compromise here cannot forge anything |

## 5. The core invariant

**What is displayed and what goes into the sighash are built natively, from the same `plan_t`.**
If parser.wasm emits a Plan that differs from the original PSBT, then that Plan is what the user sees
and that Plan is what gets signed. The native side does not need to establish that the Plan is faithful
to the PSBT. It only establishes two things:

1. that the Plan is internally consistent (§6)
2. that nothing bad happens if a value the user is not shown, such as an input amount, is a lie
   (§6, items 4 and 5)

## 6. What the native side checks

1. **Form:** record counts and lengths within their limits, unused fields zero, no reserved values.
   Nothing variable-length is parsed
2. **Inputs to be signed:** derive the key at the path the Plan gives and confirm the public key
   produces `witness_utxo.scriptPubKey` — `0014 || HASH160(pub)` for P2WPKH, the x-only key after the
   BIP86 tweak for P2TR. An input that does not match is not signed
3. **Deciding what is change:** an output's `key` is treated as a hint only. The native side re-derives
   the script on the same account's change chain (`.../1/i`, with a limit on `i`) and only calls it
   change when it matches
4. **The fee:** total input minus total output, computed natively. Overflow and negative values are
   refused
5. **Guarding against a lie about an input's amount:**
   - Taproot (BIP341) commits the sighash to every input's amount and scriptPubKey, so a signature made
     with a false amount is simply invalid
   - SegWit v0 (BIP143) commits only to the amount of the input being signed. That is what makes the
     2020 fee attack work: get the same input signed twice with different amounts. The countermeasure
     is decided in §8-1
6. **sighash type:** only `SIGHASH_ALL` and Taproot's `SIGHASH_DEFAULT` are accepted; anything else
   would need a future setting
7. **Network:** mainnet or testnet is decided by the native configuration, not by the Plan

## 7. What risk remains

- **quirc remains a parser of untrusted input inside the TCB.** About 3,000 lines including headers.
  Fuzzing makes up for it; in WASM it is far too slow
- **The code that reads the Plan also reads untrusted input natively.** Restricted to fixed-length
  records and kept to around a hundred lines
- **An input that makes parser.wasm not terminate.** Whether WAMR has an instruction limit needs
  checking; without one, a UI timeout has to interrupt it
- **A vulnerability in WAMR itself.** If the linear memory's bounds checks can be broken, the isolation
  no longer holds. Which of the interpreter and AOT is used changes the TCB (§8-3)

## 8. Decided (2026-09-22)

1. **`non_witness_utxo` for SegWit v0 inputs:** required for every input when a SegWit v0 input is to
   be signed and there are two or more inputs. With a single input it is unnecessary, since a signature
   made with a false amount is merely invalid. The minimal native tx parser
   (`components/parser/src/tx.c`, shared with jitsu-in) checks the txid, vout, amount and script
2. **First targets:** P2WPKH (BIP84) and P2TR (BIP86, no script tree)
3. **parser.wasm's runtime:** the interpreter. Measured on RV32 after implementation: 2.7M instructions
   to parse a PSBT and 40k to insert the signatures (§10), which is plenty
4. **multisig:** not in the first version

## 9. The Plan's layout (proposed)

The C struct is shared as is, with its size and offsets nailed down by `_Static_assert`. wasm32 and
rv32 are both little-endian ILP32, so the layout agrees. The native side copies it once, then checks it.

```c
#define PLAN_MAX_INPUTS  16
#define PLAN_MAX_OUTPUTS 16
#define PLAN_MAX_SPK     83   /* the standard OP_RETURN limit; P2TR and P2WSH need 34 */
#define PLAN_MAX_DEPTH   8

typedef struct {
    uint8_t  len;
    uint8_t  bytes[PLAN_MAX_SPK];
} plan_script_t;

typedef struct {
    uint8_t  depth;
    uint32_t fingerprint;
    uint32_t path[PLAN_MAX_DEPTH];
} plan_keypath_t;

typedef struct {
    uint8_t  prev_txid[32];
    uint32_t prev_vout;
    uint32_t sequence;
    uint64_t amount;            /* witness_utxo */
    plan_script_t spk;          /* witness_utxo */
    plan_keypath_t key;         /* depth = 0 for an input we will not sign */
    uint8_t  sighash_type;
} plan_input_t;

typedef struct {
    uint64_t amount;
    plan_script_t spk;
    plan_keypath_t key;         /* a change candidate; the native side re-derives to confirm */
} plan_output_t;

typedef struct {
    uint32_t magic, version;
    int32_t  tx_version;
    uint32_t locktime;
    uint8_t  n_inputs, n_outputs;
    plan_input_t  inputs[PLAN_MAX_INPUTS];
    plan_output_t outputs[PLAN_MAX_OUTPUTS];
} plan_t;
```

- The settled version is `components/parser/include/plan.h`, on the jitsu-in side, with
  `_Static_assert` fixing the size and the offsets
- At 16 inputs and 16 outputs it is 5,016 bytes: 176 per input, 136 per output. Barely a dent in RAM
- Those limits are provisional, to be settled against SeedSigner's and Krux's limits and against real
  PSBTs
- `non_witness_utxo` crosses in a buffer of its own rather than in the Plan, with the same limit as the
  whole PSBT

## 10. Implementation status

The native core is implemented in `components/signer/` (`make check-core`, `make check-qemu-core`).

| file | what it holds |
|---|---|
| `components/signer/core.c` | the checks from §6 (`core_review`), the display model (`core_display`) and signing (`core_sign`). It records the SHA-256 of the reviewed plan and refuses to display or sign anything that does not match |
| `components/signer/address.c` | addresses from a scriptPubKey: base58check for P2PKH and P2SH, bech32 for witness v0, bech32m for v1-v16. Nothing is produced for a non-standard script |
| `components/signer/sighash.c` | BIP143 (P2WPKH, SIGHASH_ALL) and BIP341 key path, all seven hash types |
| `components/parser/src/tx.c` | the minimal tx parser, shared with jitsu-in. Refuses non-minimal varints and trailing bytes; the txid is computed without the witness |
| `components/signer/bip32.c` | BIP32 derivation, shared with plan A's `signer.c` |
| `components/parser/src/sha256.c`, `components/signer/ripemd160.c`, `sha512.c` | the hashes. Secrets are cleared through `components/signer/wipe.h`, via a volatile pointer, so the optimiser cannot remove it |

What `core_review` actually does, making §6 concrete:

- An input that does not claim our fingerprint is not signed; its amount is used only for the fee. One
  that does claim it, but whose key does not produce its script, rejects the whole plan
- sighash types: `ALL` only for P2WPKH, `DEFAULT` and `ALL` only for P2TR
- An output counts as ours at `m/84'|86' / coin' / account' / {0|1} / i` — coin from the network
  setting, `i < 100000` — on the same account as the inputs being signed, and only when the script
  matches. Chain 1 is change, chain 0 is a self-transfer, which is what SeedSigner confirms as such.
  Anything else is external
- Every string on the review screens is built natively from the plan's bytes. A script that has no
  address is shown as the data for an OP_RETURN, or as the whole script in hex otherwise. The amount
  spent (`spend`) is the total of the external outputs

Tests: 71 checks, passing on both the Mac and RV32:

- BIP143's P2WPKH example: the sighash and the RFC6979 signature (DER) match the document
- The seven key-path cases from BIP341's wallet test vectors: sighash and Schnorr signature match, for
  every hash type
- BIP84 and BIP86: with the key derived from `abandon ... about`, review and signing pass and the
  signature verifies. The expected scripts are computed independently with embit and cross-checked
  against the values in BIP86 (`tools/gen_core_vectors.py`)
- Addresses: BIP350's eight valid vectors; that invalid program lengths (1 and 41 bytes, and 16 and 21
  at v0) produce nothing; base58check, with embit for the expected values; the known addresses from
  BIP84 and BIP86
- Attack scenarios: swapping the plan after review, for both display and sign; fake change; a
  self-transfer; the index limit; change on another account; a key that does not match its script; a
  sighash type we do not allow; a non-zero unused field; outputs exceeding inputs; and the fee attack
  with no previous transaction, with a false amount, with the wrong vout and with the wrong txid
- Five deliberate breakages — BIP143's hash type, BIP341's spend_type, the fee-attack check, bech32m's
  constant, base58's leading zeros — were each confirmed to make a test fail

### parser.wasm (`components/parser/src/psbt.c`, the [jitsu-in](https://github.com/habakan/jitsu-in) submodule)

- Turns a PSBT v0 (BIP174) into a `plan_t`, and inserts the natively produced signatures just before
  the end of each input map, leaving every other byte as it was
- At this stage the `.wasm` was 7KB with no imports. It holds the input and output buffers
  (`PSBT_MAX` 32KB), so the linear memory is two pages
- Fields it interprets are checked strictly: duplicate keys, key and value lengths per type,
  v2-only fields, scriptSig and witness in the unsigned tx, the `non_witness_utxo`'s txid against
  `witness_utxo`, and trailing bytes
- Fields it does not interpret, MuSig2's among them, pass through untouched. It does not check whether
  a public key is on the curve, because the native side never uses the PSBT's public keys — it derives
  its own
- The only candidates for our keys are derivations matching the master fingerprint the host supplied,
  which is not a secret. Inputs already finalized or signed, and P2TR with a script tree, are not signed
- ECDSA uses the same low-R grinding as Bitcoin Core (`components/signer/core.c`), so the signatures
  match embit's byte for byte

Verification (`make check-psbt`, `make check-qemu-psbt`):

- Six PSBTs built with embit for our own seed — P2WPKH with one and two inputs, P2TR with two, a mix,
  one including someone else's input, and two inputs with no `non_witness_utxo` — go through a full
  round, and the signed PSBT is verified independently with embit (`tools/check_signed_psbt.py`). The
  ECDSA signatures match embit's own byte for byte, and the Schnorr ones verify against embit's BIP341
  sighash. The two-input case with no `non_witness_utxo` is refused with `CORE_ERR_PREVTX_MISSING`
- Against Bitcoin Core's `test/functional/data/rpc_psbt.json` — 84 invalid and 48 valid, two of which
  have corrupt base64 and are excluded — there are no traps. Every invalid one is refused except the 15
  with MuSig2 fields. Of the valid ones 31 are accepted; the 14 PSBT v2 cases, two with no utxo and one
  with no inputs are refused (`make check-parser` in jitsu-in, `make check-parser` here)
- A full round on a mixed PSBT under RV32 (QEMU): parse 2.72M, review 17.1M, sign 14.5M, finalize 0.04M
  instructions. The signed PSBT is byte-identical between the Mac and RV32

### Bitcoin Core as the oracle (2026-10-04)

Core has no WASM target. Its PSBT code does compile to wasm32, though — measured, not assumed, with
our pinned wasi-sdk 34 (2026-10-04):

| | code | data | total | imports |
|---|---:|---:|---:|---:|
| `parser.wasm` | 13,562 | 1,369 | 15,570 | **0** |
| C++ with no STL and no exceptions | 7,658 | 0 | 8,022 | 3 |
| the same plus `string`, `vector`, `map` | 21,343 | 2,459 | 24,316 | 4 |
| `psbt.cpp` and 17 files of its closure | 103,219 | 9,746 | 114,549 | 17 |

Stripped and `wasm-opt -Oz`'d, it is 114,549 bytes, about 7x ours — not the 2.8 MB an unstripped link
reports, 93% of which is debug info. Where the difference comes from: a C++ floor of 7.7KB for
wasi-libc's startup and malloc, 13.7KB more for the STL templates and their allocator, and the rest
Core's own breadth — every script type, finalize, combine, miniscript, against our P2WPKH and P2TR key
path. Template instantiation in `serialize.h`, dynamic allocation and per-function unwind tables are
what grow it; our 13.5KB is barely above the bare C++ floor because there is none of that.

Two things still rule it out.

**Core's script code requires C++ exceptions**, and they do not link. `-fno-exceptions` fails to
compile (`script/script.h:248: cannot use 'throw' with exceptions disabled`), and with them enabled
wasi-sdk 34's prebuilt `libc++abi.a` leaves `_Unwind_RaiseException` and `__cpp_exception` undefined.
The 114,549-byte figure above only links because `--allow-undefined` turns those into imports, so it
is a module that would not run. Closing it means building libc++ from source, which gives up the
pinned official tarball the reproducible build rests on. In wasm, exceptions also mean the
exception-handling proposal, which is not one of Lime1's seven features, so the link-time gate rejects
it anyway — exactly what that gate is for. Clang 23 emits the new form (`try_table`, `exnref`) while
our pinned WAMR implements the old one (`TRY`, `CATCH`, `DELEGATE`), so the two do not even meet.

**On the device it does not fit regardless.** The classic interpreter rewrites bytecode as it loads,
so the module has to sit in writable RAM, and the RP2350 has 520KB in total for everything.

So this could run in a browser, where none of that binds, but never on the device. Patching out the
throws would change the picture — and would mean forking Core, at which point "verifiably the same
code as Core" is the thing that has been lost.

What we do take from Core verbatim is the part where being identical matters most —
**libsecp256k1, pinned to a commit**. Everything else is ours: PSBT parsing, sighash, BIP32, base58
and bech32, the hashes.

So rather than sharing Core's code, we require Core's answers (`make check-core-diff`).

| what is compared | how |
|---|---|
| the parse | `decodepsbt` against the `plan_t` parser.wasm built: version, locktime, and every txid, vout, sequence, amount, scriptPubKey, derivation path and master fingerprint |
| the fee | `decodepsbt`'s fee against `core_review`'s |
| **the signatures** | `walletprocesspsbt`'s against ours, **byte for byte** |

The third is only possible because both sides are deterministic: Core and this signer both grind for a
low R in ECDSA, and both pass a zero `aux_rand` for Schnorr. The same key over the same PSBT therefore
has exactly one right answer, and Core's is the authoritative one.

Measured: **39 PSBTs agree on the parse** — Core's own `rpc_psbt.json` valid cases plus our vectors —
and **8 signatures across 5 PSBTs are byte-identical**, ECDSA and Schnorr alike. Deliberately breaking
the low-R grinding makes two of them differ and the target fail. Notably one case still matched by
chance, because its first attempt already had a low R, so a single test case would have missed it.

Two things this does **not** claim. It does not show we share code with Core; it shows we give the same
answers, which is the part a user actually depends on. And `decodepsbt` describes what is in the file
while `plan_t` is an instruction to the signer, so the two are not the same kind of object: an input we
will not sign carries `sighash_type = 0` whatever the file says, and `core_review` rejects a plan that
breaks that (`core.c:93`). The comparison only requires agreement where both describe the same thing.

### Reassembling a UR, the animated QR

UR (BCR-2020-005) reassembly went into parser.wasm (in jitsu-in). Reassembling a fountain code is
also processing of untrusted input, so it belongs inside the sandbox.

- `parser_ur_reset`, `parser_ur_receive(len)`, `parser_ur_progress`. On completion the PSBT sits in the
  input buffer, ready for `parser_parse`. The accepted types are `crypto-psbt` and `psbt`
- The Xoshiro256**, the alias sampler and the shuffle that decide which fragments were mixed all match
  the expected values in Blockchain Commons' bc-ur tests: 1,148 checks, run natively under ASan and
  UBSan. Dropping one part and feeding the rest in reverse completes at the same 16 parts as the
  reference decoder, @ngraveio/bc-ur 1.1.13
- URs of our own PSBTs made with the reference encoder (`crypto-psbt` and `psbt`, fragments of 60, 150
  and 5000 bytes), fed in with one pure part in three dropped, come back as the original PSBT
- RV32 (QEMU): a mixed PSBT across 19 parts of 60-byte fragments costs 22.16M instructions in total and
  at most 4.23M for one part, which is 30 to 40ms at 150MHz. Signing the reassembled PSBT gives bytes
  identical to signing the binary one
- The `.wasm` reached 14KB, still with no imports. The linear memory in use (`__heap_base`) grew from
  88KB to 158KB, then came back to 132KB by removing the working arrays for the sampling and the
  shuffle. The order of computation did not change, so agreement with the reference still holds. WAMR's
  pool peaks at 147KB under QEMU, and the device application gives it 160KB

### Handing the signed PSBT back as an animated QR

- Encoding to a UR also lives in parser.wasm (`parser_ur_encode_start`, `parser_ur_encode_next`). It is
  formatting output for an untrusted wallet, so it has no bearing on the safety of the signature and can
  sit outside the TCB. Part for part it matches the reference encoder, @ngraveio/bc-ur, and it matches
  bc-ur's examples character for character (tested in jitsu-in)
- One part is 120 bytes, about 300 characters, which lands around QR v8, shown on the 240 px LCD at
  4 px per module (`ui_qr_render_line`, with a four-module quiet zone). Mixed parts keep coming after
  the pure ones, so a wallet that misses one can still reassemble
- Verification: reading the signed PSBT's UR back gives identical bytes, and rendering the LCD's QR
  screen to an image and reading it with zxing-cpp gives the part's string (`make check-psbt`)
- On the hardware (2026-10-01): a phone's QR reader reads the animated QR off the LCD. At 100 bytes per
  part it fits QR v8, 49 modules, which is 4 px per module on a 240 px LCD. At 120 bytes it became v9,
  the scale dropped to 3 px, and it would not read — so **the constraint is keeping the part small
  enough that the version does not go up**. The display cycles only the pure parts at 500ms, which lets
  a receiver that ignores mixed parts still complete, and brings a missed part round again. The output
  decodes with `@ngraveio/bc-ur` 1.1.13, the reference implementation BlueWallet and others use:
  complete in 10 frames, byte-identical to the PSBT. Dropping three frames still reassembles, from the
  mixed parts
- RV32 (QEMU): per frame, about 1.5M instructions to encode the UR and at most about 8M to build the QR.
  At 150MHz that is 60 to 95ms, inside a 250ms animation interval
- The device application: 164KB of flash, 297KB of RAM, with 160KB of that the WAMR pool, which peaks at
  149KB under QEMU. Measurements are in §11

## 11. Measured on the hardware (Pico 2 H, 150MHz, 2026-09-25)

`apps/device/rp2350/psbt_bench.c`, with no screen and no buttons, runs a built-in test PSBT — two
inputs, three outputs, P2WPKH and P2TR mixed — through a full round. The core is native in both
columns; only how parser.wasm executes differs.

| step | where | classic interp | AOT, expanded into RAM |
|---|---|---|---|
| runtime init | - | 22.6 ms | 10.2 ms |
| seed (PBKDF2 and master) | native | 469 ms | 470 ms |
| parsing the PSBT | **wasm** | 26.0 ms | **1.3 ms** |
| checking and building the display | native | 155 ms | 142 ms |
| signing, two inputs | native | 133 ms | 123 ms |
| finalize, inserting the signatures | **wasm** | 0.93 ms | **0.16 ms** |
| UR encoding and QR building, 8 parts | wasm + native | 797 ms, worst 95 ms per part | 689 ms, worst 86 ms per part |
| WAMR pool peak | - | 152,792 B | 196,808 B |
| flash / RAM | - | 158KB / 370KB | 179KB / 357KB |

- Both produce a signed PSBT matching the host's (`build/psbt/own_mixed_nwu.signed`). With the
  interpreter the pool peak matches QEMU exactly
- **Looking only at the wasm, AOT is twenty times faster**: parsing 26.0 to 1.3 ms, finalize 0.93 to
  0.16 ms
- **But across the whole round it is 806 ms against 746 ms, a difference of 8%.** Under plan B the
  parser is light, and the time goes on the native checking and signing and on the seed
- So **with plan B the interpreter is enough for parser.wasm**. AOT costs 44KB of RAM and 21KB of flash
  and puts wamrc and LLVM in the TCB, which that 8% does not justify
- The native side — 142 to 155 ms checking, 123 to 133 ms signing — came out as the QEMU instruction
  counts predicted, 148 ms and 125 ms. The interpreter build is 9% slower because linking differently
  changes how the XIP cache happens to hit
- A QR part takes 86 to 95 ms, inside a 250 ms animation interval, as expected

## 12. A full round on the hardware (2026-10-02)

Reading from the camera, signing, and handing it back as an animated QR all work on the hardware.

```
camera: ready
seed 469365 us, fingerprint 73c5da0a
scan: 7 parts, psbt 744 bytes      <- reassembled from 7 of 8 parts; a mixed part filled the gap
parse 24078 us, pool highmark 152792
review 131043 us
sign -> signed PSBT -> 10 UR parts out
```

- While reading, the camera image goes to the panel with how many of how many parts, and what is being
  detected (`no QR in view` / `QR found, cannot read`), on the first line. Without being able to aim it
  is not usable
- `tools/show_ur.py` turns the UR's parts into an animated GIF, so a PC screen can present it to the
  camera. The UR for an unsigned PSBT comes from `psbt_host bin2ur`
- The menu is `Scan PSBT`, `Sign test PSBT` (only in a TEST_SEED build) and `Lock`

## 13. SeedQR (2026-10-02)

The key can be entered through the camera too. Read on the hardware, the fingerprint matched the test
seed's.

- **It carries the secret itself, so it is handled natively, inside the TCB, and never goes through the
  parser.** This is where it differs from a PSBT
- **The BIP39 checksum is always verified.** A single character misread is refused
- Both a standard SeedQR, 48 or 96 digits of four-digit word indices, and a CompactSeedQR, 16 or 32
  bytes of entropy
- The word list is generated only after checking the official `english.txt` against its SHA-256
  (`tools/gen_bip39_words.py`, 20KB of flash)
- The mnemonic and the seed are cleared with `wipe()` once finished with
- Tested by `make check-seedqr`, 10 checks confirmed independently with embit. **Build it with ASan**:
  an input like index 2048, whose low 11 bits equal a valid value, passes the checksum and reads past
  the end of the word list, so watching only the return value hides a missing range check

## 14. Interoperating with a real wallet (2026-10-02)

Connected to Sparrow on signet, a transaction signed on the hardware went out to the network.

| step | what happened |
|---|---|
| the key | a SeedQR made from 12 words generated in Sparrow, read with the device's camera. The fingerprints matched |
| the PSBT | Sparrow's **Show QR**, a UR animated QR, read with the device's camera |
| review | destination, amount and fee on the panel. Outputs to ourselves showed as `Self-transfer` and change as `Change` |
| signing | on the device, natively |
| back again | the animated QR on the device's panel taken in with Sparrow's **Scan QR**, then broadcast |

The result: [`d22944da...`](https://mempool.space/signet/tx/d22944daeb353fc4e02012f080c632ddc8973b0395442f59d55e091a78610253),
one input and two outputs, 140 vbytes, a fee of 136 sats. **Confirmed in block 324603.** The witness
carried its two elements and a node verified the signature and accepted it.

- A `TESTNET=1` build targets signet and testnet: `m/84'/1'/0'`, with `tb1...` addresses.
  `core_review` has always checked the derivation path's coin type against the network
- Set Sparrow's **Preferences → Appearance → QR Density** to `Low`. The device's camera, QVGA plus
  quirc, reads reliably up to about v8, 49 modules
- **Not for mainnet yet.** With a Debug Probe attached the RAM can be read, and there is no case and no
  PIN. Using this for real needs the SWD detached and a third-party review; the xpub export was
  finished on 2026-10-03

## 15. What is next

What remains as of the signet round on 2026-10-02, in the order that matters. How this is positioned,
and what publishing it requires, is in [positioning.md](positioning.md).

### A. Prerequisites for mainnet — no real key goes in until these are met

On 2026-10-03 a full signet round ran with no key on the PC at all ([the procedure](signet.md)). What
remains is detaching the SWD and a third-party review.

1. ~~**Exporting an xpub for watch-only use.**~~ Done (2026-10-03). The menu's `Show xpub` shows the
   full `m/84'/coin'/0'` xpub and a QR of the output descriptor
   `wpkh([fp/84h/coin h/0h]xpub/<0;1>/*)`. At 145 characters that is 45 modules, so one still image.
   Matches BIP84's own vectors (`make check-xpub`). `UR:CRYPTO-ACCOUNT` can be added later if wanted
2. ~~**Detaching the Debug Probe (SWD).**~~ Partly done (2026-10-03); see "Using this for real" below.
   What remains is deciding whether to disable SWD permanently
3. **A third-party review.** `core_review` (the fee attack, deciding what is change),
   `apps/device/runtime/host-abi` (the boundary with WASM) and the UR parsing have only ever been
   checked by our own tests
4. ~~**Randomness for Schnorr's aux.**~~ Done (2026-10-03); see "Defences around signing" below

### Using this for real (2026-10-03)

**With a Debug Probe attached the RAM can be read, which exposes the seed.** No design choice prevents
that, so it is handled by how the device is used, and by leaving fewer traces.

1. **Build with `TEST_SEED=0`.** That is now the default (`make build/rp2350/app.elf`). It used to be
   hardcoded to 1 in CMakeLists, which meant any build could select the test seed. For development,
   say so explicitly: `make run TEST_SEED=1 TESTNET=1`
2. **Physically remove the Debug Probe.** Unplug the cable after flashing. The UART log goes with it
3. **Finish with `Lock (wipe seed)`.** Always, when done. `core_unload` zeroes the key

Traces of the seed that were being left behind now get cleared too.

| what was left | what now happens |
|---|---|
| `quirc_code` / `quirc_data`, static, holding the SeedQR's 48 digits verbatim | `wipe` once read |
| the captured camera frame, with the SeedQR in it | `wipe` before release |
| the mnemonic and the seed, already cleared | unchanged |

What goes to the UART is the fingerprint and some timings — never the seed or the mnemonic. Confirmed.

#### Measured (2026-10-04)

What had been written as an assumption was checked on the hardware. `tools/ram_scan.py` and OpenOCD
dump all 520KB of SRAM, and the values derived from the test seed were searched for in it.

| route | result |
|---|---|
| **BOOTSEL and picotool** | **nothing readable.** The SRAM was 100% zero: the bootrom clears it |
| **SWD, with the key loaded** | **the master private key and chain code are readable**, at `0x2000c1ec` and `0x2000c20c` |
| **SWD, after `Lock`** | **those same addresses are zeroed**, with the application still running |

Nothing but the key was left behind, even before pressing `Lock`.

| searched for | found |
|---|---|
| the mnemonic, 12 words | nothing |
| the BIP39 seed, 64 bytes | nothing |
| the SeedQR payload and the camera frame | nothing; that clearing went in the same day |
| the account private key | nothing; the derivation is transient |
| **the master key and chain code** | **present**, held deliberately because signing needs them |

As a control, the application's name string and the magic of the `parser.wasm` expanded into RAM were
both confirmed present in the same dump — which is how we know the dump is real.

#### Searching without knowing the value: a structural scan

The check above searches for **known values**, so it cannot find a remnant nobody thought of. So
`tools/ram_keys.py` takes every 32-byte window as if it were a private key and asks whether the public
key it derives is one we recognise. **That finds a key by its properties, without knowing its value.**

The one that matters most is **a signature's nonce, k**. If 32 bytes satisfying `k*G == R` for a
published signature's R are still in RAM, that nonce and that signature together recover the private
key. It is the most dangerous remnant there is.

| scan | result |
|---|---|
| with the key loaded, 330k windows against 83 keys | **only the master key.** No intermediate child key survives |
| after `Lock` | **nothing** |
| **the ECDSA nonce, immediately after signing** | **not present** |
| **the Schnorr nonce, immediately after signing** | **not present** |
| the child keys used to sign, 20 receive and 20 change | not present |

So being safe after the signature has gone out is measured, not assumed. What makes it hold is
`sign_input` wiping the child key it derived, and libsecp256k1 clearing the nonce internally.

**The conclusion.** What stays in RAM is the minimum signing needs, and the only route that reads it is
SWD. So two things close it: **physically unplugging the Probe** and **pressing `Lock`**. Both are
backed by measurement.

**Still undecided.** The RP2350 can disable SWD permanently through OTP, but once burned it cannot be
undone and the device can no longer be reflashed. That decision waits on how this gets distributed:
as something you build yourself, or as a finished unit.

### Defences around signing (2026-10-03)

This looked at first like one question — whether to put good randomness in aux — but reading what other
implementations do showed **three independent ones**.

| | does randomness quality matter | protects against | here |
|---|---|---|---|
| BIP340's aux | yes, for it to do anything | an extra layer against faults and side channels | **zero, deterministic** |
| verifying after signing | no | fault injection | **added** |
| blinding the context | **no** | power analysis | **added** |

**aux is zero.** BIP340 states outright that ordinary security, side channels aside, does not depend on
the quality of the RNG at signing time — and in practice Bitcoin Core
(`// Use uint256{} as aux_rnd for now.`), Trezor (NULL at every call), Blockstream Jade (anti-exfil is
ECDSA-only and does not cover taproot), SeedSigner and embit (no aux argument in the API) and BDK
(explicitly choosing `sign_schnorr_no_aux_rand`) are all deterministic. The only one found passing
randomness was Coldcard's edge branch.

Being deterministic means **the same PSBT always gives the same signature**, so the browser's
`bitcoin-signer.wasm` can reproduce the device's output bit for bit (the "verification mode" in
[positioning.md](positioning.md)). The ECDSA side is already deterministic through low-R grinding, so
the whole device agrees.

**In its place, the two defences other implementations do use went in.**

- **Verifying after signing**: check our own signature before letting it out. A glitched signature set
  next to a correct one can recover the key, which is differential fault analysis; not emitting it
  withholds the material. Core, BDK, btcd and BIP340's step 15 all do the same
- **Blinding the context**: call `secp256k1_context_randomize` before signing, so the intermediate
  values of every computation touching the key differ each time. Averaging power traces over repeated
  runs stops working. **The quality of this randomness does not matter**, so `pico_rand` is enough: a
  predictable value costs nothing in security, it just stops helping. Trezor calls it twice per
  signature, Coldcard once per session, Core at startup

The cost is 2,552 bytes of flash. **Deterministic nonces become unsafe with multisig** — BIP340 says so
— which is when aux has to be revisited. There is a comment in the code saying as much.

### B. Usability

5. **Reading speed.** 120ms to capture a frame plus 62ms to decode gives an effective 2.5fps. Running
   capture and decode on the two cores in parallel would double it
6. **Entering a passphrase**, BIP39's 25th word. Only empty is supported now
7. Legibility. Drawing just the amounts and addresses double width would help
8. Soldering the joystick. Two buttons do suffice, but going back would be easier

### C. Relationships outside this repository

8.5 **The companion web page.** An offline-capable page using the same `parser.wasm` to display a PSBT
   and move URs back and forth. It holds no keys. It should let the parser hash the device reports be
   compared against it ([positioning.md](positioning.md))
9. ~~**Whether to publish jitsu-in.**~~ Published on 2026-10-04, with v0.1.0 released. The order
   was settled deliberately: the parser first, this repository after
10. **An upstream PR for the quirc fork.** The unmerged security fixes (#158, #159) and the fixed-point
    version. Now that real camera frames can be captured, a corpus of read rates can back it up
11. WAMR's unaligned `i64.store` fix is merged upstream (#5123)

### D. Hardware

12. Run the OV7670 that is already here from a 1.8V LDO; the parts are ordered. Low priority, since the
    OV7675 works
13. A case and a real board. A breadboard cannot be carried anywhere
