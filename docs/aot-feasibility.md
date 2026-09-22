# Feasibility: WAMR AOT（XIP）で署名ロジックを実用速度にできるか

2026-09-22 時点。実機なし。`bitcoin-signer.wasm` を wamrc（WAMR 2.4.3 + LLVM 18.1.8）で RV32 向け AOT に変換し、
`docs/feasibility.md` と同じ QEMU virt（`-icount shift=0`）で命令数とメモリを測った。

## 結論

- **AOT + XIP で実用速度に入る。** ECDSA 640 万命令、BIP32 `m/84'/0'/0'/0/0` 1,900 万命令、PBKDF2 1.36 億命令（host SHA-512 なし）。1-in-2-out の P2WPKH は約 4,500 万命令で、150MHz・CPI 1〜1.5 なら 0.3〜0.45 秒
- **XIP なら RAM はインタプリタより少ない。** AOT イメージ（150KB）を Flash に置いたまま実行でき、ローダは元バイナリを書き換えない（実行前後で比較して確認）。RP2350 ファームは FLASH 228KB、RAM 68.9KB（インタプリタ版は RAM 100.5KB）
- **非 XIP の AOT は使えない。** ロード時にコードを RAM へ展開し、WAMR プールが 272KB になる
- ネイティブ比はまだ 2〜6 倍。境界チェックを外しても 25% しか速くならないので、主因は境界チェックではない
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

## A / B への影響

| | A: 署名ロジック全体を WASM（AOT XIP） | B: 解析器だけ WASM、鍵と署名はネイティブ |
|---|---|---|
| 1-in-2-out の処理時間 | 0.3〜0.45 秒 | 0.05〜0.08 秒 + 解析 |
| 解析器が乗っ取られた場合 | 同じモジュール内の鍵に届く | 鍵に届かない（表示と署名対象のすり替えは、ネイティブ側の再検証で防ぐ必要あり） |
| 同一 `.wasm` を PC で検証 | 署名ロジック全体 | 解析部分のみ |
| TCB に増えるもの | AOT ローダ、ビルド時の wamrc + LLVM 18 | インタプリタ（解析なら速度は足りる見込み）またはAOT |

## AOT を採る場合の注意

- **ビルド時の LLVM が TCB に入る。** AOT コードの境界チェックが正しいかは LLVM と wamrc のコード生成に依存する。再現可能ビルドでは wamrc と LLVM の版を固定して `.aot` のハッシュを一致させる必要がある
- **実機では XIP キャッシュ（16KB）のミスが効く。** 150KB の AOT コードを QSPI Flash から実行するので、QEMU の命令数より遅くなりうる。ホットな関数だけ SRAM に置く手はあるが、そうすると XIP でなくなる
- QEMU 用の AOT は `generic-rv32` + `m,a,c,zba,zbb,zbs` 向け。Hazard3 の `zbkb`、`zcb`、`zcmp` は使っていない
- RISC-V では AOT コードを書いた後に `fence.i` が要るので、`os_icache_flush` に入れた（XIP では書き込みが起きないため効かない）

## 再現手順

```
make build/wamrc/wamrc                       # llvm@18（Homebrew）で wamrc をビルド
rm -f build/signer_wasm.h && make check-qemu AOT=1 POOL_KB=64
rm -rf build/rp2350 build/signer_wasm.h && make build/rp2350/signer.elf AOT=1
```

`AOT` を切り替えたときは `build/signer_wasm.h` を消してから実行する。
