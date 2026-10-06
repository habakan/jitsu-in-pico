# Feasibility: WAMR AOT で署名ロジックを実用速度にできるか

2026-09-25 更新（実機計測を追加）。`bitcoin-signer.wasm` を wamrc（WAMR 2.4.3 + LLVM 18.1.8）で RV32 向け AOT に変換し、
`docs/feasibility.md` と同じ QEMU virt（`-icount shift=0`）で命令数とメモリを測った。

## 結論

- **AOT は実用速度に入る。ただし XIP ではだめ（実機で確認）。** RAM へ展開したときだけ QEMU の命令数どおりの速度が出る。実機で ECDSA 41ms、Schnorr 78ms、BIP32 `m/84'/0'/0'/0/0` 122ms、PBKDF2 0.83 秒。1-in-2-out の P2WPKH は 0.2 秒前後
- **XIP は同じコードで 7 倍遅い。** XIP キャッシュ 16KB に対し AOT コードが 150KB あるため。QEMU の命令数からは予測できない差なので、当初の見込み（0.3〜0.45 秒）は外れた
- **速度と RAM のどちらを取るかになる。** XIP は RAM 68.9KB（インタプリタ版 135KB より少ない）、RAM 展開は 347KB。Flash はどちらも 228KB
- ネイティブ比は命令数でまだ 2〜6 倍。境界チェックを外しても 25% しか速くならないので、主因は境界チェックではない
- 性能はもはや A（署名ロジック全体を WASM）を否定する理由にならない。A と B（解析器だけ WASM）の選択は、鍵を解析器から隔離するかというセキュリティ上の判断だけになる

## 計測結果（RV32, QEMU, 命令数）

| 処理 | ネイティブ | classic interp | AOT 非 XIP | AOT XIP（既定） | AOT XIP（i64 も関数呼出） |
|---|---|---|---|---|---|
| ECDSA 署名 | 1.05M | 82.2M | 5.68M | 6.42M | 12.2M |
| Schnorr 署名 | 1.89M | 153M | 10.9M | 12.2M | 23.3M |
| PBKDF2（BIP39） | 66.4M | 1,693M | 119M | 136M | 177M |
| BIP32 `m/84'/0'/0'/0/0` | 3.19M | 239M | 16.9M | 19.0M | 35.9M |
| WAMR プール最大 | - | 36.7KB | 272KB | 35.1KB | 35.1KB |
| 元バイナリの書換 | - | あり | なし | なし | なし |

- 「AOT XIP（既定）」は `Makefile` の `WAMRC_FLAGS`。`--xip` に加え、ランタイム関数呼び出しにする intrinsic を i64 の除算・剰余（rv32 で libgcc 呼び出しになるもの）と定数・浮動小数点に絞っている。wamrc の riscv32 向け既定は i64 の乗算・シフト・AND/OR まで関数呼び出しにし、約 2 倍遅い（右端の列）
- `--bounds-checks=0` にした場合（参考、MCU では使えない）: ECDSA 4.33M、PBKDF2 97.0M、BIP32 12.9M
- 全構成で BIP340 / BIP39 / BIP84 のベクタが一致

## 実機計測（Pico 2 H、150MHz、2026-09-25）

| 処理 | QEMU 命令数 | AOT XIP 実測 | CPI | AOT RAM 展開 実測 | CPI |
|---|---|---|---|---|---|
| ECDSA 署名 | 6.42M / 5.68M | 0.319 s | 7.5 | **0.041 s** | 1.08 |
| Schnorr 署名 | 12.2M / 10.9M | 0.622 s | 7.7 | **0.078 s** | 1.08 |
| PBKDF2（BIP39） | 136M / 119M | 0.979 s | 1.1 | **0.832 s** | 1.05 |
| BIP32 `m/84'/0'/0'/0/0` | 19.0M / 16.9M | 0.937 s | 7.4 | **0.122 s** | 1.08 |
| FLASH | - | 228.6KB | - | 228.1KB | - |
| RAM | - | 68.9KB | - | 347.5KB | - |
| WAMR プール最大 | - | 34.9KB | - | 277.3KB | - |

- 命令数の列は「XIP 版 / 非 XIP 版」。実効 CPI は実測時間 × 150MHz ÷ 命令数
- **XIP は secp256k1 の経路だけ CPI 7.5。** PBKDF2 が CPI 1.1 に留まるのは、SHA-512 の圧縮関数が小さく XIP キャッシュ 16KB に収まるため。つまり遅さはコードサイズとキャッシュの関係で決まり、命令数では予測できない
- **RAM 展開なら全項目 CPI 1.08** で、QEMU の命令数がそのまま実機時間になる。インタプリタ比 17 倍速
- RAM 展開版は `--xip` と `--enable-builtin-intrinsics` を外してビルドし、WAMR プールを 320KB 取る（実使用 277KB）

## A / B への影響

| | A: 署名ロジック全体を WASM（AOT） | B: 解析器だけ WASM、鍵と署名はネイティブ |
|---|---|---|
| 1-in-2-out の処理時間（実機） | 0.2 秒前後（RAM 展開）/ 1.5 秒（XIP） | 0.05〜0.08 秒 + 解析 |
| 解析器が乗っ取られた場合 | 同じモジュール内の鍵に届く | 鍵に届かない（表示と署名対象のすり替えは、ネイティブ側の再検証で防ぐ必要あり） |
| 同一 `.wasm` を PC で検証 | 署名ロジック全体 | 解析部分のみ |
| TCB に増えるもの | AOT ローダ、ビルド時の wamrc + LLVM 18 | インタプリタ（解析なら速度は足りる見込み）またはAOT |

## AOT を採る場合の注意

- **ビルド時の LLVM が TCB に入る。** AOT コードの境界チェックが正しいかは LLVM と wamrc のコード生成に依存する。再現可能ビルドでは wamrc と LLVM の版を固定して `.aot` のハッシュを一致させる必要がある
- **XIP は採らない（実測で 7 倍遅い）。** AOT を採るなら RAM 展開（RAM 347KB）で、Flash に置いたまま実行する利点は捨てる。ホットな関数だけ SRAM に置く配置は wamrc では指定できない
- QEMU 用の AOT は `generic-rv32` + `m,a,c,zba,zbb,zbs` 向け。Hazard3 の `zbkb`、`zcb`、`zcmp` は使っていない
- RISC-V では AOT コードを書いた後に `fence.i` が要るので、`os_icache_flush` に入れた（XIP では書き込みが起きないため効かない）

## 再現手順

旧 `bitcoin-signer.wasm`（jitsu-in v0.1.0 で削除）を対象にした手順で、今のツリーでは動かない。
再実行するときは `f2f61c6` を checkout する。

```
make build/wamrc/wamrc                       # llvm@18（Homebrew）で wamrc をビルド
rm -f build/signer_wasm.h && make check-qemu AOT=1 POOL_KB=64
rm -rf build/rp2350 build/signer_wasm.h && make build/rp2350/signer.elf AOT=1
# RAM 展開版（実機）
rm -f build/bitcoin-signer.aot build/signer_wasm.h && rm -rf build/rp2350 && make AOT=1 RP2350_POOL_KB=320 \
  WAMRC_FLAGS="--target=riscv32 --target-abi=ilp32 --cpu=generic-rv32 --cpu-features=+m,+a,+c,+zba,+zbb,+zbs --bounds-checks=1" \
  build/rp2350/signer.elf
```

`AOT` を切り替えたときは `build/signer_wasm.h` を消してから実行する。
