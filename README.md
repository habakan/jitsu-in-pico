# baremetal-wasm-signer

Raspberry Pi Pico 2（RP2350 / RISC-V Hazard3）上で動くベアメタルの Bitcoin 署名器。
untrusted な入力（PSBT、QR）の解析を WASM に隔離し、鍵と署名はネイティブに置く（案 B）。
OS もファイルシステムも持たず、Flash には鍵を一切書かない。

設計の背景と判断は [docs/design.md](docs/design.md)、構成は [docs/architecture-b.md](docs/architecture-b.md)。

## 現状（2026-10-01）

実機（Pico 2 H + 1.54 インチ ST7789 液晶 + タクトスイッチ 2 個 + Debug Probe）で一巡する。

| 段 | 実測 | 備考 |
|---|---|---|
| BIP39 シード（PBKDF2 2048 回） | 0.47 s | ネイティブ |
| PSBT 解析 | 25 ms | **parser.wasm**（WAMR classic interp） |
| 検査・確認画面の組み立て | 147 ms | ネイティブ |
| 署名（2 入力、ECDSA + Schnorr） | 126 ms | ネイティブ。embit で独立検証済み |
| finalize（署名の差し込み） | 0.8 ms | **parser.wasm** |
| アニメーション QR（UR）出力 | 1 パート 95 ms | スマホで読取可、`@ngraveio/bc-ur` で復元・バイト一致 |
| RAM | 約 341KB / 520KB | quirc と PSBT バッファはヒープを共有 |

カメラから SeedQR で鍵を読み、アニメーション QR（UR）で PSBT を受け取り、確認画面を経て署名し、
署名済み PSBT を QR で返すところまで実機で一巡する。Sparrow（signet）と繋いだ実機の署名が
[ブロックに取り込まれた](https://mempool.space/signet/tx/d22944daeb353fc4e02012f080c632ddc8973b0395442f59d55e091a78610253)。

## 構成

| ディレクトリ | 中身 | TCB |
|---|---|---|
| `core/` | 鍵と署名の中核。BIP32 導出、BIP143/BIP341 sighash、アドレス生成、plan の検査（`core_review`）、low-R grinding、SeedQR | 内 |
| `signer/` | 署名ロジック全体を WASM にした版（案 A の比較用。`bitcoin-signer.wasm`） | - |
| `parser/` | PSBT・UR の解析器（submodule [wasm-psbt-parser](https://github.com/habakan/wasm-psbt-parser)）。`parser.wasm` になる | **外** |
| `runtime/host-abi/` | parser.wasm の呼び出し口。線形メモリとの出入りを範囲検証する境界 | 内 |
| `runtime/wamr-platform/` | WAMR の RP2350 向けプラットフォーム層（malloc も時刻も使わない） | 内 |
| `qr/quirc` | QR デコーダ（submodule [quirc](https://github.com/habakan/quirc) の `mcu` ブランチ。FPU 無し向けに固定小数点化） | 外 |
| `ui/` | 240x240 の画面を組む。確認画面、メニュー、QR 描画。表示先に依存しない | 内 |
| `platform/rp2350/` | 実機のファーム。液晶（ST7789）、ボタン、カメラ（PIO + DMA）、各確認用ファーム | 内 |
| `platform/qemu-riscv32/` | QEMU virt 向けの起動コードとリンク設定（命令数の計測用） | - |
| `host/` | Mac / QEMU で動かす検査用のホスト（署名・PSBT 一巡・QR ベンチ） | - |
| `tools/` | ベクタ生成、参照実装との照合、UART モニタ、PIO シミュレータ | - |
| `patches/` | third_party に当てるパッチ（現在は WAMR 1 件。上流に取り込まれ済み） | - |
| `docs/` | 設計と実現性検証。`docs/internal/` はコミットしない内部メモ | - |

依存（`third_party/`、gitignore 済み）は `make deps` で clone する: libsecp256k1、WAMR 2.4.3、pico-sdk 2.3.1、
QR-Code-generator、spleen フォント、RISC-V ツールチェーン。

## 実機のファーム（`platform/rp2350`）

| ターゲット | 用途 |
|---|---|
| `app` | 本体。メニューから PSBT 署名の一巡（確認画面 → 署名 → アニメーション QR） |
| `app_nolcd` | `app` と同じで、画面の内容を UART に文字で出す（液晶なしでの確認・回帰） |
| `signer` | 署名ロジックを WASM で回したときの時間とメモリを測る（案 A の計測） |
| `psbt_bench` | PSBT 一巡の時間・プール使用量・ヒープ共有の確認（表示とボタンを使わない） |
| `qr_bench` | 埋め込んだ画像で quirc のデコード時間を測る（カメラ不要） |
| `camera_test` | カメラから取り込んで液晶に出し、quirc で読む |
| `pio_loopback_test` | カメラ無しで DVP 取り込みを検証（PIO が同じピンを駆動して読む） |
| `button_test` | GP2〜GP28 のどのピンが GND に落ちたかを出す（配線の切り分け） |

`TEST_SEED=1` でビルドしたものだけ、BIP39 のテストベクタ（`abandon ... about`）を選べる。資金を扱ってはならない。

## 使い方

```
git submodule update --init
make deps                                    # third_party を取得してパッチを当てる
make deps-openocd                            # 一度だけ。SWD 書き込み用（Raspberry Pi のフォーク）

make run ELF=build/rp2350/app.elf SECONDS=180  # 書き込み → 受信開始 → リセット
make flash-swd ELF=build/rp2350/app.elf        # 書き込みだけ
make monitor SECONDS=60                        # UART を受けるだけ
```

配線と部品は [docs/hardware.md](docs/hardware.md)。BOOTSEL から `.uf2` を書く手順も残してある（`make flash`）。

## 検査

```
make check-core        # 中核のベクタ（71 項目）
make check-parser      # 解析器（submodule 側のテスト）
make check-psbt        # PSBT 一巡 + UR の往復 + 署名を embit で独立検証
make check-ui          # 画面の組み立て
make check-seedqr      # SeedQR の読み取り（ASan 付き）
make check-host        # Mac: ネイティブ / WAMR classic / fast
make check-qemu        # RV32 の命令数（-icount shift=0）
make check-qemu-psbt   # RV32 で PSBT 一巡（ホストの出力と一致するか）
make check-qemu-qr     # quirc の命令数
make check-qr-mac      # quirc と zxing-cpp の読取可否を比べる
make check-camera-sim  # camera.pio を Python のシミュレータで検証
```

期待値は独立に作る（`tools/` の embit / hashlib / `@ngraveio/bc-ur` / zxing-cpp）。
テストを足したら必ずミューテーションテストで検出力を確かめる。

## 検証の記録

| 文書 | 内容 |
|---|---|
| [docs/positioning.md](docs/positioning.md) | 何を作っていて、誰のどんな問題を解くのか。公開と資金申請の前提 |
| [docs/architecture.md](docs/architecture.md) | システム構成（信頼境界・一巡・メモリ）。図つき |
| [docs/design.md](docs/design.md) | 設計と、何を信頼しないかの線引き |
| [docs/architecture-b.md](docs/architecture-b.md) | 解析器を WASM に隔離する構成、plan の形式、実装状況、実機計測 |
| [docs/feasibility.md](docs/feasibility.md) | RP2350 に載るか（サイズ・速度）、実機 4 構成の比較 |
| [docs/kdf-feasibility.md](docs/kdf-feasibility.md) | BIP39 / BIP32 の速度と、SHA-512 を import に出す判断 |
| [docs/aot-feasibility.md](docs/aot-feasibility.md) | WAMR AOT。XIP は実機で 7 倍遅く、RAM 展開なら命令数どおり |
| [docs/qr-feasibility.md](docs/qr-feasibility.md) | QR の読み書き、quirc の固定小数点化、RAM 見積り |
| [docs/hardware.md](docs/hardware.md) | GPIO 割り当て、部品ごとの配線、組むときの注意 |
| [docs/breadboard.md](docs/breadboard.md) | ブレッドボードの実配線（行と穴の番号まで） |
| [docs/breadboard.yml](docs/breadboard.yml) | 同じ配線を機械可読にしたもの。`make breadboard` でブレッドボードの絵 |
| [docs/wiring.yml](docs/wiring.yml) | 信号の対応（WireViz）。`make wiring` で配線図と部品表 |

## 上流への還元

- WAMR: classic interpreter の `i64.store` が 4 byte 境界を前提にしていたバグを修正
  （[PR #5123](https://github.com/wasm-micro-runtime/wasm-micro-runtime/pull/5123)、2026-09-30 マージ）。
  非整列アクセスを許さない CPU で踏む。QEMU では再現しない
- quirc: 自前フォーク（`mcu` ブランチ）で固定小数点化、未マージのセキュリティ修正の取り込み、UBSan とファジング
