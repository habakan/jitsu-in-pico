# jitsu-in-pico

<sup>[日本語](docs/ja/README.md)</sup>

> **jitsu-in** — 実印, the seal that makes a signature binding in Japan; **pico** for the RP2350 it
> runs on.


[![CI](https://github.com/habakan/jitsu-in-pico/actions/workflows/ci.yml/badge.svg)](https://github.com/habakan/jitsu-in-pico/actions/workflows/ci.yml)

`jitsu-in-pico` is the Raspberry Pi Pico 2 (RP2350) reference implementation for
[jitsu-in](https://github.com/habakan/jitsu-in). This repository contains the firmware, board-specific
UI and camera code, hardware documentation, and device integration checks. The reusable modules and
cross-platform examples live in jitsu-in.

The [browser viewer](https://github.com/habakan/jitsu-in/tree/main/examples/viewer) is maintained in jitsu-in.

The device runs `parser.wasm` from jitsu-in. Its `Parser hash` screen shows the module's SHA-256;
`make check-repro` rebuilds the pinned artifacts and compares them with the recorded hashes.

| | |
|---|---|
| Modules, specifications, and cross-platform examples | [jitsu-in](https://github.com/habakan/jitsu-in) |
| Pico 2 hardware and wiring | [docs/hardware.md](docs/hardware.md), [docs/breadboard.md](docs/breadboard.md) |
| Parser ABI and host examples | [jitsu-in parser documentation](https://github.com/habakan/jitsu-in/tree/main/parser) |

Most design notes and measurements under `docs/` are in Japanese. English pages are linked from the
Japanese index at [docs/ja/README.md](docs/ja/README.md).

## Disclaimer

**This software has not been reviewed by a third party. Do not put real funds through it.**

It completes a signing round on signet. The measurements on this page come from this repository;
they do not show that anyone besides the author has tried to break it. Mainnet readiness, supported
transactions, and known limitations are listed in [docs/limitations.md](docs/limitations.md).

Found a security problem? [SECURITY.md](SECURITY.md) — **not** a public issue.

## The Pico 2 implementation

Read a seed from a SeedQR with the camera, receive a PSBT as an animated QR (UR), show it for review,
sign, and hand the signed PSBT back as a QR. The PC never holds a key or a recovery phrase — a
[signet transaction](https://mempool.space/signet/tx/de849e8c01a39fcf2aa84aaeeccb2ac8aea128086b2f4252539bcab90a0a432f)
went through that way ([verification procedure](docs/verification.md#signet-transaction)).

| Stage | Measured | |
|---|---|---|
| BIP39 seed (PBKDF2, 2048 rounds) | 0.47 s | native |
| PSBT parsing | 25 ms | **parser.wasm** (WAMR classic interpreter) |
| Checks and building the review screens | 147 ms | native |
| Signing (2 inputs, ECDSA + Schnorr) | 126 ms | native, cross-checked with embit |
| Animated QR output | 95 ms per part | reassembled byte-identical by `@ngraveio/bc-ur` |
| RAM | ~341KB of 520KB | quirc and the PSBT buffers share the heap |

Verified in this repository on RP2350 hardware, macOS hosts, and QEMU.

The firmware currently supports single-signature P2WPKH and P2TR only. BIP39 passphrase input and
multisig are planned, but are not available yet. The multisig script formats have not been decided.
PSBT v2 is not supported.

## Try it

### On hardware

Prebuilt firmware is on the [Releases](https://github.com/habakan/jitsu-in-pico/releases) page:
`jitsu-in-pico.uf2` for mainnet, `jitsu-in-pico-signet.uf2` for signet. Hold BOOTSEL while connecting
the Pico 2 over USB and copy the UF2 onto the drive. Building it yourself gives the same bytes
([docs/reproducible-build.md](docs/reproducible-build.md)).

Parts and wiring: [hardware](docs/hardware.md), [breadboard wiring](docs/breadboard.md). Japanese: [breadboard wiring](docs/ja/breadboard.md).

```sh
make deps-openocd                 # once, for SWD flashing (the Raspberry Pi fork of OpenOCD)
make run SECONDS=180              # flash, start listening, reset
make run TESTNET=1 SECONDS=180    # signet
```

Only a build with `TEST_SEED=1` can select the BIP39 test vector seed (the default is 0). Never put funds on it.
Known limitations and operational constraints are in [docs/limitations.md](docs/limitations.md).

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

`src/` is the firmware that ships as the UF2; `bringup/` holds the other programs for the board.
Host and QEMU checks live under `tests/`.

| | | TCB |
|---|---|---|
| `components/parts/` | Pinned [jitsu-in](https://github.com/habakan/jitsu-in) submodule: reusable parser and signer modules. Their specifications and host examples are maintained in jitsu-in | **outside** |
| `components/qr/` | QR decoder ([quirc](https://github.com/habakan/quirc) submodule) | outside |
| `src/` | The firmware's entry point (`main.c`) and the pin assignment (`board_pins.h`) | inside |
| `src/drivers/` | Display (ST7789), buttons, camera (PIO + DMA). Runs only on the board | inside |
| `src/ui/` | Builds the 240x240 screens, independent of where they are shown | inside |
| `src/runtime/` | The call boundary into `parser.wasm` (every offset and length is range-checked) and the WAMR platform layer | inside |
| `bringup/` | Wiring checks and benchmarks: buttons, camera, PIO loopback, a PSBT round without UI | - |
| `tests/` | Host (`host/`), QEMU (`qemu/`) and screen (`ui/`) checks | - |
| `tools/` `docs/` | Build and verification tools, wiring diagrams, measurements, and implementation documentation |  - |

Dependencies (`third_party/`, gitignored) are cloned by `make deps`: libsecp256k1, WAMR 2.4.3,
pico-sdk 2.3.1, QR-Code-generator, the spleen font, and a RISC-V toolchain.

## Documentation

| If you want to... | Read |
|---|---|
| Understand how the device works | [System architecture](docs/architecture.md) |
| Review the parser boundary and signing path | [Signing architecture](docs/signing-architecture.md) |
| Check supported transactions and known risks | [Limitations](docs/limitations.md) |
| Rebuild and verify the published artifacts | [Reproducible build](docs/reproducible-build.md) |
| Repeat the signet transaction procedure | [Verification](docs/verification.md) |
| See performance and feasibility measurements | [Feasibility and performance](docs/feasibility.md) |
| Identify parts and wire the device | [Hardware](docs/hardware.md), [breadboard](docs/breadboard.md) |

## Contributing

Patches welcome: [CONTRIBUTING.md](CONTRIBUTING.md). Security problems go to
[SECURITY.md](SECURITY.md), never to a public issue.

## License

MIT ([LICENSE](LICENSE)). Third-party code is listed in [NOTICE](NOTICE).
