# Bare-metal WASM Bitcoin Signer 設計書

Raspberry Pi Pico 2 (RP2350 / RISC-V) / Air-gapped / OS-less / Bitcoin Core-derived signing logic
Version 0.2 | 2026-09-22

## 1. 目的

本システムは、Raspberry Pi Pico 2（RP2350）のRISC-Vコア（Hazard3）上でOSを使用せず、最小限のベアメタルホストとWebAssemblyランタイム上でBitcoin署名ロジックを実行する、エアギャップ型ハードウェア署名器を実現することを目的とする。

署名ロジックはハードウェア依存部分から分離した `bitcoin-signer.wasm` として実装し、PSBTの解析・検証・sighash計算・ECDSA/Schnorr署名等を同一WASMモジュールに閉じ込める。実機固有処理はカメラ、ディスプレイ、ボタン、乱数生成などに限定する。

Pi Zero 1.3 はカメラ・ディスプレイを揃えやすい副ターゲットとして残す（§17）。

## 2. 設計原則

## 3. 全体アーキテクチャ

```
┌───────────────────────────────────────────┐
│ Raspberry Pi Pico 2 (RP2350 / Hazard3)    │
│                                           │
│  DVP Camera      SPI Display      Buttons │
│      │               │               │    │
│      └───────────────┬───────────────┘    │
│                      ▼                    │
│  ┌───────────────────────────────────┐    │
│  │ Bare-metal Host                   │    │
│  │ startup / HAL / PIO camera / SPI  │    │
│  │ QR decoder / entropy / WASM rt    │    │
│  └─────────────────┬─────────────────┘    │
│                    │ Minimal Host ABI     │
│                    ▼                      │
│  ┌───────────────────────────────────┐    │
│  │ bitcoin-signer.wasm               │    │
│  │ PSBT / BIP32 / Script / Sighash   │    │
│  │ libsecp256k1 / ECDSA / Schnorr    │    │
│  └───────────────────────────────────┘    │
└───────────────────────────────────────────┘
            ▲                       │
            │ unsigned PSBT / QR    │ signed PSBT / QR
            │                       ▼
      Untrusted wallet software (e.g. desktop wallet)
```

## 4. Trust Boundary / TCB

WASM sandboxは、ホストOSから秘密鍵を守る仕組みではない。本設計ではOSそのものを除外し、TCBを明示的に小さくする。RP2350ではBoot ROMがTCBに残るが、Pi Zeroのような非公開GPUファームウェアは含まない。Hazard3はRTLが公開されており、CPUコアまで監査対象にできる。

## 5. ハードウェア構成

- MCU: Raspberry Pi Pico 2（RP2350、Hazard3 RISC-V ×2、150MHz、SRAM 520KB、Flash 4MB、PSRAM なし）
- カメラ: DVP カメラモジュール（OV2640 等）を PIO で取り込む
- ディスプレイ: SPI LCD
- 入力: 物理ボタン（GPIO）
- 乱数: RP2350 内蔵 TRNG ＋ ユーザー入力エントロピー
- 起動: secure boot / OTP で署名済みファームウェアのみ起動

520KB に WAMR + `bitcoin-signer.wasm` + QR バッファが収まらない場合は、PSRAM 付きボード（Pimoroni Pico Plus 2 等）に切り替える。

## 6. Bare-metal Host

初期PoCでは pico-sdk（RISC-V ビルド）を startup/HAL/driver の実装基盤として使う。最終TCB削減のため、不要なUSB、ファイルシステム等はリンクしない。

- Boot/startup、割り込み、タイマ
- GPIO / physical button driver
- SPI display driver
- PIO DVP camera driver
- Frame buffer（必要最小限）
- QR / animated QR decoder
- Entropy/RNG abstraction
- WASM runtime
- Minimal host ABI
- Memory zeroization / panic handling

## 7. WASM Runtime / ABI

WASIへの依存は原則として持たない。`bitcoin-signer.wasm` はネットワーク、ファイルシステム、clock、process等をimportせず、署名に必要な最小限のABIだけを使用する。

```
host_random(ptr, len) -> status
host_confirm(request_ptr, request_len) -> decision
host_zeroize(ptr, len) -> void
host_log(code) -> void              // debug build only

signer_load_seed(seed_ptr, seed_len) -> handle
signer_parse_psbt(psbt_ptr, psbt_len) -> handle
signer_inspect_psbt(handle, out_ptr, out_len) -> status
signer_sign_psbt(handle, seed_handle, out_ptr, out_len) -> status
signer_destroy(handle) -> void
```

画面描画やカメラ取得を `signer.wasm` から直接行わせるかはPoC後に判断する。セキュリティ上は、signerが構造化された「確認すべき内容」をホストへ返し、ホストが固定UIで表示する方式が実装を単純化できる。一方、表示改ざん耐性の観点ではsigner側に確認画面生成ロジックを寄せる案も比較対象とする。

## 8. bitcoin-signer.wasm

署名モジュールの責務は以下とする。

- PSBT parsing / serialization
- UTXO・script・amount等の整合性検証
- BIP32鍵導出
- legacy / SegWit / Taproot sighash計算
- ECDSA / Schnorr署名（libsecp256k1由来実装を優先）
- P2WPKHを初期対象とし、P2TR、multisig、descriptorへ段階拡張
- 秘密鍵・seed・中間鍵素材のzeroization
- 署名対象の構造化レビュー情報生成

## 9. 署名フロー

```
[Desktop Wallet: untrusted]
        │
        │ unsigned PSBT (QR)
        ▼
[Camera]
        │ raw frame
        ▼
[QR Decoder]
        │ bytes
        ▼
[PSBT Parser / Validator in WASM]
        │
        ├─ reject malformed / unsupported transaction
        │
        ▼
[Review Model]
        │
        ▼
[Trusted Local Display]   amount / destination / fee / warnings
        │
        ▼
[Physical Confirm]
        │
        ▼
[libsecp256k1 signing]
        │
        ▼
[Signed PSBT]
        │
        ▼
[Display as QR]
        │
        ▼
[Desktop Wallet: untrusted]
```

## 10. Seed / Key Management

SeedSignerに近いstatelessモデルを初期方針とする。seedはSeedQR等から一時的に読み込み、RAM上だけに保持し、署名終了または明示的な破棄操作でzeroizeする。microSDへの秘密鍵保存は行わない。

ただし電源断だけでRAM消去を保証したとみなさず、正常終了時の明示zeroization、秘密値のコピー削減、コンパイラ最適化で消去が除去されない実装、クラッシュ時の挙動を検証する。

## 11. Threat Model

## 12. Non-goals（初期版）

- 秘密鍵の永続保存
- Wi-Fi / Bluetooth / Ethernet
- OTA update
- 複数WASMアプリの動的インストール
- 汎用shell / filesystem
- USB経由での署名データ交換
- Secure Element依存
- 高度な物理攻撃・電力解析への完全耐性

## 13. 実装フェーズ

## 14. PoCのDefinition of Done

- Pico 2がRISC-Vコアで、OSなしで起動する
- SPI displayに署名確認画面を表示できる
- 物理ボタンでConfirm / Rejectできる
- PIO経由のDVPカメラからQRを読み込める
- `bitcoin-signer.wasm` がP2WPKH PSBTを解析・署名できる
- 同一PSBT/鍵に対し、PC上のWASMテストとPico 2実機で結果が一致する
- WAMR + `bitcoin-signer.wasm` + QRバッファがSRAM 520KBに収まる
- 秘密値が署名終了後に明示的にzeroizeされる
- ネットワーク関連コードがfirmwareに含まれない
- TCBの構成要素・依存ライブラリ・概算LOCを一覧化できる

## 15. 技術選定で未確定の事項

## 16. 推奨リポジトリ構成

```
baremetal-wasm-signer/
├── signer/
│   ├── bitcoin/            # PSBT / sighash / key logic
│   ├── secp256k1/
│   ├── abi/
│   └── tests/
├── runtime/
│   ├── wasm/
│   └── host-abi/
├── platform/
│   ├── rp2350/
│   │   ├── boot/
│   │   ├── gpio/
│   │   ├── spi/
│   │   ├── display/
│   │   ├── pio-camera/
│   │   └── entropy/
│   └── rpi-zero/           # 副ターゲット
├── qr/
├── test-vectors/
├── tools/
│   └── browser-harness/
└── docs/
    ├── architecture.md
    ├── threat-model.md
    ├── reproducible-build.md
    └── tcb.md
```

## 17. 将来像

`bitcoin-signer.wasm` を特定ハードの資産にせず、ブラウザ、WASI、他のbare-metal hostでも同一モジュールを実行できる状態を目指す。Pico 2版をreference hardwareとする。

```
                  bitcoin-signer.wasm
                           │
          ┌────────────────┼────────────────┐
          ▼                ▼                ▼
  Pico 2 bare metal     Browser      Pi Zero bare metal
  reference target      test UI      secondary target
```

Pi Zero は CSI カメラと既存の SeedSigner 筐体を使える反面、VideoCore の非公開ファームウェアが TCB に残る。K210 を追加すれば Krux と同一ハードでの比較実験ができる（§19）。

## 18. 参考実装・調査対象

- **Bitcoin Core / libsecp256k1** — Bitcoin署名プリミティブおよびテストベクトル
- **SeedSigner** — stateless / air-gapped / QR-based signerのUX・threat model
- **pico-sdk / Hazard3** — RP2350 の startup・PIO・RISC-V コア
- **Circle** — Pi Zero（副ターゲット）向けC++ bare-metal環境・driver reference
- **Circle libcamera** — Raspberry Pi CSI cameraのbare-metal利用
- **WAMR** — MCU/embedded向けWebAssembly runtime候補
- **AkiraOS** — WASM + embedded capability modelの参考設計（本設計ではOS自体は採用しない）

## 19. 類似プロジェクトとの比較（Krux）

Krux は K210（RISC-V）上の MaixPy v1 フォーク（MicroPython）で動く stateless signer で、「Linux なし」は既に実現済み。上流の MaixPy は v4 で MaixCAM（SG2002, Linux）へ移行しており、Krux は v1 系を自前フォークで維持している（2026-09 時点、対応8機種はすべて K210）。

| 観点 | Krux | 本設計 |
|---|---|---|
| OS | なし（Kendryte SDK + MicroPython） | なし（pico-sdk ベースの最小ホスト） |
| 実行モデル | UI・QR・署名が同一 VM・同一メモリ空間 | 署名ロジックのみ WASM に分離、最小 ABI 経由 |
| 署名実装 | embit（Python）+ secp256k1 | libsecp256k1 / Bitcoin Core 由来 |
| 移植性 | K210 + MaixPy フォークに依存 | 同一 `.wasm` をブラウザ・PC・他 MCU で実行 |
| CPU コア | K210（RTL 非公開） | Hazard3（RTL 公開） |
| 起動チェーン TCB | K210 Boot ROM | RP2350 Boot ROM + secure boot |
| RAM | 8MB | 520KB |
| 成熟度 | 実運用中（multisig / Taproot / SeedQR） | signet で一巡（2026-10-02） |

### SeedSigner との比較（UX の手本にしたもの）

| 観点 | SeedSigner | 本設計 |
|---|---|---|
| ハード | Raspberry Pi Zero（v1.3 推奨）+ Waveshare 1.3 インチ LCD HAT + Pi カメラ + microSD | Pico 2 H + 1.54 インチ ST7789 + OV7675 + ブレッドボード |
| 部品代 | BOM 約 $35、完成品で $50 未満（[公式](https://seedsigner.com/seedsigner-independent-custody-guide/)）。組立済みは £65〜£90 / €73 | ¥4,940（2026-10 の秋月価格、送料別。Debug Probe を足すと ¥7,120） |
| OS | Raspberry Pi OS（Linux） | なし（ベアメタル） |
| 言語 | Python（embit） | C + WASM |
| 鍵の置き場 | RAM のみ（microSD には書かない） | RAM のみ（Flash には書かない） |
| CPU | BCM2835（ARM11。RTL 非公開、VideoCore が先に起動する） | RP2350 Hazard3（RISC-V、RTL 公開） |
| RAM | 512MB | 520KB（実使用 341KB） |
| 解析器の隔離 | なし（同一プロセス） | `parser.wasm` に隔離 |
| 機能 | マルチシグ、パスフレーズ、xpub 出力、Nostr ほか多数 | 単署名の P2WPKH / P2TR に署名するだけ |
| 成熟度 | 実運用多数 | signet で一巡 |

**値段はほぼ同じで、機能は向こうが圧倒的に上。** 違いは TCB の大きさと、CPU まで含めた検証可能性にある。
日本では Pi Zero の入手性が悪く割高なので、部品代はむしろこちらが安くなる。

差別化の軸は「OS レス」ではなく、署名ロジックの WASM 分離、PC と実機で同一バイナリの結果一致を検証できる点、CPU コアまでオープンな点に置く。TCB 規模は WASM runtime も MicroPython 同様インタプリタのため、§14 の LOC 一覧で Krux（MaixPy + embit）と並べて比較する。Krux より 1/16 の RAM で同等機能を出せるかも比較点になる。
