# jitsu-in-pico

> **jitsu-in** — 実印, the seal that makes a signature binding in Japan; **pico** for the RP2350 it
> runs on.


[![CI](https://github.com/habakan/jitsu-in-pico/actions/workflows/ci.yml/badge.svg)](https://github.com/habakan/jitsu-in-pico/actions/workflows/ci.yml)

`jitsu-in-pico` is the Raspberry Pi Pico 2 (RP2350) reference implementation for
[jitsu-in](https://github.com/habakan/jitsu-in). This repository contains the firmware, board-specific
UI and camera code, hardware documentation, and device integration checks. The reusable modules and
cross-platform examples live in jitsu-in.

The [browser viewer](https://github.com/habakan/jitsu-in/tree/main/examples/viewer) and the
[Android sample](https://github.com/habakan/jitsu-in-android) are maintained separately.

<img src="components/parts/docs/everywhere.svg" alt="The same bytes run everywhere" width="940">

The device runs `parser.wasm` from jitsu-in. Its `Parser hash` screen shows the module's SHA-256;
`make check-repro` rebuilds the pinned artifacts and compares them with the recorded hashes.

| | |
|---|---|
| Modules, specifications, and cross-platform examples | [jitsu-in](https://github.com/habakan/jitsu-in) |
| Pico 2 implementation and use cases | [docs/positioning.md](docs/positioning.md) (Japanese) |
| Terms and headings used in the docs | [docs/terms.md](docs/terms.md) (Japanese) |
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

Verified in this repository on RP2350 hardware, macOS hosts, and QEMU.

Not supported: multisig, passphrases, PSBT v2. Single-signature P2WPKH and P2TR only.

## Try it

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

The device's `Parser hash` screen should match the parser hash produced by this build
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

`apps/` contains the Raspberry Pi Pico 2 firmware. Host and QEMU integration checks live under `tests/`.

| | | TCB |
|---|---|---|
| `components/parts/` | Pinned [jitsu-in](https://github.com/habakan/jitsu-in) submodule: reusable parser and signer modules. Their specifications and host examples are maintained in jitsu-in | **outside** |
| `components/qr/` | QR decoder (submodule: [quirc](https://github.com/habakan/quirc), `mcu` branch, made fixed-point for CPUs without an FPU) | outside |
| `apps/device/rp2350/` | The firmware: display (ST7789), buttons, camera (PIO + DMA) | inside |
| `apps/device/ui/` | Builds the 240x240 screens, independent of where they are shown | inside |
| `apps/device/runtime/` | The call boundary into `parser.wasm` (every offset and length is range-checked) and the WAMR platform layer | inside |
| `tests/host/` | Integration hosts for macOS and QEMU | - |
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
