# Feasibility and performance measurements

This page combines the RP2350 memory and performance studies for the current device, QR decoding,
and earlier experiments with running the complete signer in WebAssembly. The signer WASM results are
historical: the current firmware runs the signing core natively and uses WASM for transaction parsing.

## Current device

The current application uses about 341KB of the RP2350's 520KB SRAM. QR decoding and PSBT processing
share the heap: quirc is released after scanning, before the PSBT buffers are allocated.

| Item | Size | Basis |
|---|---:|---|
| WAMR pool, including parser.wasm memory and UR assembly | 160KB | 152,792 bytes maximum measured use |
| parser.wasm copy in RAM | 16KB | Rewritten by the interpreter while loading |
| Shared heap | 92KB | 93,696 bytes peak measured with `mallinfo` |
| Stack | 32KB reserved | 23KB measured; the default 2KB is insufficient |
| UI, qrcodegen, core, secp256k1, pico-sdk and other use | 42KB | Remainder of the application |
| **Total** | **about 341KB / 520KB** | |

The shared heap holds about 90KB for quirc while scanning, then about 66KB for PSBT buffers. Without
sharing, the peak would be 159,232 bytes. About 180KB remains; double-buffering the camera would use
another 77KB.

## QR decoding on the RP2350

These measurements cover quirc and qrcodegen on QEMU RV32 (`-icount shift=0`) and Pico 2 H hardware
at 150MHz. Synthetic camera-like images were stored in Flash and run with
`make run ELF=build/rp2350/qr_bench.elf`.

### Synthetic image generation

`tools/generate/gen_qr_frames.py` puts one UR QR code (`UR:CRYPTO-PSBT/12-30/...`, uppercase alphanumeric mode,
ECC L) into a 320×240 grayscale image. It applies a 5° rotation, Gaussian blur, noise σ=4, 25% side
and 15% top/bottom illumination variation, and contrast from 40 to 210.

| UR fragment | QR version | Modules |
|---|---:|---:|
| 50 bytes | v5 | 37 |
| 100 bytes | v8 | 49 |
| 200 bytes | v11 | 61 |

### Read success by QR size and blur

| QR | Span (px) | px/module | Blur σ | quirc | zxing-cpp |
|---|---|---|---|---|---|
| v5 | 220 / 190 / 160 | 5.9 / 5.1 / 4.3 | 0.5, 1.0 | all pass | all pass |
| v8 | 220 | 4.5 | 0.5 / 1.0 | pass / fail | pass / pass |
| v8 | 190 | 3.9 | 0.5 / 1.0 | pass / pass | pass / pass |
| v8 | 160 | 3.3 | 0.5 / 1.0 | pass / fail | pass / pass |
| v11 | 220 | 3.6 | 0.5 / 1.0 | pass / fail | pass / pass |
| v11 | 190 | 3.1 | 0.5 / 1.0 | fail / fail | pass / pass |
| v11 | 160 | 2.6 | 0.5 / 1.0 | fail / fail | pass / fail |

The v8 failures are not monotonic: one occurs at the largest 220px span. These results use generated
images and do not predict camera performance on their own.

### Instructions per frame (RV32)

| Operation | v5 | v8 | v11 |
|---|---:|---:|---:|
| Decode (upstream quirc, double) | 370M | 622M | - |
| Decode (upstream quirc, float) | 231M | 388M | - |
| Decode (patched quirc, float) | 22.5–23.2M | 35.8–36.6M | 40.7M |
| Frame with no QR found | 2.9–3.9M | | |
| Generate QR (qrcodegen) | 4.5M | 7.8M | 12.2M |
| Draw 240×240 RGB565 one row at a time | 1.5M | 1.6M | 1.5M |

Upstream quirc calls `fitness_all` 81 times from `jiggle_perspective`. A frame calls `perspective_map`
about 430,000 times at roughly 1,300 instructions each, accounting for nearly all decode time. The
fixed-point patch converts coefficients once per `fitness_all`; `fitness_cell` then uses 32×32→64-bit
multiplication and 32-bit division, falling back to floating point if coefficients are out of range.

The fixed-point `mcu` branch of [habakan/quirc](https://github.com/habakan/quirc) brought synthetic
v5 and v8 decoding to 23M and 36M instructions. After divide-by-zero and overflow fixes, they took
26.8M and 43M. Its 800-image comparison reported no incorrect reads; 789 results matched upstream.
Building with `-mcpu=hazard3-rp2350 -O2` emits the Hazard3-specific Xh3bextm instruction, which QEMU
does not support. The QEMU build used
`-march=rv32imac_zicsr_zifencei_zba_zbb_zbs_zbkb_zcb_zcmp`.

### Pico 2 H measurements (150MHz, 2026-09-25)

Eighteen generated frames were stored in Flash and run on the device.

| Operation | v5 | v8 | v11 | Effective CPI |
|---|---:|---:|---:|---:|
| Decode (patched quirc) | 0.278–0.285s | 0.447–0.453s | 0.494s | 1.57 |
| Frame with no QR found | 0.029–0.038s | | | 1.48 |
| Generate QR (qrcodegen) | 0.034s | 0.058s | 0.091s | 1.12 |
| Draw 240×240 RGB565 one row at a time | 0.021s | 0.022s | 0.020s | 2.07 |

Read successes and failures matched QEMU, including two v8 failures and three v11 failures. quirc's
effective CPI was 1.57, compared with 7.5 for AOT signing code. The frame buffer is in RAM and the hot
functions are small. Drawing's CPI of 2.07 is dominated by writes to the row buffer. Measured
throughput was 3.6 fps for v5 and 2.2 fps for v8; frames with no readable QR returned in 30ms.

### OV7675 camera measurements (2026-10-02)

An OV7675 (Arducam B0070) was connected to the Pico 2 H. `camera_test` read actual QR images and
reassembled a v5 QR (about a 50-byte UR fragment).

| Operation | Measurement |
|---|---:|
| Capture | 120ms (VGA frame plus VSYNC wait, about 8fps) |
| Decode, no QR | 62ms |
| Decode, QR found | 300ms (grid extraction and error correction) |
| Effective rate | about 2.5fps |

The OV7675's vertical downscaling (`SCALING_DCWCTR`) did not work. The sensor outputs VGA YUV422;
PIO keeps the first Y from each `Y U Y V` pair and skips every other row to produce 320×240. This
does not depend on sensor scaling (`src/drivers/camera.pio`). The same frame may decode or return
`ECC failure`; camera shake and exposure matter, and adequate lighting improves the success rate.
At startup, `camera_test` reports GP2–GP17 states and PCLK, HREF, and VSYNC edge counts to help find
wiring mistakes.

The current design keeps QR decoding native and in the TCB. Putting it in WASM would make it about
80 times slower, over ten seconds per frame even with the fixed-point patch. Synthetic QVGA frames
decode through v8, but real camera reliability at v8 has not been established. The current quirc fork
also carries unmerged security fixes (#158, #159), UBSan and fuzzing work, and a fix for
NaN-to-integer conversion in upstream `perspective_map()`; see its `mcu/README.md`.

QR generation is not the bottleneck: drawing a v11 QR on the 240×240 LCD took under 14 million
instructions. For animated output, 100-byte fragments keep frames at v8. Larger PSBTs, especially
those carrying full previous transactions, can require many frames.

### QR reproduction

```sh
make deps
make check-qemu-qr
make check-qemu-qr QUIRC=third_party/quirc/lib QUIRC_DEFS=  # upstream quirc (double)
make check-qr-mac
make run ELF=build/rp2350/qr_bench.elf                    # hardware, via SWD and UART
```

## Earlier experiments: signer and key derivation in WASM

The following measurements use the former `bitcoin-signer.wasm`, removed from jitsu-in v0.1.0. They
are retained as historical evidence for the performance and memory trade-offs. They do not describe
the current firmware, whose signing core is native.

### QEMU instruction counts (RV32)

| Operation | RV32 native | WAMR classic | WAMR fast | Classic + host SHA-512 |
|---|---:|---:|---:|---:|
| PBKDF2 (BIP39 seed) | 66.4M | 1,693M | 1,030M | 300M |
| BIP32 `m/84'/0'/0'/0` | 2.2M | 161M | 95M | 154M |
| BIP32 `m/84'/0'/0'/0/0` | 3.2M | 239M | 142M | 231M |
| ECDSA signing | 1.0M | 82M | 49M | 82M |

For the standalone signer study, classic interpreter configurations measured 82.2M instructions for
ECDSA and 153.5M for Schnorr with a 2KB `ecmult_gen` table. A 22KB table reduced those counts to
63.5M and 116.0M at a 20KB RAM cost. Fast interpreter measured 48.5M and 90.7M.

PBKDF2's 64-bit SHA-512 arithmetic made the interpreter especially slow. Importing only the
SHA-512 compression function reduced PBKDF2 from 1,693M to 300M instructions; BIP32 improved little
because public-key calculation dominated its cost. At an estimated CPI of 1–1.5, classic plus host
SHA-512 put seed generation at 2–3 seconds and signing one input at 2.1–3.1 seconds. Caching the
account key would reduce repeated derivation work.

### Pico 2 H measurements (150MHz, 2026-09-25)

| Operation | Classic interpreter | Classic + host SHA-512 | AOT XIP | AOT expanded into RAM |
|---|---:|---:|---:|---:|
| ECDSA signature | 0.696s | 0.696s | 0.319s | **0.041s** |
| Schnorr signature | 1.297s | 1.298s | 0.622s | **0.078s** |
| BIP39 seed (PBKDF2, 2048 rounds) | 14.81s | 3.053s | 0.979s | **0.832s** |
| BIP32 `m/84'/0'/0'/0/0` | 2.029s | 1.960s | 0.937s | **0.122s** |
| Runtime initialization | 16.9ms | 16.9ms | 2.5ms | 1.4ms |
| Flash | 125.4KB | 124.2KB | 228.6KB | 228.1KB |
| RAM | 135.3KB | 133.0KB | **68.9KB** | 347.5KB |
| Maximum WAMR pool | 36.7KB | 36.0KB | 34.9KB | 277.3KB |

All four configurations produced signatures, seeds, and public keys matching QEMU and Mac native
results. The interpreter's measured CPI was 1.27 (1.35 for PBKDF2). AOT expanded into RAM measured
CPI 1.08 and followed QEMU instruction counts. AOT XIP measured CPI 7.5 for secp256k1 paths because
the 150KB generated code exceeded the 16KB XIP cache; PBKDF2 stayed near CPI 1.1 because its SHA-512
compression loop fit in cache. Thus QEMU instruction counts alone did not predict XIP performance.

The AOT QEMU instruction counts were:

| Operation | Native | Classic interpreter | AOT, not XIP | AOT XIP (default) | AOT XIP, more runtime calls |
|---|---:|---:|---:|---:|---:|
| ECDSA signature | 1.05M | 82.2M | 5.68M | 6.42M | 12.2M |
| Schnorr signature | 1.89M | 153M | 10.9M | 12.2M | 23.3M |
| PBKDF2 (BIP39) | 66.4M | 1,693M | 119M | 136M | 177M |
| BIP32 `m/84'/0'/0'/0/0` | 3.19M | 239M | 16.9M | 19.0M | 35.9M |
| Maximum WAMR pool | - | 36.7KB | 272KB | 35.1KB | 35.1KB |

Setting `--bounds-checks=0` reduced ECDSA to 4.33M, PBKDF2 to 97.0M, and BIP32 to 12.9M instructions,
but is not suitable for an MCU security boundary. The default `WAMRC_FLAGS` used `--xip` and limited
runtime calls to operations such as i64 division and remainder; wamrc's broader RISC-V defaults made
the workloads about twice as slow.

RAM-expanded AOT reduced the complete 1-input, 2-output P2WPKH round to about 0.2 seconds, while AOT
XIP took about 1.5 seconds. RAM-expanded code used 347.5KB overall, including a 277.3KB WAMR pool.
That left too little headroom for QR capture buffers in the former whole-signer design. This memory
trade-off, the TCB cost of the AOT loader and build-time LLVM, and the current parser-only WASM
boundary informed the current architecture.

### Runtime and memory constraints found

- The classic interpreter rewrites bytecode while loading, so the WASM image needs a RAM copy.
- WASM data segments, including libsecp256k1 constant tables, occupy linear memory. The study used
  `ECMULT_GEN_KB=2` (`COMB_BLOCKS=2`, `COMB_TEETH=5`) instead of the 86KB default.
- WAMR's shrunk-memory mode can avoid allocating whole 64KB pages when `memory.grow` is unused and
  `__heap_base` / `__data_end` are exported.
- On RV32, libsecp256k1's 10x26 field arithmetic uses many 64-bit operations, which WAMR handles in
  software.
- WAMR classic interpreter 2.4.3 assumed 4-byte alignment for `i64.store`; Hazard3 faults on some
  unaligned stores while QEMU does not. The fix was merged upstream in
  [WAMR PR #5123](https://github.com/wasm-micro-runtime/wasm-micro-runtime/pull/5123) on 2026-09-30.
  `make deps` applies the compatibility patch while version 2.4.3 is used.
- The RP2350's default 2KB stack was too small; the firmware used a 16KB stack.
- WAMR links libm functions such as `ceil` and `sqrt` even when the signer itself does not use floating
  point, adding them to the trusted computing base.
- RISC-V AOT code written to RAM needs `fence.i`; the firmware added this through `os_icache_flush`.
  The measured QEMU AOT target used `generic-rv32` with `m,a,c,zba,zbb,zbs`, not Hazard3's
  `zbkb`, `zcb`, or `zcmp` extensions.

### Historical signer reproduction

These commands target the removed `bitcoin-signer.wasm`. Check out commit `f2f61c6` before running
them; they do not run against the current tree.

```sh
make check-host
make check-qemu-native
make check-qemu POOL_KB=64
make check-qemu FAST=1 POOL_KB=256
uv run tools/ref_kdf.py
make build/wamrc/wamrc
```

For the former AOT hardware build, the study used `AOT=1`, `RP2350_POOL_KB=320`, and
`--target=riscv32 --target-abi=ilp32 --cpu=generic-rv32 --cpu-features=+m,+a,+c,+zba,+zbb,+zbs
--bounds-checks=1`. Rebuilds required deleting generated `bitcoin-signer.wasm`, AOT, and header files
when switching configurations.
