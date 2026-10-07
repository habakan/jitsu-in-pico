# Reproducible builds

The device shows the SHA-256 of the `parser.wasm` it loaded. The point of this page is that anyone
can build that same file and get that same hash. Without it, "the device runs what you can read"
is a claim rather than something you can check.

## Using it

```sh
make check-repro
```

This downloads a toolchain pinned by version and hash into `build/toolchain/`, rebuilds the two
WASM artifacts used by this repository, and compares them against `checksums.txt`.

```
7c89bf15…  build/parser.wasm   the parser loaded by the device
96b78cd6…  build/signer.wasm   keys, derivation and signing, as a wasm component
```

## What is pinned

| | version | SHA-256 of the tarball |
|---|---|---|
| wasi-sdk | 34.0 | arm64-macos `9c593981…` / x86_64-linux `b761e3a0…` |
| binaryen (`wasm-opt`) | 132 | arm64-macos `98aad827…` / x86_64-linux `195ddc94…` |

`tools/build/toolchain.sh` fetches and verifies them. To add another platform, put its hash in the same place.

## What has been confirmed

**macOS arm64 and Linux x86_64 produce identical hashes.** Checked on a different OS, a different CPU
and a different machine (2026-10-03, and again on 2026-10-04 after adopting Lime1). CI rebuilds these
artifacts on Linux for every commit.

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
| `bitcoin-signer.wasm` | 34,410 | **34,377** |

What validation demands narrowed too, from full `bulk-memory` to `bulk-memory-opt`.

**`-mcpu=mvp` does the opposite** and is not used: measured, it adds 2.5KB and still demands
`bulk-memory`. `-mcpu` only affects our own translation units, wasi-libc is already built with those
features, and `target_features` is the union of the inputs.

**`--no-growable-memory`** is used rather than `--max-memory=N`. The output is identical byte for byte,
and the limit cannot drift out of step with `--initial-memory`.

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

The browser example and its auxiliary modules are maintained in
[jitsu-in](https://github.com/habakan/jitsu-in/tree/main/examples/viewer).

## Pinning the dependencies too

A pinned toolchain means nothing if the inputs move. Everything in `third_party/` is pinned to a commit
(`make check-deps`). In particular, **secp256k1 must not be taken from the tip of master**: that would
mean the library holding the keys varies by the day it was fetched.

| | |
|---|---|
| libsecp256k1 | pinned commit |
| WAMR | pinned upstream commit, including [PR #5123](https://github.com/wasm-micro-runtime/wasm-micro-runtime/pull/5123) |
| pico-sdk | commit matching a tag |
| quirc / QR-Code-generator / spleen | pinned commit |

Moving one means reading the diff, then changing the matching `*_REV` in the `Makefile`.

## The firmware

The released UF2s are built the same way, with the RISC-V toolchain pinned by hash in `make deps`:

```sh
make deps && ./tools/build/toolchain.sh && make check-repro
make build/rp2350/app.elf               # build/rp2350/app.uf2, mainnet
rm -rf build/rp2350 && make build/rp2350/app.elf TESTNET=1   # signet
```

macOS arm64 and Linux x86_64 give the same UF2, from different directories (2026-10-06). The one thing
that differed was the build date pico-sdk writes into the binary info, so the firmware turns it off
(`PICO_NO_BI_PROGRAM_BUILD_DATE=1`).

## Not done yet

- The toolchain comes from GitHub releases. Whether those tarballs are themselves reproducible is
  upstream's business
