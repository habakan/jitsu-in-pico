# Feasibility: WAMR + bitcoin-signer.wasm が RP2350 に載るか

2026-09-25 更新（実機計測を追加）。2026-09-22 に QEMU virt（`-icount shift=0`）で測った命令数と、
Pico 2 H（RP2350 RISC-V, 150MHz）での実測を並べる。

## 結論

載る。classic interpreter 構成で静的 RAM 96.7KB / 512KB、Flash 118KB / 4MB。
QR フレームバッファ（QVGA グレースケール 77KB）を足しても 300KB 以上余る。
課題はメモリではなく速度（インタプリタでネイティブ比 約 80 倍）で、AOT を RAM へ展開すれば解消する（実機 ECDSA 41ms）。

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

## 実機計測（Pico 2 H、RP2350 RISC-V 150MHz、2026-09-25）

| 項目 | classic interp | classic + host SHA-512 | AOT XIP | AOT RAM 展開 |
|---|---|---|---|---|
| ECDSA 署名 | 0.696 s | 0.696 s | 0.319 s | **0.041 s** |
| Schnorr 署名 | 1.297 s | 1.298 s | 0.622 s | **0.078 s** |
| BIP39 シード（PBKDF2 2048 回） | 14.81 s | 3.053 s | 0.979 s | **0.832 s** |
| BIP32 `m/84'/0'/0'/0/0` | 2.029 s | 1.960 s | 0.937 s | **0.122 s** |
| ランタイム初期化 | 16.9 ms | 16.9 ms | 2.5 ms | 1.4 ms |
| FLASH | 125.4KB | 124.2KB | 228.6KB | 228.1KB |
| RAM | 135.3KB | 133.0KB | **68.9KB** | 347.5KB |
| WAMR プール最大 | 36.7KB | 36.0KB | 34.9KB | 277.3KB |

- 署名値・シード・公開鍵は 4 構成とも QEMU / Mac ネイティブと完全一致
- **インタプリタは「QEMU 命令数 × 1.27 ÷ 150MHz」で実機時間になる。** 全項目で CPI 1.27±0.01（PBKDF2 のみ 1.35）
- **AOT XIP だけ CPI 7.5 で、命令数からの見込みの 7 倍遅い。** XIP キャッシュ 16KB に対し AOT コードが 150KB あるため。SHA-512 のループはキャッシュに収まるので PBKDF2 だけ CPI 1.1 に留まる
- **AOT を RAM に展開すると全項目 CPI 1.08。** 命令数どおりの速度が出る（`--xip` なし、プール 320KB 指定・実使用 277KB）。RAM 347KB は 512KB に収まるが、QR フレームバッファ 77KB を足すと残り 90KB になる

## 分かった制約

- **wasm バイナリは Flash に置いたまま実行できない。** classic interp はロード時にバイトコードを書き換える（`wasm_loader.c` の `*p_org = EXT_OP_BLOCK` など）ので RAM コピーが要る
- **WASM の定数テーブルは RAM に載る。** data segment は線形メモリへ展開されるため、libsecp256k1 の既定 `ECMULT_GEN_KB=86` はそのまま SRAM を消費する。今回は 2KB（`COMB_BLOCKS=2, COMB_TEETH=5`）を採用
- **線形メモリの 64KB 単位を避けるには WAMR の shrunk memory が要る。** `memory.grow` を使わず `__heap_base` / `__data_end` を export すると、線形メモリが `__heap_base` まで縮む
- **遅さの主因は i64 演算の見込み。** libsecp256k1 の 10x26 field は 64bit 乗算を多用し、rv32 上の WAMR ではソフトウェア処理になる
- **WAMR classic interp の `i64.store` は 4 byte 境界を前提にしている（上流のバグ）。** 線形メモリへの書き込みにオペランドスタック用の `PUT_I64_TO_ADDR` を使っており、非整列対応の `STORE_I64`（fast interp は使っている）を通らない。clang が memcpy を展開した 1 byte 境界の `i64.store` で Hazard3 が例外を上げる。QEMU は非整列アクセスを黙って通すので再現しない。
  **上流に報告して修正が入った**（[PR #5123](https://github.com/wasm-micro-runtime/wasm-micro-runtime/pull/5123)、2026-09-30 に main へマージ）。
  2.4.3 を使う間は `patches/wamr-classic-interp-unaligned-i64-store.patch` を `make deps` が適用する（適用済みなら飛ばす）
- **RP2350 の既定スタック 2KB は薄い。** QEMU 実測は 1.4KB で足りてはいるが、余裕がないので 16KB にした
- WAMR のインタプリタは、signer が浮動小数点を使わなくても libm（`ceil`, `sqrt` など）をリンクする。TCB の LOC に計上する

## 次の判断事項

1. AOT を採るなら XIP ではなく RAM 展開にする。Flash 228KB / RAM 347KB を、カメラのフレームバッファと両立できるかが焦点
2. 案 B（解析器だけ WASM）なら parser.wasm は小さいので、AOT RAM 展開でも RAM は大きく増えない。実機での再計測が必要
3. ホットな関数だけ SRAM に置いて XIP を併用する案（`.time_critical` 相当）は、AOT では関数単位の配置ができないので現状は取れない

## 再現手順

```
make check-host                              # Mac: ネイティブ / WAMR classic / fast
make check-qemu-native                       # RV32 ネイティブ
make check-qemu POOL_KB=40                   # RV32 WAMR classic
make check-qemu FAST=1 POOL_KB=256           # RV32 WAMR fast
make build/rp2350/signer.elf                 # Pico 2 ファーム（--print-memory-usage）
make monitor SECONDS=75                      # Debug Probe の UART を受ける（焼いた後に USB を抜き差し）
```

依存は `third_party/` に clone する（gitignore 済み）: bitcoin-core/secp256k1、
bytecodealliance/wasm-micro-runtime 2.4.3、raspberrypi/pico-sdk 2.3.1、
pico-sdk-tools の riscv-toolchain-16。ホスト側は Homebrew の llvm / lld / wasi-libc / wasi-runtimes / wabt / ninja / qemu。
