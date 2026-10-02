# Feasibility: RP2350 (Hazard3) で QR を読み書きできるか

2026-09-25 更新（実機計測を追加）。カメラ画像を模した合成フレームを、RV32 の QEMU virt
（`-icount shift=0`）と Pico 2 H 実機で quirc / qrcodegen に通して、命令数・時間・メモリを測った。
カメラはまだ無いので、合成フレームを Flash に埋めて実機で回している（`make run ELF=build/rp2350/qr_bench.elf`）。

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

## 実機計測（Pico 2 H、150MHz、2026-09-25。18 枚を Flash に埋めて実行）

| 処理 | v5 | v8 | v11 | 実効 CPI |
|---|---|---|---|---|
| デコード（パッチ版） | 0.278〜0.285 s | 0.447〜0.453 s | 0.494 s | 1.57 |
| QR が見つからないフレーム | 0.029〜0.038 s | | | 1.48 |
| 生成（qrcodegen） | 0.034 s | 0.058 s | 0.091 s | 1.12 |
| 描画（240x240 RGB565 を 1 行ずつ） | 0.021 s | 0.022 s | 0.020 s | 2.07 |

- 読み取れた / 読み取れなかったフレームの内訳は QEMU と完全に一致した（v8 の 220px/σ1.0 と 160px/σ1.0、v11 の 3 枚が NG）
- **XIP キャッシュの劣化は小さい。** AOT の署名コードでは CPI 7.5 まで落ちたが、quirc は 1.57 に収まる。フレームバッファが RAM にあり、ホットな関数が小さいため
- 描画の CPI 2.07 は行バッファへの書き込みが支配的で、コードではなくメモリ側の律速
- **実測 fps は v5 で 3.6、v8 で 2.2。** 読めないフレームは 30ms で返るので、ピントや露出が合うまでの空振りは軽い

## RAM 見積り（2026-09-25、実測に更新）

読み取りから署名・表示までを 1 つのファームに入れた場合。`app`（parser.wasm + core + UI）の実測に、
quirc のヒープ（実機の `mallinfo` で 91,548 B）を足したもの。

| 項目 | サイズ | 出どころ |
|---|---|---|
| WAMR プール（parser.wasm の線形メモリと UR の組み立てを含む） | 160KB | 実測の最大 152,792 B に余裕 |
| parser.wasm の RAM コピー（interp はロード時に書き換えるため） | 16KB | |
| 共有ヒープ（読み取り中は quirc、解析〜署名は PSBT のバッファ） | 92KB | 実機 `mallinfo` で 93,696 B |
| スタック（`quirc_data` / `datastream` がスタックに載る） | 32KB 確保（実測 23KB） | 既定の 2KB では足りない |
| UI の画面バッファ・qrcodegen・core・secp256k1・pico-sdk ほか | 42KB | `app` の残り |
| 合計 | **約 341KB / 520KB** | |

- **quirc（90KB）と PSBT のバッファ（`prevtx_arena` 32KB + `signed_psbt` 34KB）は同じヒープを順に使う。**
  読み取りが終われば quirc は要らず、解析より前に PSBT のバッファは要らない。実機で確かめたところ、
  quirc を解放してから取り直すとヒープの山は 93,696 B のままで、共有しない場合の 159,232 B に対し 64KB 減る
  （`make run ELF=build/rp2350/psbt_bench.elf` が両方の段の `mallinfo` を出す）
- 余りは約 180KB。カメラをダブルバッファにしても +77KB で収まる
- **parser.wasm を AOT にすると入らない。** AOT は RAM 展開が必須（XIP は 7 倍遅い、`docs/aot-feasibility.md`）で、
  プールが 277KB になるため合計 520KB を超える。案 B で解析器をインタプリタのままにする理由がここにもある

## 分かった制約と判断事項

1. **送信側の QR 密度を下げる運用が前提になる。** QVGA + quirc なら UR フラグメント 100 byte 以下（v8 以下）。1-in-2-out の P2WPKH PSBT（200〜300 byte）なら数フレームで済むが、non-witness UTXO を含む PSBT は数十フレームになる
2. **quirc はフォークして持つ。** 上流は 2025-05 以降マージが止まっていて、未マージのセキュリティ修正（#158、#159）もフォークに取り込んだ。フォーク側で UBSan とファジングを回し、上流にもある浮動小数点の未定義動作（`perspective_map()` の NaN→int）も直した。変更点と検査結果はフォークの `mcu/README.md`
3. **代替案: QR デコードだけ Cortex-M33 側で動かす。** RP2350 の ARM コアは単精度 FPU を持つのでパッチ不要だが、「CPU コアまで RTL 公開」という差別化（design.md §19）が崩れる
4. **QR デコーダは untrusted な入力を解析する。** 現設計ではホスト側（TCB 内）にある。WASM に入れて隔離すると約 80 倍遅くなるので、パッチ版でも 10 秒/フレーム以上になり実用外

## 実カメラでの読み取り（2026-10-02）

OV7675（Arducam B0070）を Pico 2 に繋ぎ、`camera_test` で実際の QR を読んだ。v5（UR 断片 50 byte 相当）を復元できた。

| 項目 | 実測 |
|---|---|
| 取り込み | 120ms（VGA 1 フレーム + VSYNC 待ち、約 8fps） |
| デコード（QR なし） | 62ms |
| デコード（QR あり） | 300ms（格子の抽出と ECC を通るため） |
| 実効 | 約 2.5 fps |

- **センサーのスケーラは当てにしない。** OV7675 では縦の縮小（SCALING_DCWCTR）が効かず 1 フレーム 480 行のままだった。
  VGA の YUV422 をそのまま出させ、PIO 側で `Y U Y V` の先頭の Y だけを拾い、1 行取り込むごとに 1 行読み飛ばして
  320x240 にしている（`platform/rp2350/camera.pio`）。センサーの素性に依存しない
- 同じフレームでも成功と `ECC failure` が混ざる。手ぶれと露出の影響で、照明が十分なら成功率が上がる
- `camera_test` は起動時に GP2〜GP17 の状態と PCLK / HREF / VSYNC のエッジ数を出す。
  **エッジの数で配線の間違いを特定できる**（100ms で PCLK 10 万回以上、HREF 約 1800 回、VSYNC 約 4 回）

## 実機で確かめること

- OV2640 を PIO + DMA で QVGA グレースケール取得できるか、取り込みとデコードを 2 コアで並行できるか
- 実カメラ画像での読取率を v8 まで広げる（v5 は読めた）。特に合成画像で NG だった条件
- ~~実測の fps~~ 済（上の実機計測）。2 コアで取り込みとデコードを並行させれば、デコード時間がそのまま fps になる

## 再現手順

```
make deps
make check-qemu-qr                           # RV32 での命令数・ヒープ・スタック
make check-qemu-qr QUIRC=third_party/quirc/lib QUIRC_DEFS=   # 素の quirc (double)
make check-qr-mac                            # quirc と zxing-cpp の読取可否
make run ELF=build/rp2350/qr_bench.elf       # 実機（SWD で書いてリセットし、UART を受ける）
```
