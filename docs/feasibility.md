# Feasibility: WAMR + bitcoin-signer.wasm が RP2350 に載るか

2026-09-22 時点。実機なし。RP2350 向けファームのリンク結果と、同じ RV32 WAMR を QEMU virt
（`-mcpu=hazard3-rp2350` でビルド、`-icount shift=0`）で動かした実測から判断する。

## 結論

載る。classic interpreter 構成で静的 RAM 96.7KB / 512KB、Flash 118KB / 4MB。
QR フレームバッファ（QVGA グレースケール 77KB）を足しても 300KB 以上余る。
課題はメモリではなく速度（インタプリタでネイティブ比 約 80 倍）。

## 検証したもの

- `signer/signer.c`: libsecp256k1 で ECDSA / BIP340 Schnorr 署名する最小 WASM（malloc なし、preallocated context）
- import 0 個（WASI・ネットワーク・clock への依存なし）を `wasm-objdump` で確認
- BIP340 test vector #1 の Schnorr 署名が、Mac ネイティブ / Mac WAMR / RV32 ネイティブ / RV32 WAMR の全てで一致。ECDSA（RFC6979）も全経路で一致

## 計測結果（RV32, QEMU）

| 構成 | .wasm | WAMR プール最大 | ECDSA 命令数 | Schnorr 命令数 |
|---|---|---|---|---|
| RV32 ネイティブ（WASM なし） | - | - | 1.05M | 1.89M |
| classic interp, ecmult_gen 2KB | 30KB | 33.5KB | 82.2M | 153.5M |
| fast interp, ecmult_gen 2KB | 30KB | 107KB | 48.5M | 90.7M |
| classic interp, ecmult_gen 22KB | 51KB | 54KB | 63.5M | 116.0M |

- WAMR プールには線形メモリ（shadow stack 16KB + data/bss 約 4KB）を含む。ネイティブスタックは 1.4KB
- 150MHz で CPI 1〜1.5 と仮定すると、classic の ECDSA は 0.5〜0.8 秒、Schnorr は 1.0〜1.5 秒。XIP キャッシュミスは未考慮で、実機で要計測

## RP2350 ファーム（`platform/rp2350`、プール 48KB）

| 項目 | サイズ |
|---|---|
| FLASH | 118,344 B |
| RAM 合計 | 96,704 B |
| うち `.data`（wasm バイナリの RAM コピー 30KB を含む） | 46,668 B |
| うち `.bss`（WAMR プール 48KB を含む） | 50,032 B |

## 分かった制約

- **wasm バイナリは Flash に置いたまま実行できない。** classic interp はロード時にバイトコードを書き換える（`wasm_loader.c` の `*p_org = EXT_OP_BLOCK` など）ので RAM コピーが要る
- **WASM の定数テーブルは RAM に載る。** data segment は線形メモリへ展開されるため、libsecp256k1 の既定 `ECMULT_GEN_KB=86` はそのまま SRAM を消費する。今回は 2KB（`COMB_BLOCKS=2, COMB_TEETH=5`）を採用
- **線形メモリの 64KB 単位を避けるには WAMR の shrunk memory が要る。** `memory.grow` を使わず `__heap_base` / `__data_end` を export すると、線形メモリが `__heap_base` まで縮む
- **遅さの主因は i64 演算の見込み。** libsecp256k1 の 10x26 field は 64bit 乗算を多用し、rv32 上の WAMR ではソフトウェア処理になる
- WAMR のインタプリタは、signer が浮動小数点を使わなくても libm（`ceil`, `sqrt` など）をリンクする。TCB の LOC に計上する

## 次の判断事項

1. 実機で署名時間を測る（`build/rp2350/signer.uf2`、UART0 GP0/GP1 115200bps に結果を出力）
2. 速度が UX 上許容できない場合の選択肢
   - fast interp（RAM +74KB、1.7 倍速）
   - ecmult_gen 22KB（RAM +20KB、1.3 倍速）
   - WAMR AOT（`wamrc` で RV32 ネイティブ化。XIP 可能で速いが、ビルド時の LLVM が TCB に入る）
3. PSBT パース・BIP32・sighash を載せた後のサイズ再計測

## 再現手順

```
make check-host                              # Mac: ネイティブ / WAMR classic / fast
make check-qemu-native                       # RV32 ネイティブ
make check-qemu POOL_KB=40                   # RV32 WAMR classic
make check-qemu FAST=1 POOL_KB=256           # RV32 WAMR fast
make build/rp2350/signer.elf                 # Pico 2 ファーム（--print-memory-usage）
```

依存は `third_party/` に clone する（gitignore 済み）: bitcoin-core/secp256k1、
bytecodealliance/wasm-micro-runtime 2.4.3、raspberrypi/pico-sdk 2.3.1、
pico-sdk-tools の riscv-toolchain-16。ホスト側は Homebrew の llvm / lld / wasi-libc / wasi-runtimes / wabt / ninja / qemu。
