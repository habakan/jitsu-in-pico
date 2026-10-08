# jitsu-in-pico

<sup>[English](../../README.md)</sup>

> **jitsu-in ── 実印。** 署名に拘束力を与える印。**pico** は動作先の RP2350 から。


[![CI](https://github.com/habakan/jitsu-in-pico/actions/workflows/ci.yml/badge.svg)](https://github.com/habakan/jitsu-in-pico/actions/workflows/ci.yml)

英語版は [README.md](../../README.md) です。

`jitsu-in-pico` は [jitsu-in](https://github.com/habakan/jitsu-in) の Raspberry Pi Pico 2（RP2350）参照実装です。
ファームウェア、基板固有の UI・カメラ処理、配線資料、実機連携の検証を置いています。
再利用モジュールと言語別の利用例は jitsu-in 側で管理しています。

## 安全上の注意

> **状態: 第三者のレビューを受けていません。本番の資金には使わないでください。**

signet で署名から送信まで一巡しました。ここに載せた数値はこのリポジトリで測ったものですが、
作者以外が攻撃を試みたことを示すものではありません。mainnet の前提や対応範囲は
[制約一覧](limitations.md)をご覧ください。

セキュリティ上の問題は [SECURITY.md](../../SECURITY.md)（英語）に従って報告してください。**公開 issue には書かないでください。**

ブラウザの [PSBT ビューア](https://github.com/habakan/jitsu-in/tree/main/examples/viewer) は jitsu-in 側で管理しています。

デバイスが使う `parser.wasm` は jitsu-in の成果物です。画面の `Parser hash` と
`make check-repro` の記録を突き合わせられます。

| | |
|---|---|
| モジュールの仕様と他環境での利用 | [jitsu-in](https://github.com/habakan/jitsu-in) |
| parser の ABI とホスト実装例 | [jitsu-in parser](https://github.com/habakan/jitsu-in/tree/main/parser)（英語） |

## Pico 2 の実装

カメラで SeedQR を読み、アニメーション QR（UR）で PSBT を受け取ります。画面で内容を確認して署名し、
署名済み PSBT を QR で返します。PC 側に鍵やシードフレーズを置かず、
[signet の取引](https://mempool.space/signet/tx/de849e8c01a39fcf2aa84aaeeccb2ac8aea128086b2f4252539bcab90a0a432f)
を送信しました（手順は [検証手順](../verification.md#signet-transaction)をご覧ください）。

| 段 | 実測 | |
|---|---|---|
| BIP39 シード（PBKDF2 2048 回） | 0.47 s | ネイティブ |
| PSBT 解析 | 25 ms | **parser.wasm**（WAMR classic interp） |
| 検査・確認画面の組み立て | 147 ms | ネイティブ |
| 署名（2 入力、ECDSA + Schnorr） | 126 ms | ネイティブ。embit で独立検証 |
| アニメーション QR 出力 | 1 パート 95 ms | `@ngraveio/bc-ur` で復元・バイト一致 |
| RAM | 約 341KB / 520KB | quirc と PSBT バッファはヒープを共有 |

このリポジトリでは、RP2350 実機、macOS のホスト、QEMU で確認しています。

## 利用方法

### 実機

ビルド済みのファームは [Releases](https://github.com/habakan/jitsu-in-pico/releases) にあります
（`jitsu-in-pico.uf2` が mainnet、`jitsu-in-pico-signet.uf2` が signet）。BOOTSEL を押しながら USB で
つなぎ、表示されたドライブに UF2 をコピーしてください。自分でビルドした場合も同じバイト列になります
（[再現可能ビルド](reproducible-build.md)をご覧ください）。

部品と論理的なピン割り当ては [hardware.md](../hardware.md)（英語）、実配線は [breadboard.md](breadboard.md)（日本語）をご覧ください。

```sh
make deps-openocd                 # 一度だけ。SWD 書き込み用（Raspberry Pi のフォーク）
make run SECONDS=180              # 書き込み → 受信開始 → リセット
make run TESTNET=1 SECONDS=180    # signet 用
```

`TEST_SEED=1` でビルドした場合だけ BIP39 のテストベクタを選べます（既定は 0）。このビルドでは資金を扱わないでください。
実機の運用上の制約は[制約一覧](limitations.md)をご覧ください。

## ビルドと検証

```sh
make check-repro   # 版を固定したツールチェーンで parser.wasm を作り直し、記録と突き合わせる
```

デバイスの `Parser hash` 画面と再現ビルドの結果が一致すれば、
**デバイス上の解析器が公開ソースからビルドされたこと**を確認できます
（[再現可能ビルド](reproducible-build.md)をご覧ください）。

```sh
make check-core        # 中核のベクタ（71 項目）
make check-xpub        # 口座 xpub とディスクリプタ（BIP84 の公式ベクタ）
make check-psbt        # PSBT 一巡 + UR の往復 + 署名を embit で独立検証
make check-ui          # 画面の組み立て
make check-seedqr      # SeedQR の読み取り（ASan 付き）
make check-qemu-psbt   # RV32 で PSBT 一巡（ホストの出力と一致するか）
make check-qemu-qr     # quirc の命令数
make check-qr-mac      # quirc と zxing-cpp の読取可否を比べる
make check-camera-sim  # camera.pio を Python のシミュレータで検証
make -C components/parts/parser test        # 解析器のベクタ（529 項目）
make -C components/parts/parser check-fuzz  # 解析器へのファジング
```

期待値は embit / hashlib / `@ngraveio/bc-ur` / zxing-cpp / Bitcoin Core で独立に作成しています。
テストを追加した場合は、ミューテーションテストで検出力を確かめています。

## 対応範囲

現在は単署名の P2WPKH / P2TR に対応しています。BIP39 パスフレーズ入力とマルチシグは今後対応予定ですが、現行ファームウェアでは使えません。マルチシグの方式は未定です。PSBT v2 には対応していません。詳しくは[制約一覧](limitations.md)をご覧ください。

## このリポジトリの内容

`src/` に UF2 として配布するファームウェア本体、`bringup/` にそれ以外の実機用プログラムがあります。
ホストと QEMU の検査は `tests/` にあります。

| | | TCB |
|---|---|---|
| `components/parts/` | [jitsu-in](https://github.com/habakan/jitsu-in) の固定 submodule。再利用する parser / signer モジュールを含む。仕様とホスト実装例の管理先は jitsu-in | **外** |
| `components/qr/` | QR デコーダ（submodule [quirc](https://github.com/habakan/quirc) の `mcu` ブランチ。FPU 無し向けに固定小数点化） | 外 |
| `src/` | ファームの入口（`main.c`）とピン割り当て（`board_pins.h`） | 内 |
| `src/drivers/` | 液晶（ST7789）、ボタン、カメラ（PIO + DMA）。実機でしか動かない | 内 |
| `src/ui/` | 240x240 の画面を組む。表示先に依存しない | 内 |
| `src/runtime/` | parser.wasm の呼び出し口（線形メモリとの出入りを範囲検証する境界）と WAMR のプラットフォーム層 | 内 |
| `bringup/` | 配線確認と計測。ボタン、カメラ、PIO ループバック、UI なしの PSBT 一巡 | - |
| `tests/` | ホスト（`host/`）、QEMU（`qemu/`）、画面（`ui/`）の検査 | - |
| `tools/` `docs/` | ビルド・検証ツール、配線図、この実装の設計と実測の記録 | - |

`third_party/`（gitignore 済み）の依存は `make deps` で clone します。libsecp256k1、WAMR 2.4.3、
pico-sdk 2.3.1、QR-Code-generator、spleen フォント、RISC-V ツールチェーンを取得します。

### 実機のファームの種類

| ターゲット | 用途 |
|---|---|
| `app` | 本体。PSBT 署名の一巡、xpub 表示、解析器ハッシュ表示 |
| `app_nolcd` | 同じ内容を UART に文字で出す（液晶なしでの回帰） |
| `psbt_bench` `qr_bench` | 時間・メモリの計測 |
| `camera_test` `pio_loopback_test` `button_test` | 配線とカメラの切り分け |

## 設計・検証資料

| 文書 | 内容 |
|---|---|
| [system architecture](../architecture.md)（英語） | 技術スタック・信頼境界・一巡・メモリ構成 |
| [signing architecture](../signing-architecture.md)（英語） | 現在の解析・署名構成と信頼境界 |
| [再現可能ビルド](reproducible-build.md) | ビルド成果物の再現方法 |
| [verification](../verification.md)（英語） | signet での送金とビルド・検証手順 |
| [Feasibility and performance measurements](../feasibility.md)（英語） | RP2350 のメモリ・速度、QR、BIP39 / BIP32、WASM / AOT の計測 |
| [hardware](../hardware.md)（英語）・[breadboard](../breadboard.md)（英語）・[breadboard](breadboard.md)（日本語） | 部品と配線、実配線図 |

計測条件と再現手順は [Feasibility and performance measurements](../feasibility.md)（英語）にまとめています。

## 外部プロジェクトへの貢献

- **WAMR**: classic interpreter の `i64.store` が 4 byte 境界を前提にしていた問題の修正を、固定した upstream commit に含めています
  （[PR #5123](https://github.com/wasm-micro-runtime/wasm-micro-runtime/pull/5123)、2026-09-30 マージ）。
  非整列アクセスを許さない CPU で発生し、QEMU では再現しません。
- **quirc**: 自前フォーク（`mcu` ブランチ）で固定小数点化と未マージのセキュリティ修正の取り込みを行い、
  UBSan とファジングを実施しています。

## 貢献

変更の送り方は [CONTRIBUTING.md](../../CONTRIBUTING.md)（英語）をご覧ください。セキュリティ上の問題は公開 issue ではなく、
[SECURITY.md](../../SECURITY.md)（英語）に従って報告してください。

## ライセンス

ライセンスは MIT です（[LICENSE](../../LICENSE)）。取り込んでいる第三者のコードは [NOTICE](../../NOTICE) をご覧ください。
