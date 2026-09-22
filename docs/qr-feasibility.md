# Feasibility: RP2350 (Hazard3) で QR を読み書きできるか

2026-09-22 時点。実機・カメラなし。カメラ画像を模した合成フレームを、RV32 の QEMU virt
（`-icount shift=0`）で quirc / qrcodegen に通して、命令数とメモリを測った。

## 結論

- **素の quirc は使えない。** Hazard3 に FPU が無く、透視補正のフィットがソフト浮動小数点になる。v5 の 1 フレームで 3.7 億命令（150MHz で 2.5〜3.7 秒）
- **fitness 評価だけ固定小数点にすると実用域に入る**（自前のフォーク [habakan/quirc](https://github.com/habakan/quirc) の `mcu` ブランチ、`QUIRC_FIXED_POINT_FITNESS`。submodule `qr/quirc`）。v5 で 2,300 万命令、v8 で 3,600 万命令（フォークで 0 除算とオーバーフローの対策を入れた後は v5 で 2,680 万、v8 で 4,300 万）。CPI 1〜1.5 と仮定して 3〜6 fps。読取結果は素の quirc と全ケースで一致（フォークの 800 枚の比較では 789 枚が一致、誤読は 0）
- **QVGA + quirc で安定して読めるのは v8（UR フラグメント 100 byte）まで。** v11 は条件が良くないと落ちる。zxing-cpp はほぼ全ケース読めるので、限界は画像ではなく quirc 側
- **RAM は足りる。** 署名側 97KB + QR 側 約 120KB で 約 220KB / 512KB
- 表示側（qrcodegen で生成して 240x240 LCD へ 1 行ずつ描画）は v11 でも 1,400 万命令弱で、ボトルネックにならない

## テスト画像

`tools/gen_qr_frames.py` が UR 形式（`UR:CRYPTO-PSBT/12-30/...`、英大文字なので alphanumeric モード、ECC L）の
1 フレームを作り、320x240 グレースケールに合成する。5 度回転、ガウスぼかし、ノイズ σ=4、
左右 25%・上下 15% の照明ムラ、コントラスト 40〜210。

| UR フラグメント | QR バージョン | モジュール数 |
|---|---|---|
| 50 byte | v5 | 37 |
| 100 byte | v8 | 49 |
| 200 byte | v11 | 61 |

## 読取可否（QR の一辺 span と、ぼかし σ を振った）

| QR | span px | px/module | blur σ | quirc | zxing-cpp |
|---|---|---|---|---|---|
| v5 | 220 / 190 / 160 | 5.9 / 5.1 / 4.3 | 0.5, 1.0 | 全 ok | 全 ok |
| v8 | 220 | 4.5 | 0.5 / 1.0 | ok / NG | ok / ok |
| v8 | 190 | 3.9 | 0.5 / 1.0 | ok / ok | ok / ok |
| v8 | 160 | 3.3 | 0.5 / 1.0 | ok / NG | ok / ok |
| v11 | 220 | 3.6 | 0.5 / 1.0 | ok / NG | ok / ok |
| v11 | 190 | 3.1 | 0.5 / 1.0 | NG / NG | ok / ok |
| v11 | 160 | 2.6 | 0.5 / 1.0 | NG / NG | ok / NG |

v8 の NG は px/module が大きい 220 でも出ており、単調ではない。実カメラ画像での再評価が要る。

## 命令数（RV32, 1 フレーム）

| 処理 | v5 | v8 | v11 |
|---|---|---|---|
| デコード（素の quirc, double） | 370M | 622M | - |
| デコード（素の quirc, float） | 231M | 388M | - |
| デコード（パッチ版, float） | 22.5〜23.2M | 35.8〜36.6M | 40.7M |
| QR が見つからないフレーム | 2.9〜3.9M | | |
| 生成（qrcodegen） | 4.5M | 7.8M | 12.2M |
| 描画（240x240 RGB565 を 1 行ずつ） | 1.5M | 1.6M | 1.5M |

- 素の quirc では、`jiggle_perspective` が `fitness_all` を 81 回呼び、1 フレームで `perspective_map` が 43 万回（1 回 約 1,300 命令）走る。これがデコード時間のほぼ全て
- パッチは `fitness_all` の入口で係数を固定小数点に変換し、`fitness_cell` を 32x32→64bit 乗算と 32bit 除算だけで回す。係数が範囲外なら float 版に戻る
- QEMU では `-march=rv32imac_zicsr_zifencei_zba_zbb_zbs_zbkb_zcb_zcmp` でビルドした。`-mcpu=hazard3-rp2350 -O2` は QEMU 非対応の Hazard3 独自命令（Xh3bextm）を出す

## RAM 見積り

| 項目 | サイズ |
|---|---|
| 署名側（WAMR + wasm + pico-sdk、`docs/feasibility.md`） | 97KB |
| quirc ヒープ（320x240 画像 77KB を含む。カメラの DMA 先に兼用） | 90KB |
| スタック（`quirc_data` / `datastream` がスタックに載る） | 23KB |
| qrcodegen 作業領域 | 8KB |
| LCD 行バッファ | 0.5KB |
| 合計 | 約 220KB / 512KB |

カメラをダブルバッファにしても +77KB で 300KB 程度。

## 分かった制約と判断事項

1. **送信側の QR 密度を下げる運用が前提になる。** QVGA + quirc なら UR フラグメント 100 byte 以下（v8 以下）。1-in-2-out の P2WPKH PSBT（200〜300 byte）なら数フレームで済むが、non-witness UTXO を含む PSBT は数十フレームになる
2. **quirc はフォークして持つ。** 上流は 2025-05 以降マージが止まっていて、未マージのセキュリティ修正（#158、#159）もフォークに取り込んだ。フォーク側で UBSan とファジングを回し、上流にもある浮動小数点の未定義動作（`perspective_map()` の NaN→int）も直した。変更点と検査結果はフォークの `mcu/README.md`
3. **代替案: QR デコードだけ Cortex-M33 側で動かす。** RP2350 の ARM コアは単精度 FPU を持つのでパッチ不要だが、「CPU コアまで RTL 公開」という差別化（design.md §19）が崩れる
4. **QR デコーダは untrusted な入力を解析する。** 現設計ではホスト側（TCB 内）にある。WASM に入れて隔離すると約 80 倍遅くなるので、パッチ版でも 10 秒/フレーム以上になり実用外

## 実機で確かめること

- OV2640 を PIO + DMA で QVGA グレースケール取得できるか、取り込みとデコードを 2 コアで並行できるか
- 実カメラ画像（ピント、露出、画面の反射、手ぶれ）での読取率。特に v8 の NG 条件
- 実測の fps（CPI と XIP キャッシュミスの影響）

## 再現手順

```
make deps
make check-qemu-qr                           # RV32 での命令数・ヒープ・スタック
make check-qemu-qr QUIRC=third_party/quirc/lib QUIRC_DEFS=   # 素の quirc (double)
make check-qr-mac                            # quirc と zxing-cpp の読取可否
```
