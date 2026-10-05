# jitsu-in-pico

> **jitsu-in** — 実印, the seal that makes a signature binding in Japan; **pico** for the RP2350 it
> runs on.


[![CI](https://github.com/habakan/jitsu-in-pico/actions/workflows/ci.yml/badge.svg)](https://github.com/habakan/jitsu-in-pico/actions/workflows/ci.yml)

`jitsu-in-pico` is the RP2350 reference implementation for [jitsu-in](https://github.com/habakan/jitsu-in),
a set of reusable Bitcoin signing modules. This repository contains the Raspberry Pi Pico 2 firmware,
a browser PSBT viewer, and the hardware and verification work around them. See jitsu-in for the portable
modules, host APIs, and their specifications.

The firmware and viewer use the same parser module from jitsu-in. It reads attacker-controlled PSBT and
UR data in a WebAssembly module with **zero imports**; the key-handling code stays separate.

<img src="components/parts/docs/everywhere.svg" alt="The same bytes run everywhere" width="940">

The same **15,570 bytes** run on a microcontroller with no OS (RP2350) and in Safari on an iPhone.
`parser.wasm` is byte-for-byte identical in both, the device shows its SHA-256 on screen, and
`make check-repro` rebuilds it from source to the same hash — so you can check for yourself that the
parser inside the device is the one in this repository.

| | |
|---|---|
| The idea, with diagrams | [docs/everywhere.md](docs/everywhere.md) (Japanese) |
| Project overview and use cases | [docs/positioning.md](docs/positioning.md) (Japanese) |
| Terms and headings used in the docs | [docs/terms.md](docs/terms.md) (Japanese) |
| Reusable signing modules and module conventions | [jitsu-in](https://github.com/habakan/jitsu-in) |
| Parser ABI and host examples | [jitsu-in parser documentation](https://github.com/habakan/jitsu-in/tree/main/parser) |

A Japanese version is at [README.ja.md](README.ja.md). The design notes and measurements under
`docs/` are still Japanese only.

## Disclaimer

**This software has not been reviewed by a third party. Do not put real funds through it.**

It completes a signing round on signet, and every claim on this page is backed by a measurement in
this repository — but that is not the same as having been attacked by someone other than its author.
What mainnet would require is listed in [docs/architecture-b.md](docs/architecture-b.md) §15; what
the signer accepts, refuses and deliberately does not do is in
[docs/limitations.md](docs/limitations.md).

Found a security problem? [SECURITY.md](SECURITY.md) — **not** a public issue.

## The Pico 2 implementation

Read a seed from a SeedQR with the camera, receive a PSBT as an animated QR (UR), show it for review,
sign, and hand the signed PSBT back as a QR. The PC never holds a key or a recovery phrase — a
[signet transaction](https://mempool.space/signet/tx/de849e8c01a39fcf2aa84aaeeccb2ac8aea128086b2f4252539bcab90a0a432f)
went through that way ([how](docs/signet.md)).

| Stage | Measured | |
|---|---|---|
| BIP39 seed (PBKDF2, 2048 rounds) | 0.47 s | native |
| PSBT parsing | 25 ms | **parser.wasm** (WAMR classic interpreter) |
| Checks and building the review screens | 147 ms | native |
| Signing (2 inputs, ECDSA + Schnorr) | 126 ms | native, cross-checked with embit |
| Animated QR output | 95 ms per part | reassembled byte-identical by `@ngraveio/bc-ur` |
| RAM | ~341KB of 520KB | quirc and the PSBT buffers share the heap |

Actually run and verified on: bare-metal MCU (RP2350), browsers, Android 10, iOS, Linux / macOS, Node.

Not supported: multisig, passphrases, PSBT v2. Single-signature P2WPKH and P2TR only.

## Try it

### Without any hardware

```sh
git submodule update --init
make deps          # clone third_party and apply patches
make viewer        # builds build/viewer.html and opens it
```

One 189KB HTML file holding three WASM modules (parsing, QR decoding, addresses). It works offline,
and the camera works straight from `file://` on desktop (Android needs localhost or HTTPS).

### Parser hosts

The Kotlin and Swift host examples live in [jitsu-in](https://github.com/habakan/jitsu-in/tree/main/parser/hosts).
They use [Chicory](https://github.com/dylibso/chicory) and [WasmKit](https://github.com/swiftwasm/WasmKit),
respectively. Neither needs JNI or a native build step. C, JavaScript, Kotlin, Swift, and this device
produce the same plan for the same PSBT.

### On hardware

Parts and wiring: [docs/hardware.md](docs/hardware.md), [docs/breadboard.md](docs/breadboard.md) (Japanese).

```sh
make deps-openocd                 # once, for SWD flashing (the Raspberry Pi fork of OpenOCD)
make run SECONDS=180              # flash, start listening, reset
make run TESTNET=1 SECONDS=180    # signet
```

Only a build with `TEST_SEED=1` can select the BIP39 test vector seed (the default is 0). Never put funds on it.
What to do differently for real use is in [docs/architecture-b.md](docs/architecture-b.md) (Japanese).

## Check it yourself

```sh
make check-repro   # rebuild parser.wasm with a pinned toolchain and compare against the record
```

The device's `Parser hash` screen, the footer of the viewer page, and this output should all agree.
When they do, the parser running inside the device is the one built from public sources
([docs/reproducible-build.md](docs/reproducible-build.md)).

```sh
make check-core        # signing core vectors (71 checks)
make check-xpub        # account xpub and descriptor (official BIP84 vectors)
make check-psbt        # a full PSBT round, UR round-trip, signatures verified with embit
make check-ui          # screen construction
make check-seedqr      # SeedQR reading, with ASan
make check-host        # macOS: native / WAMR classic / fast
make check-qemu-psbt   # the same round on RV32, output compared with the host
make check-qemu-qr     # quirc instruction counts
make check-qr-mac      # quirc vs zxing-cpp on the same images
make check-camera-sim  # camera.pio against a Python simulator
make -C components/parts/parser test        # parser vectors (529 checks)
make -C components/parts/parser check-fuzz  # fuzzing the PSBT and UR parsers
```

Expected values come from independent implementations: embit, hashlib, `@ngraveio/bc-ur`, zxing-cpp,
Bitcoin Core. New tests are checked with mutation testing before they are trusted.

## What this repository contains

The firmware targets Raspberry Pi Pico 2 (RP2350). The viewer and host checks make it possible to exercise
and compare the same parser outside the device.

| | | TCB |
|---|---|---|
| `components/parts/` | Pinned [jitsu-in](https://github.com/habakan/jitsu-in) submodule: reusable parser and signer modules. Their specifications and host examples are maintained in jitsu-in | **outside** |
| `components/qr/` | QR decoder (submodule: [quirc](https://github.com/habakan/quirc), `mcu` branch, made fixed-point for CPUs without an FPU) | outside |
| `apps/device/rp2350/` | The firmware: display (ST7789), buttons, camera (PIO + DMA) | inside |
| `apps/device/ui/` | Builds the 240x240 screens, independent of where they are shown | inside |
| `apps/device/runtime/` | The call boundary into `parser.wasm` (every offset and length is range-checked) and the WAMR platform layer | inside |
| `apps/viewer/` | The single-file HTML viewer, running the same wasm as the device | - |
| `apps/host/` | Test hosts for macOS and QEMU | - |
| `tools/` `docs/` | Build and verification tools, wiring diagrams, measurements, and design notes for this implementation |  - |

Dependencies (`third_party/`, gitignored) are cloned by `make deps`: libsecp256k1, WAMR 2.4.3,
pico-sdk 2.3.1, QR-Code-generator, the spleen font, and a RISC-V toolchain.

## The record

Measurements and mistakes are kept as they happened (currently in Japanese): AOT with XIP running
7× slower on real hardware, WAMR's unaligned `i64.store`, `wasm-opt` changing the artifact by 2.7KB
merely by being on `PATH`, quirc failing to read a display from a distance.

[architecture](docs/architecture.md) ·
[design](docs/design.md) ·
[isolating the parser](docs/architecture-b.md) ·
[reproducible build](docs/reproducible-build.md) ·
[signet workflow](docs/signet.md) ·
[fit on RP2350](docs/feasibility.md) ·
[BIP39/BIP32 speed](docs/kdf-feasibility.md) ·
[AOT](docs/aot-feasibility.md) ·
[QR](docs/qr-feasibility.md) ·
[hardware](docs/hardware.md)

## Upstream

- **WAMR**: fixed the classic interpreter assuming 4-byte alignment for `i64.store`
  ([PR #5123](https://github.com/wasm-micro-runtime/wasm-micro-runtime/pull/5123), merged 2026-09-30).
  It bites on CPUs that disallow unaligned access, and does not reproduce under QEMU
- **quirc**: the `mcu` fork carries the fixed-point work, unmerged security fixes, UBSan and fuzzing

## Contributing

Patches welcome: [CONTRIBUTING.md](CONTRIBUTING.md). Security problems go to
[SECURITY.md](SECURITY.md), never to a public issue.

## License

MIT ([LICENSE](LICENSE)). Third-party code is listed in [NOTICE](NOTICE).
