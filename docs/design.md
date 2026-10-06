# Design notes

Raspberry Pi Pico 2 (RP2350 / RISC-V) / Air-gapped / OS-less / Bitcoin Core-derived signing logic
Version 0.2 | 2026-09-22

## 1. What this is for

An air-gapped hardware signer that runs its Bitcoin signing logic on the RISC-V core (Hazard3) of a
Raspberry Pi Pico 2 (RP2350) with no operating system — just a minimal bare-metal host and a
WebAssembly runtime.

The signing logic is separated from anything hardware-specific as `bitcoin-signer.wasm`, which holds
PSBT parsing and validation, sighash computation and ECDSA/Schnorr signing in one module. What stays
device-specific is the camera, the display, the buttons and the entropy source.

The Pi Zero 1.3 stays as a secondary target, since a camera and a display are easy to source for it
(§17).

## 2. Design principles

## 3. The shape of it

```
┌───────────────────────────────────────────┐
│ Raspberry Pi Pico 2 (RP2350 / Hazard3)    │
│                                           │
│  DVP Camera      SPI Display      Buttons │
│      │               │               │    │
│      └───────────────┬───────────────┘    │
│                      ▼                    │
│  ┌───────────────────────────────────┐    │
│  │ Bare-metal Host                   │    │
│  │ startup / HAL / PIO camera / SPI  │    │
│  │ QR decoder / entropy / WASM rt    │    │
│  └─────────────────┬─────────────────┘    │
│                    │ Minimal Host ABI     │
│                    ▼                      │
│  ┌───────────────────────────────────┐    │
│  │ bitcoin-signer.wasm               │    │
│  │ PSBT / BIP32 / Script / Sighash   │    │
│  │ libsecp256k1 / ECDSA / Schnorr    │    │
│  └───────────────────────────────────┘    │
└───────────────────────────────────────────┘
            ▲                       │
            │ unsigned PSBT / QR    │ signed PSBT / QR
            │                       ▼
      Untrusted wallet software (e.g. desktop wallet)
```

## 4. The trust boundary and the TCB

A WASM sandbox is not a mechanism for protecting a key from the host OS. This design removes the OS
instead, to make the TCB small on purpose. On the RP2350 the Boot ROM stays in the TCB, but there is
nothing like the Pi Zero's closed GPU firmware. Hazard3's RTL is published, so the CPU core itself is
auditable.

## 5. The hardware

- MCU: Raspberry Pi Pico 2 (RP2350, two Hazard3 RISC-V cores, 150MHz, 520KB SRAM, 4MB flash, no PSRAM)
- Camera: a DVP module (an OV2640 or similar) captured through PIO
- Display: an SPI LCD
- Input: physical buttons on GPIO
- Entropy: the RP2350's TRNG, plus entropy from user input
- Boot: secure boot and OTP, so only signed firmware starts

If WAMR plus `bitcoin-signer.wasm` plus the QR buffers will not fit in 520KB, the fallback is a board
with PSRAM, such as a Pimoroni Pico Plus 2.

## 6. The bare-metal host

The initial proof of concept uses pico-sdk (built for RISC-V) as the basis for startup, HAL and
drivers. To keep the eventual TCB small, nothing unnecessary — USB, a filesystem — is linked in.

- Boot and startup, interrupts, timers
- GPIO / physical button driver
- SPI display driver
- PIO DVP camera driver
- A framebuffer, the smallest one that works
- QR / animated QR decoder
- Entropy/RNG abstraction
- WASM runtime
- Minimal host ABI
- Memory zeroization / panic handling

## 7. The WASM runtime and the ABI

As a rule there is no dependency on WASI. `bitcoin-signer.wasm` imports no network, no filesystem, no
clock and no process functions — only the smallest ABI signing actually needs.

```
host_random(ptr, len) -> status
host_confirm(request_ptr, request_len) -> decision
host_zeroize(ptr, len) -> void
host_log(code) -> void              // debug build only

signer_load_seed(seed_ptr, seed_len) -> handle
signer_parse_psbt(psbt_ptr, psbt_len) -> handle
signer_inspect_psbt(handle, out_ptr, out_len) -> status
signer_sign_psbt(handle, seed_handle, out_ptr, out_len) -> status
signer_destroy(handle) -> void
```

Whether `signer.wasm` should drive the display and the camera directly is a decision for after the
proof of concept. For security, having the signer return a structured "here is what to confirm" and
letting the host render it with a fixed UI keeps the implementation simpler. Against that, putting the
review-screen logic inside the signer resists tampering with what is displayed, so both are compared.

## 8. bitcoin-signer.wasm

What the signing module is responsible for:

- PSBT parsing and serialization
- Checking the UTXOs, scripts and amounts against each other
- BIP32 derivation
- Legacy, SegWit and Taproot sighash computation
- ECDSA and Schnorr signing, preferring an implementation derived from libsecp256k1
- P2WPKH first, then P2TR, multisig and descriptors in stages
- Zeroizing the key, the seed and any intermediate key material
- Producing the structured review information for what is about to be signed

## 9. The signing flow

```
[Desktop Wallet: untrusted]
        │
        │ unsigned PSBT (QR)
        ▼
[Camera]
        │ raw frame
        ▼
[QR Decoder]
        │ bytes
        ▼
[PSBT Parser / Validator in WASM]
        │
        ├─ reject malformed / unsupported transaction
        │
        ▼
[Review Model]
        │
        ▼
[Trusted Local Display]   amount / destination / fee / warnings
        │
        ▼
[Physical Confirm]
        │
        ▼
[libsecp256k1 signing]
        │
        ▼
[Signed PSBT]
        │
        ▼
[Display as QR]
        │
        ▼
[Desktop Wallet: untrusted]
```

## 10. Seed and key handling

The starting position is a stateless model close to SeedSigner's: the seed is read in temporarily
from a SeedQR or similar, held only in RAM, and zeroized when signing finishes or when explicitly
discarded. No key is ever written to an SD card.

Losing power is not treated as a guarantee that RAM is cleared. What gets verified instead is explicit
zeroization on a normal exit, keeping the number of copies of a secret down, writing the clearing so
that the optimiser cannot remove it, and what happens on a crash.

## 11. Threat model

## 12. Non-goals, for the first version

- Storing a key persistently
- Wi-Fi, Bluetooth or Ethernet
- Over-the-air updates
- Installing more than one WASM application dynamically
- A general-purpose shell or filesystem
- Exchanging signing data over USB
- Depending on a secure element
- Full resistance to sophisticated physical attacks or power analysis

## 13. Implementation phases

## 14. What done means for the proof of concept

- The Pico 2 boots on its RISC-V core with no OS
- A signing review screen appears on the SPI display
- Physical buttons confirm and reject
- A QR can be read from the DVP camera through PIO
- `bitcoin-signer.wasm` parses and signs a P2WPKH PSBT
- For the same PSBT and key, the WASM test on a PC and the Pico 2 agree
- WAMR plus `bitcoin-signer.wasm` plus the QR buffers fit in 520KB of SRAM
- Secrets are explicitly zeroized once signing finishes
- No network code is present in the firmware
- The TCB's parts, its dependencies and their approximate line counts can be listed

## 15. Decisions still open

### A second parser, in Rust (undecided)

The parser is the only thing that touches bytes an attacker chooses, which is an argument for writing
it in Rust. The `prevtx_off` bug found on 2026-10-03, a pointer truncated to 32 bits, is one the type
system would have caught.

**Writing a second one is stronger than replacing the first.**

- Requiring two independent implementations to return the same `plan_t` finds bugs in either one. The
  same shape as quirc and zbar reading different frames
- The fuzzing corpus feeds both as is
- There is [a specification](../components/parts/parser/docs/abi.md), so the second one can be written from
  it independently — which also tests the specification
- A wallet written in Rust could take it as a crate, with no wasm runtime at all

What Rust would not remove is a fee computed wrongly, change identified wrongly, or BIP174 read
wrongly — and that is mostly what the 529 vectors and the fuzzing are guarding. That the sandbox
already caps the damage at "the display is wrong" is another reason not to rush a replacement.

The way to decide is to write a skeleton with `no_std`, fixed buffers and `panic=abort`, then
**measure its size and its import count**.

## 16. How the repository is laid out

**Split into the parts (`components`) and the things that use them (`apps`).** How these are
positioned is in [positioning.md](positioning.md).

```
components/            the parts; any of them can sit behind any UI
  parser/              submodule: jitsu-in (PSBT and UR parsing, its ABI, host libraries)
  qr/                  submodule: a fork of quirc, made fixed-point
  signer/              keys, BIP32, signing, addresses. Native on the device, wasm in the browser
src/                   the firmware shipped as the UF2: main.c and the pin assignment
  drivers/             panel, camera, buttons; only these need the board
  ui/                  building the screens
  runtime/             the boundary with WAMR (parser_host) and the platform layer
bringup/               wiring checks and benchmarks for the board
tests/                 host, QEMU and screen checks
tools/                 scripts for generating, measuring and drawing
docs/                  the design notes and the measurements
test-vectors/ third_party/ patches/
```

The browser viewer is maintained in [jitsu-in](https://github.com/habakan/jitsu-in/tree/main/examples/viewer).

`components/parts/signer/` is both the device's signing code and the source of `signer.wasm`.
Giving it a repository of its own is on the table, but not yet: what makes the parser usable as a part
is its specification, its host libraries and its tests, and the signer has none of those yet. Moving
the files first would only produce an empty repository. The conditions for splitting it are that
`plan_t`'s ABI is stable enough to tag, that the signer has a specification, host libraries and tests
of its own, and that someone outside this project wants to sign. Against those sits a cost measured on
2026-10-03: changing what `prevtx_off` meant touched three files at once, and the device uses this same
`core.c` natively, so a split would mean a submodule bump for every device change.

The host libraries for calling the parser from another language live in `components/parts/parser/hosts/`,
in that part's own repository, because that is what they document.

## 17. Where this is going

`bitcoin-signer.wasm` should not end up tied to one piece of hardware: the same module should run in a
browser, on WASI, and on another bare-metal host. The Pico 2 build is the reference hardware.

```
                  bitcoin-signer.wasm
                           │
          ┌────────────────┼────────────────┐
          ▼                ▼                ▼
  Pico 2 bare metal     Browser      Pi Zero bare metal
  reference target      test UI      secondary target
```

The Pi Zero can use a CSI camera and an existing SeedSigner enclosure, at the cost of leaving
VideoCore's closed firmware in the TCB. Adding a K210 would allow a comparison with Krux on identical
hardware (§19).

## 18. Prior art and references

- **Bitcoin Core / libsecp256k1** — the signing primitives and the test vectors
- **SeedSigner** — the UX and threat model of a stateless, air-gapped, QR-based signer
- **pico-sdk / Hazard3** — the RP2350's startup, PIO and RISC-V core
- **Circle** — a C++ bare-metal environment and driver reference for the Pi Zero, the secondary target
- **Circle libcamera** — using a Raspberry Pi CSI camera bare-metal
- **WAMR** — the candidate WebAssembly runtime for MCUs
- **AkiraOS** — a reference design for WASM with an embedded capability model; the OS itself is not
  adopted here

## 19. Compared with Krux

Krux is a stateless signer running on a fork of MaixPy v1 (MicroPython) on the K210 (RISC-V), so
"without Linux" is something it already achieves. Upstream MaixPy moved to MaixCAM (SG2002, Linux) at
v4, and Krux maintains its own fork of the v1 line; as of 2026-09 all eight supported devices are
K210.

| | Krux | here |
|---|---|---|
| OS | none (Kendryte SDK + MicroPython) | none (a minimal host on pico-sdk) |
| execution model | UI, QR and signing share one VM and one address space | only the signing logic is separated into WASM, across a minimal ABI |
| signing | embit (Python) plus secp256k1 | libsecp256k1, derived from Bitcoin Core |
| portability | tied to the K210 and a MaixPy fork | the same `.wasm` runs in a browser, on a PC and on other MCUs |
| CPU core | K210 (RTL not published) | Hazard3 (RTL published) |
| boot chain in the TCB | K210 Boot ROM | RP2350 Boot ROM plus secure boot |
| RAM | 8MB | 520KB |
| maturity | in real use: multisig, Taproot, SeedQR | one round on signet (2026-10-02) |

### Compared with SeedSigner, whose UX this follows

| | SeedSigner | here |
|---|---|---|
| hardware | Raspberry Pi Zero (v1.3 recommended), Waveshare 1.3 inch LCD HAT, Pi camera, microSD | Pico 2 H, 1.54 inch ST7789, OV7675, a breadboard |
| parts cost (US) | about $35 for the BOM, under $50 built ([their guide](https://seedsigner.com/seedsigner-independent-custody-guide/)); assembled units £65-£90 / €73 | about $33 |
| parts cost (sourced in Japan) | about ¥9,500: Pi Zero 2 W ¥3,190, LCD HAT ¥3,854, a compatible camera from ¥1,500, microSD. ¥13,700 with the official camera | **¥4,940**, excluding shipping; ¥7,120 with a Debug Probe |
| availability in Japan | the Pi Zero is usually back-ordered; Waveshare's LCD HAT and a compatible camera are not at Akizuki, so another shop or an import | everything from Akizuki, in one order |
| OS | Raspberry Pi OS (Linux) | none, bare metal |
| language | Python (embit) | C and WASM |
| where the key lives | RAM only, never written to the SD card | RAM only, never written to flash |
| CPU | BCM2835 (ARM11; RTL not published, and VideoCore boots first) | RP2350 Hazard3 (RISC-V, RTL published) |
| RAM | 512MB | 520KB, of which 341KB is used |
| parser isolated | no, same process | yes, in `parser.wasm` |
| features | multisig, passphrases, xpub export, Nostr and much more | signs single-signature P2WPKH and P2TR, and nothing else |
| maturity | widely used | one round on signet |

**At US prices they cost about the same; sourced in Japan this is roughly half. On features
SeedSigner is far ahead.** What differs is the size of the TCB and how far verification reaches —
here, down to the CPU. Note also that the Pi Zero **v1.3** SeedSigner recommends, the one without
radios, is nearly unobtainable in Japan, leaving the Zero 2 W as the substitute. Radios on an
air-gapped device are not what anyone wants.

What distinguishes this is not "no OS". It is that the signing logic is isolated in WASM, that the
identical binary can be shown to agree between a PC and the hardware, and that the CPU core is open.
On TCB size, a WASM runtime is an interpreter much as MicroPython is, so the line-count list in §14
should sit next to Krux (MaixPy plus embit). Whether comparable features fit in a sixteenth of Krux's
RAM is another thing worth measuring.
