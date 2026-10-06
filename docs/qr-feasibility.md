# Feasibility: QR decoding on the RP2350 (Hazard3)

Updated 2026-10-02 with measurements from the OV7675 camera. The earlier synthetic-frame tests ran
quirc and qrcodegen on QEMU RV32 (`-icount shift=0`) and Pico 2 H hardware to measure instruction
count, time, and memory. They used generated camera-like frames stored in Flash
(`make run ELF=build/rp2350/qr_bench.elf`).

## Findings

- **Upstream quirc is too slow on Hazard3.** It has no FPU, so the perspective-fit code uses software
  floating point. A v5 frame takes 370 million instructions (2.5–3.7 seconds at 150MHz).
- **Fixed-point fitness evaluation brings synthetic-frame decoding into range.** The
  [habakan/quirc](https://github.com/habakan/quirc) `mcu` branch uses `QUIRC_FIXED_POINT_FITNESS`.
  Before divide-by-zero and overflow fixes, v5 took 23 million instructions and v8 took 36 million;
  after the fixes, they took 26.8 million and 43 million. At an estimated CPI of 1–1.5, that is 3–6
  fps. The fork produced no incorrect reads in its 800-image comparison; 789 results matched
  upstream.
- **Synthetic QVGA frames decode reliably through v8** (100-byte UR fragments). v11 becomes sensitive
  to image conditions. zxing-cpp reads nearly every test case, so these failures are quirc's limit,
  not the image data's.
- **RAM fits.** The signer uses about 97KB and the QR path about 120KB, roughly 220KB of 520KB.
- **QR output is not the bottleneck.** Generating and drawing a v11 QR on the 240×240 LCD takes under
  14 million instructions.

## Synthetic frames

`tools/gen_qr_frames.py` creates a frame containing one UR QR code
(`UR:CRYPTO-PSBT/12-30/...`, uppercase alphanumeric mode, ECC L) and composites it into a 320×240
grayscale image. It applies a 5° rotation, Gaussian blur, noise σ=4, 25% side and 15% top/bottom
illumination variation, and contrast from 40 to 210.

| UR fragment | QR version | Modules |
|---|---:|---:|
| 50 bytes | v5 | 37 |
| 100 bytes | v8 | 49 |
| 200 bytes | v11 | 61 |

## Read success by QR size and blur

| QR | Span (px) | px/module | Blur σ | quirc | zxing-cpp |
|---|---|---|---|---|---|
| v5 | 220 / 190 / 160 | 5.9 / 5.1 / 4.3 | 0.5, 1.0 | all pass | all pass |
| v8 | 220 | 4.5 | 0.5 / 1.0 | pass / fail | pass / pass |
| v8 | 190 | 3.9 | 0.5 / 1.0 | pass / pass | pass / pass |
| v8 | 160 | 3.3 | 0.5 / 1.0 | pass / fail | pass / pass |
| v11 | 220 | 3.6 | 0.5 / 1.0 | pass / fail | pass / pass |
| v11 | 190 | 3.1 | 0.5 / 1.0 | fail / fail | pass / pass |
| v11 | 160 | 2.6 | 0.5 / 1.0 | fail / fail | pass / fail |

The v8 failures are not monotonic: one occurs at the largest 220px span. These results need to be
rechecked against camera frames.

## Instructions per frame (RV32)

| Operation | v5 | v8 | v11 |
|---|---:|---:|---:|
| Decode (upstream quirc, double) | 370M | 622M | - |
| Decode (upstream quirc, float) | 231M | 388M | - |
| Decode (patched quirc, float) | 22.5–23.2M | 35.8–36.6M | 40.7M |
| Frame with no QR found | 2.9–3.9M | | |
| Generate QR (qrcodegen) | 4.5M | 7.8M | 12.2M |
| Draw 240×240 RGB565 one row at a time | 1.5M | 1.6M | 1.5M |

- With upstream quirc, `jiggle_perspective` calls `fitness_all` 81 times. One frame calls
  `perspective_map` 430,000 times at about 1,300 instructions each; this accounts for nearly all
  decode time.
- The patch converts coefficients to fixed point at the start of `fitness_all`. `fitness_cell` then
  uses 32×32→64-bit multiplication and 32-bit division, falling back to floating point if the
  coefficients are out of range.
- QEMU used `-march=rv32imac_zicsr_zifencei_zba_zbb_zbs_zbkb_zcb_zcmp`. Building with
  `-mcpu=hazard3-rp2350 -O2` emits the Hazard3-specific Xh3bextm instruction, which QEMU does not
  support.

## Pico 2 H measurements (150MHz, 2026-09-25)

Eighteen generated frames were stored in Flash and run on the device.

| Operation | v5 | v8 | v11 | Effective CPI |
|---|---:|---:|---:|---:|
| Decode (patched quirc) | 0.278–0.285s | 0.447–0.453s | 0.494s | 1.57 |
| Frame with no QR found | 0.029–0.038s | | | 1.48 |
| Generate QR (qrcodegen) | 0.034s | 0.058s | 0.091s | 1.12 |
| Draw 240×240 RGB565 one row at a time | 0.021s | 0.022s | 0.020s | 2.07 |

- Read successes and failures exactly matched QEMU: v8 at 220px/σ1.0 and 160px/σ1.0, and three v11
  cases failed.
- **XIP cache degradation was small.** quirc's effective CPI was 1.57, compared with 7.5 for AOT
  signing code. The frame buffer is in RAM and the hot functions are small.
- Drawing's CPI of 2.07 is dominated by writes to the row buffer, not instruction execution.
- Measured throughput was 3.6 fps for v5 and 2.2 fps for v8. Frames with no readable QR return in
  30ms, so waiting for focus and exposure is relatively cheap.

## RAM estimate (2026-09-25)

The estimate combines measurements from `app` (parser.wasm, core, and UI) with quirc's measured heap
use (91,548 bytes from `mallinfo`).

| Item | Size | Source |
|---|---:|---|
| WAMR pool, including parser.wasm linear memory and UR assembly | 160KB | Maximum measured use: 152,792 bytes |
| parser.wasm copy in RAM | 16KB | The interpreter rewrites it while loading |
| Shared heap: quirc while reading, PSBT buffers during parsing/signing | 92KB | 93,696 bytes measured with `mallinfo` |
| Stack | 32KB reserved; 23KB measured | The default 2KB is insufficient |
| UI buffers, qrcodegen, core, secp256k1, pico-sdk, and other use | 42KB | Remainder of `app` |
| **Total** | **about 341KB / 520KB** | |

- **quirc (90KB) and PSBT buffers (`prevtx_arena` 32KB plus `signed_psbt` 34KB) share the heap in
  sequence.** After QR reading, quirc is released before the PSBT buffers are allocated. The measured
  peak is 93,696 bytes, 64KB below the 159,232 bytes needed without sharing. Run
  `make run ELF=build/rp2350/psbt_bench.elf` to print `mallinfo` at both stages.
- About 180KB remains. Double-buffering the camera would add 77KB.
- **AOT parser.wasm does not fit.** AOT must be expanded into RAM; XIP was seven times slower. The
  pool would grow to 277KB and total use would exceed 520KB. This is one reason the current design
  keeps the parser in the interpreter.

## Constraints and design decisions

1. The sender should use QR density low enough for v8 or below. A 1-input, 2-output P2WPKH PSBT
   (200–300 bytes) takes a few frames; a PSBT with `non_witness_utxo` can take dozens.
2. The device uses the project's quirc fork. It carries the fixed-point changes, unmerged security
   fixes (#158, #159), UBSan and fuzzing work, and a fix for NaN-to-integer conversion in upstream
   `perspective_map()`. The fork's `mcu/README.md` documents the changes and checks.
3. QR decoding remains native and in the TCB. Putting it in WASM would make it about 80 times slower,
   over 10 seconds per frame even with the fixed-point patch.

## OV7675 camera measurements (2026-10-02)

An OV7675 (Arducam B0070) was connected to the Pico 2 H. `camera_test` read actual QR images and
reassembled a v5 QR (about a 50-byte UR fragment).

| Operation | Measurement |
|---|---:|
| Capture | 120ms (VGA frame plus VSYNC wait, about 8fps) |
| Decode, no QR | 62ms |
| Decode, QR found | 300ms (grid extraction and error correction) |
| Effective rate | about 2.5fps |

- **Do not rely on the sensor scaler.** The OV7675's vertical downscaling (`SCALING_DCWCTR`) did not
  work. The sensor outputs VGA YUV422; PIO keeps the first Y from each `Y U Y V` pair and skips every
  other row to produce 320×240. This does not depend on sensor scaling (`src/drivers/camera.pio`).
- The same frame can succeed or return `ECC failure`. Camera shake and exposure matter; success rate
  improves with adequate lighting.
- At startup, `camera_test` reports GP2–GP17 states and PCLK, HREF, and VSYNC edge counts. These help
  find wiring mistakes (in 100ms: over 100,000 PCLK edges, about 1,800 HREF edges, and about 4 VSYNC
  edges).

## Still to verify

- Whether the OV2640 can capture QVGA grayscale through PIO + DMA, and whether capture and decode can
  run on separate cores
- Whether real camera frames can be read reliably through v8; v5 has been read so far. Include
  conditions that failed in synthetic tests

## Reproduce

```sh
make deps
make check-qemu-qr
make check-qemu-qr QUIRC=third_party/quirc/lib QUIRC_DEFS=  # upstream quirc (double)
make check-qr-mac
make run ELF=build/rp2350/qr_bench.elf                    # hardware, via SWD and UART
```
