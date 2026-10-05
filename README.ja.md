# jitsu-in-pico

> **jitsu-in ── 実印。** 署名に拘束力を与える印。**pico** は動作先の RP2350 から。


[![CI](https://github.com/habakan/jitsu-in-pico/actions/workflows/ci.yml/badge.svg)](https://github.com/habakan/jitsu-in-pico/actions/workflows/ci.yml)

英語版は [README.md](README.md)。

`jitsu-in-pico` は [jitsu-in](https://github.com/habakan/jitsu-in) の Raspberry Pi Pico 2（RP2350）参照実装。
ファームウェア、基板固有の UI・カメラ処理、配線資料、実機連携の検証を置いている。
再利用モジュールと言語別の利用例は jitsu-in 側で管理する。

ブラウザの [PSBT ビューア](https://github.com/habakan/jitsu-in/tree/main/examples/viewer) と
[Android サンプル](https://github.com/habakan/jitsu-in-android) は別リポジトリに移した。

<img src="components/parts/docs/everywhere.svg" alt="The same bytes run everywhere" width="940">

デバイスが使う `parser.wasm` は jitsu-in の成果物。画面の `Parser hash` と
`make check-repro` の記録を突き合わせられる。

| | |
|---|---|
| モジュールの仕様と他環境での利用 | [jitsu-in](https://github.com/habakan/jitsu-in) |
| Pico 2 の設計と利用例 | [docs/positioning.md](docs/positioning.md) |
| parser の ABI とホスト実装例 | [jitsu-in parser](https://github.com/habakan/jitsu-in/tree/main/parser)（英語） |

## 安全上の注意

**第三者のレビューを受けていない。本番の資金に使わないこと。**

signet で一巡していて、このページの主張はすべてこのリポジトリ内の実測に基づく。
ただしそれは、作者以外から攻撃されたことがある、ということではない。
mainnet の前提は [docs/architecture-b.md](docs/architecture-b.md) §15、
受理する範囲と「やらないこと」は [docs/limitations.md](docs/limitations.md)（英語）。

セキュリティ上の問題は [SECURITY.md](SECURITY.md) へ。**公開の issue には書かないこと。**

## Pico 2 の実装

カメラで SeedQR を読み、アニメーション QR（UR）で PSBT を受け取る。画面で内容を確認して署名し、
署名済み PSBT を QR で返す。PC 側に鍵やシードフレーズを置かずに、
[signet の取引](https://mempool.space/signet/tx/de849e8c01a39fcf2aa84aaeeccb2ac8aea128086b2f4252539bcab90a0a432f)
を通した（手順は [docs/signet.md](docs/signet.md)）。

| 段 | 実測 | |
|---|---|---|
| BIP39 シード（PBKDF2 2048 回） | 0.47 s | ネイティブ |
| PSBT 解析 | 25 ms | **parser.wasm**（WAMR classic interp） |
| 検査・確認画面の組み立て | 147 ms | ネイティブ |
| 署名（2 入力、ECDSA + Schnorr） | 126 ms | ネイティブ。embit で独立検証 |
| アニメーション QR 出力 | 1 パート 95 ms | `@ngraveio/bc-ur` で復元・バイト一致 |
| RAM | 約 341KB / 520KB | quirc と PSBT バッファはヒープを共有 |

このリポジトリで確認した環境: RP2350 実機、macOS のホスト、QEMU。

未対応: マルチシグ、パスフレーズ、PSBT v2。単署名の P2WPKH / P2TR だけ。

## 使い方

### 実機

部品と配線は [docs/hardware.md](docs/hardware.md)、実配線は [docs/breadboard.md](docs/breadboard.md)。

```sh
make deps-openocd                 # 一度だけ。SWD 書き込み用（Raspberry Pi のフォーク）
make run SECONDS=180              # 書き込み → 受信開始 → リセット
make run TESTNET=1 SECONDS=180    # signet 用
```

`TEST_SEED=1` でビルドしたものだけ BIP39 のテストベクタを選べる（既定は 0）。資金を扱ってはならない。
本番で使うときの手順は [docs/architecture-b.md](docs/architecture-b.md) の「本番で使うときの手順」。

## 検証

```sh
make check-repro   # 版を固定したツールチェーンで parser.wasm を作り直し、記録と突き合わせる
```

デバイスの `Parser hash` 画面と再現ビルドの結果が一致すれば、
**デバイスの中で動いている解析器は公開ソースから出たもの**だと言える
（[docs/reproducible-build.md](docs/reproducible-build.md)）。

```sh
make check-core        # 中核のベクタ（71 項目）
make check-xpub        # 口座 xpub とディスクリプタ（BIP84 の公式ベクタ）
make check-psbt        # PSBT 一巡 + UR の往復 + 署名を embit で独立検証
make check-ui          # 画面の組み立て
make check-seedqr      # SeedQR の読み取り（ASan 付き）
make check-host        # Mac: ネイティブ / WAMR classic / fast
make check-qemu-psbt   # RV32 で PSBT 一巡（ホストの出力と一致するか）
make check-qemu-qr     # quirc の命令数
make check-qr-mac      # quirc と zxing-cpp の読取可否を比べる
make check-camera-sim  # camera.pio を Python のシミュレータで検証
make -C components/parts/parser test        # 解析器のベクタ（529 項目）
make -C components/parts/parser check-fuzz  # 解析器へのファジング
```

期待値は独立に作る（embit / hashlib / `@ngraveio/bc-ur` / zxing-cpp / Bitcoin Core）。
テストを足したらミューテーションテストで検出力を確かめる。

## このリポジトリの内容

`apps/` には Raspberry Pi Pico 2 のファームウェアだけを置く。ホスト・QEMU の連携検査は `tests/` に置く。

| | | TCB |
|---|---|---|
| `components/parts/` | [jitsu-in](https://github.com/habakan/jitsu-in) の固定 submodule。再利用する parser / signer モジュールを含む。仕様とホスト実装例の管理先は jitsu-in | **外** |
| `components/qr/` | QR デコーダ（submodule [quirc](https://github.com/habakan/quirc) の `mcu` ブランチ。FPU 無し向けに固定小数点化） | 外 |
| `apps/device/rp2350/` | 実機のファーム。液晶（ST7789）、ボタン、カメラ（PIO + DMA） | 内 |
| `apps/device/ui/` | 240x240 の画面を組む。表示先に依存しない | 内 |
| `apps/device/runtime/` | parser.wasm の呼び出し口（線形メモリとの出入りを範囲検証する境界）と WAMR のプラットフォーム層 | 内 |
| `tests/host/` | macOS / QEMU で動かす連携検査用ホスト | - |
| `tools/` `docs/` | ビルド・検証ツール、配線図、この実装の設計と実測の記録 | - |

依存（`third_party/`、gitignore 済み）は `make deps` で clone する:
libsecp256k1、WAMR 2.4.3、pico-sdk 2.3.1、QR-Code-generator、spleen フォント、RISC-V ツールチェーン。

### 実機のファームの種類

| ターゲット | 用途 |
|---|---|
| `app` | 本体。PSBT 署名の一巡、xpub 表示、解析器ハッシュ表示 |
| `app_nolcd` | 同じ内容を UART に文字で出す（液晶なしでの回帰） |
| `psbt_bench` `qr_bench` `signer` | 時間・メモリの計測 |
| `camera_test` `pio_loopback_test` `button_test` | 配線とカメラの切り分け |

## 設計・検証資料

| 文書 | 内容 |
|---|---|
| [docs/architecture.md](docs/architecture.md) | システム構成（信頼境界・一巡・メモリ）。図つき |
| [docs/design.md](docs/design.md) | 設計と、何を信頼しないかの線引き |
| [docs/architecture-b.md](docs/architecture-b.md) | 解析器を WASM に隔離する構成、plan の形式、残作業 |
| [docs/reproducible-build.md](docs/reproducible-build.md) | 再現可能ビルドと、`wasm-opt` が PATH にあるだけで成果物が変わる罠 |
| [docs/signet.md](docs/signet.md) | bitcoin-cli でのウォッチオンリー運用と一巡 |
| [docs/feasibility.md](docs/feasibility.md) | RP2350 に載るか（サイズ・速度）、実機 4 構成の比較 |
| [docs/kdf-feasibility.md](docs/kdf-feasibility.md) | BIP39 / BIP32 の速度と、SHA-512 を import に出す判断 |
| [docs/aot-feasibility.md](docs/aot-feasibility.md) | WAMR AOT。XIP は実機で 7 倍遅い |
| [docs/qr-feasibility.md](docs/qr-feasibility.md) | QR の読み書き、quirc の固定小数点化、RAM 見積り |
| [docs/hardware.md](docs/hardware.md) [docs/breadboard.md](docs/breadboard.md) | 配線。`make wiring` `make breadboard` で図を作る |
| [docs/terms.md](docs/terms.md) | 文書で使う用語と章立て |

失敗もそのまま残してある。AOT の XIP が 7 倍遅いこと、WAMR の非整列 `i64.store`、
`wasm-opt` が PATH にあるだけで成果物が 2.7KB 変わること、quirc が液晶の遠景を読めないこと。

## 外部プロジェクトへの貢献

- **WAMR**: classic interpreter の `i64.store` が 4 byte 境界を前提にしていたバグを修正
  （[PR #5123](https://github.com/wasm-micro-runtime/wasm-micro-runtime/pull/5123)、2026-09-30 マージ）。
  非整列アクセスを許さない CPU で踏む。QEMU では再現しない
- **quirc**: 自前フォーク（`mcu` ブランチ）で固定小数点化、未マージのセキュリティ修正の取り込み、
  UBSan とファジング

## 貢献

変更の送り方は [CONTRIBUTING.md](CONTRIBUTING.md) を参照。セキュリティ上の問題は公開 issue ではなく、
[SECURITY.md](SECURITY.md) に従って報告する。

## ライセンス

MIT（[LICENSE](LICENSE)）。取り込んでいる第三者のコードは [NOTICE](NOTICE) を見る。
