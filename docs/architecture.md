# システム構成

実機で動いているものの全体像（2026-10-03）。設計の背景は [design.md](design.md)、
解析器を分離する判断は [architecture-b.md](architecture-b.md)。

## 1. 信頼境界

攻撃者が中身を決められるデータ（QR、PSBT）は、**鍵を持たない WASM の中でしか触らない**。
鍵と署名はネイティブ側に置き、両者の往復は固定長のレコード 1 枚に絞る。

```mermaid
flowchart TB
    subgraph untrusted["信頼しない（攻撃者が中身を決められる）"]
        qr["アニメーション QR / SeedQR"]
        wallet["PC のウォレット"]
    end

    subgraph device["デバイス（RP2350、OS なし）"]
        subgraph sandbox["WASM サンドボックス（鍵を持たない）"]
            parser["parser.wasm<br/>UR 復元 → CBOR → PSBT 解析 → Plan 生成<br/>署名の差し込みと UR 符号化"]
        end

        subgraph tcb["TCB（ネイティブ）"]
            boundary["host ABI<br/>線形メモリの範囲検証つきコピー"]
            core["signing core<br/>Plan の再検証 → BIP32 導出 → 所有確認<br/>→ 表示モデル → sighash → 署名"]
            seedqr["SeedQR 解析<br/>BIP39 チェックサム検算"]
            ui["UI<br/>確認画面・メニュー・QR 描画"]
        end

        subgraph drivers["周辺（ネイティブ）"]
            cam["カメラ<br/>PIO + DMA"]
            quirc["quirc<br/>QR デコード"]
            lcd["ST7789 液晶"]
            btn["ボタン"]
        end
    end

    qr --> cam --> quirc
    quirc -->|"QR の文字列"| boundary
    quirc -->|"SeedQR"| seedqr
    seedqr -->|"シード 64 byte"| core
    boundary <--> parser
    boundary <-->|"Plan / 署名"| core
    core --> ui --> lcd
    btn --> ui
    ui -->|"承認"| core
    boundary -->|"署名済み UR"| lcd
    lcd -.->|"カメラで読む"| wallet
    wallet -.->|"未署名 PSBT"| qr
```

境界で守っていること:

- `parser.wasm` は **import を 1 個も持たない**。時計もメモリ確保もネットワークも触れない
- ホストは `parser.wasm` が返したアドレスと長さを `wasm_runtime_validate_app_addr` で検証してからコピーする
- 受け渡しは固定長 5,016 byte の `plan_t` 1 枚。`_Static_assert` でレイアウトを固定している
- **ネイティブ側が Plan を再検証する。** 解析器が嘘をついても、鍵の導出と所有確認はネイティブ側で独立に行う
- 画面に出した内容と署名対象が一致することは、`core_sign` が `core_review` 済みの Plan の SHA-256 一致を要求することで担保する

## 2. 署名の一巡

```mermaid
sequenceDiagram
    participant W as PC のウォレット
    participant C as カメラ + quirc
    participant P as parser.wasm
    participant K as signing core
    participant L as 液晶 + ボタン

    Note over K: SeedQR を読んで鍵を RAM に載せる（0.47 秒）
    W->>C: 未署名 PSBT をアニメーション QR で表示
    loop パートが揃うまで（実測 8 枚中 7 枚で復元）
        C->>P: UR のパート 1 枚
        P-->>L: 進捗「3/8 parts」
    end
    P->>K: Plan（固定長）+ 前の取引
    Note over K: 再検証・鍵導出・所有確認（147ms）
    K->>L: 確認画面 5 枚（送金先・金額・手数料）
    L->>K: 全画面を見てから承認
    Note over K: 署名（2 入力で 126ms）
    K->>P: 署名一覧
    P->>L: 署名済み PSBT を UR のパートに
    L-->>W: アニメーション QR を読ませる
```

## 3. 同じ `.wasm` をどこでも検証できる

解析器はバイト単位で同じものが 4 か所で動く。実機でしか再現しない不具合を切り分けられるのと、
第三者が同じ成果物を独立に検証できるのが利点。

```mermaid
flowchart LR
    src["components/parser/src/*.c<br/>wasm-psbt-parser"] --> wasm["parser.wasm<br/>15,598 byte"]
    wasm --> mac["Mac ネイティブ<br/>make check-psbt"]
    wasm --> qemu["QEMU RV32<br/>命令数を数える"]
    wasm --> dev["RP2350 実機<br/>WAMR interp"]
    wasm -.-> browser["ブラウザ<br/>未実装"]
    mac --> ref["参照実装と突き合わせ<br/>embit / @ngraveio/bc-ur /<br/>Bitcoin Core の rpc_psbt.json"]
    qemu --> ref
    dev --> ref
```

## 4. メモリ（520KB をどう使うか）

```mermaid
flowchart LR
    subgraph ram["RAM 520KB"]
        pool["WAMR プール 160KB<br/>parser.wasm の線形メモリと UR の組み立て"]
        heap["共有ヒープ 92KB<br/>読み取り中は quirc、解析〜署名は PSBT のバッファ"]
        stack["スタック 32KB"]
        other["UI・qrcodegen・core・secp256k1・pico-sdk 58KB"]
        wasmcopy["parser.wasm の RAM コピー 16KB"]
        free["空き 162KB"]
    end
```

- **quirc（90KB）と PSBT のバッファ（66KB）は同じヒープを順に使う。** 読み取りが終われば quirc は要らず、
  解析より前に PSBT のバッファは要らない。実機で山が 93,696 B のままなのを確認した（共有しなければ 159,232 B）
- `parser.wasm` を AOT にすると速いが、RAM 展開が必須（XIP は実機で 7 倍遅い）でプールが 277KB になり、
  カメラと両立できない。案 B では解析器が軽いので、一巡は 8% しか変わらない。**インタプリタのままにする**

## 5. 実測（Pico 2 H、150MHz）

| 段 | 実行場所 | 時間 |
|---|---|---|
| SeedQR → シード（PBKDF2 2048 回） | ネイティブ | 0.47 秒 |
| QR 1 フレームの取り込み | PIO + DMA | 120ms |
| QR のデコード | quirc | 62ms（見つかると 300ms） |
| PSBT 解析 | **parser.wasm** | 25ms |
| 検査・表示の組み立て | ネイティブ | 147ms |
| 署名（2 入力、ECDSA + Schnorr） | ネイティブ | 126ms |
| 署名の差し込み | **parser.wasm** | 0.8ms |
| UR 符号化 + QR 生成 | wasm + ネイティブ | 1 パート 95ms |
