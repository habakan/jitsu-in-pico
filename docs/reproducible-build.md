# Reproducible builds

The device shows the SHA-256 of the `parser.wasm` it loaded. The point of this page is that anyone
can build that same file and get that same hash. Without it, "the device runs what you can read"
is a claim rather than something you can check.

## Using it

```sh
make check-repro
```

This downloads a toolchain pinned by version and hash into `build/toolchain/`, rebuilds all five
wasm modules, and compares them against `checksums.txt`.

```
21ea6dbc…  build/parser.wasm           the parser; the device and the browser load the same file
57155143…  build/signer.wasm           keys, derivation and signing, as a wasm component
94f92d82…  build/bitcoin-signer.wasm   the signing primitives alone; what the RV32 benchmark exercises
ba25a15d…  build/address.wasm          scriptPubKey to an address
c70531df…  build/qr.wasm               the QR decoder (quirc)
```

## What is pinned

| | version | SHA-256 of the tarball |
|---|---|---|
| wasi-sdk | 34.0 | arm64-macos `9c593981…` / x86_64-linux `b761e3a0…` |
| binaryen (`wasm-opt`) | 132 | arm64-macos `98aad827…` / x86_64-linux `195ddc94…` |

`tools/toolchain.sh` fetches and verifies them. To add another platform, put its hash in the same place.

## What has been confirmed

**macOS arm64 and Linux x86_64 produce identical hashes.** Checked on a different OS, a different CPU
and a different machine (2026-10-03, and again on 2026-10-04 after adopting Lime1). CI rebuilds all
five on Linux for every commit, so the claim does not quietly rot.

## The trap: `wasm-opt` merely being on the `PATH` changes the output

The first attempt gave 15,598 bytes on macOS and 18,278 on Linux, from the same wasi-sdk version.
The source, the compiler's default features, the sysroot's `libc.a` and the linker version were all
the same.

The cause is that **clang's driver silently runs `wasm-opt` afterwards if it finds one on the `PATH`.**
macOS had binaryen from Homebrew, so it ran; the Linux machine did not, so it did not.

```
clang ... -o out.wasm     ->  wasm-ld ... && /opt/homebrew/bin/wasm-opt out.wasm -Oz -o out.wasm
```

The fix is `--no-wasm-opt` to stop the driver doing it, then calling a pinned `wasm-opt` explicitly.
That needs **`-Wl,--keep-section=target_features`**: the driver only adds it when it is going to run
wasm-opt itself, and without it `--strip-all` removes `target_features`, after which the wasm-opt we
call cannot tell whether bulk memory is enabled and refuses to validate.

```make
WASM_OPT ?= wasm-opt
	$(LLVM)/clang ... --no-wasm-opt -Wl,--keep-section=target_features -o $@ $(SRC) -lc ...
	$(WASM_OPT) $@ -Oz -o $@
```

This is the worst kind of build bug — **the artifact depends on what happens to be installed** — and
comparing hashes is what surfaced it. It is also the reason to have reproducible builds at all.

## Pinning the wasm features to Lime1

[Lime1](https://github.com/WebAssembly/tool-conventions/blob/main/Lime.md) is a named level:
**WebAssembly 1.0 plus seven phase-5 (standardised) features.** Having defined it, the authors state
it will not change, so citing it does not drift.

```
-mcpu=lime1 -Xlinker --features=mutable-globals,multivalue,sign-ext,nontrapping-fptoint,bulk-memory-opt,extended-const,call-indirect-overlong
```

**Passing it to the linker is the point, because that makes it a gate.** The day a dependency tries to
bring in SIMD or threads, the link fails. What a runtime has to support can no longer widen quietly.

Everything got smaller as a side effect.

| | before | after |
|---|---:|---:|
| `parser.wasm` | 15,603 | **15,570** |
| `signer.wasm` | 56,508 | **56,475** |
| `address.wasm` | 3,087 | **3,055** |
| `qr.wasm` | 16,754 | **16,722** |
| `bitcoin-signer.wasm` | 34,410 | **34,377** |

What validation demands narrowed too, from full `bulk-memory` to `bulk-memory-opt`.

**`-mcpu=mvp` does the opposite** and is not used: measured, it adds 2.5KB and still demands
`bulk-memory`. `-mcpu` only affects our own translation units, wasi-libc is already built with those
features, and `target_features` is the union of the inputs.

**`--no-growable-memory`** is used rather than `--max-memory=N`. The output is identical byte for byte,
but the number cannot drift out of step with `--initial-memory` — which it did, in `address.wasm`
and `qr.wasm`.

## Checking that what we ship has the right shape

```sh
make check-wasm     # needs wasm-tools (brew install wasm-tools)
```

The properties that let a user **check rather than trust** are worth checking continuously on our side
too.

| what | why |
|---|---|
| no imports | it cannot call a host function: no clock, no network |
| memory has a maximum | it cannot eat the host's memory through `memory.grow` |
| no mutable global exported | the host cannot reach in and rewrite internal state |
| no table exported | the indirect call table cannot be swapped out |
| no start function | loading it does not run anything |
| no unfamiliar custom sections | nothing extra came along |

Adding this check is what found that **`address.wasm` and `qr.wasm` had no memory maximum** — a missing
`--max-memory`. They only ever use fixed buffers, so there was never a reason to let them grow.

## Pinning the dependencies too

A pinned toolchain means nothing if the inputs move. Everything in `third_party/` is pinned to a commit
(`make check-deps`). In particular, **secp256k1 must not be taken from the tip of master**: that would
mean the library holding the keys varies by the day it was fetched.

| | |
|---|---|
| libsecp256k1 | pinned commit |
| WAMR / pico-sdk | commit matching a tag |
| quirc / QR-Code-generator / spleen | pinned commit |

Moving one means reading the diff, then changing the matching `*_REV` in the `Makefile`.

## Not done yet

- The toolchain comes from GitHub releases. Whether those tarballs are themselves reproducible is
  upstream's business
- A reproducible build of the whole device firmware, pico-sdk and WAMR included, has not been started
