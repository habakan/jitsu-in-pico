# 同じコードがどこでも動く

このプロジェクトのコンセプト。技術的な中身は [architecture.md](architecture.md)、
位置づけと公開の方針は [positioning.md](positioning.md)。

## 一文で

**取引を読み解く部分を鍵から切り離し、同じバイナリをデバイスでもブラウザでも動かせるようにした。**
だから、署名するときに画面へ出る内容を、誰でも自分の手元で再現して確かめられる。

## 何が「同じ」なのか

3 つの WASM に切り出してある。どれも **import を 1 個も持たない**ので、
WASI も JS のポリフィルも要らず、置かれた場所の違いが結果に影響しない。

| | 大きさ | import | 役割 |
|---|---:|---:|---|
| `parser.wasm` | 15,598 B | 0 | UR の復元、PSBT の解析、Plan 生成、署名の差し込み、UR 符号化 |
| `qr.wasm`（quirc） | 16,546 B | 0 | QR のデコード（FPU 無し向けに固定小数点化した自前フォーク） |
| `address.wasm` | 3,058 B | 0 | scriptPubKey → アドレス（bech32 / bech32m / base58check） |

`parser.wasm` は**バイト単位で同じもの**がデバイスに載る。`qr.wasm` と `address.wasm` は
デバイスではネイティブに積むので、同じソースから別のターゲットへ出したものになる。

## 同じバイト列を、違うランタイムで動かす

```mermaid
flowchart LR
    src["C のソース<br/>wasm-psbt-parser / quirc / core"]
    src --> w1["parser.wasm<br/>sha256 a53bd5f7…"]
    src --> w2["qr.wasm"]
    src --> w3["address.wasm"]

    w1 --> wamr["WAMR<br/>RP2350 の実機"]
    w1 --> v8["V8 / JavaScriptCore / SpiderMonkey<br/>ブラウザ・単一 HTML"]
    w1 --> host["ネイティブ / QEMU<br/>検査とベクタ照合"]
    w2 --> v8
    w3 --> v8

    wamr --> same{"結果が一致するか"}
    v8 --> same
    host --> same
    same -->|"一致"| ok["実装もランタイムも正しい傍証"]
    same -->|"食い違う"| bug["どちらかのランタイムのバグ<br/>（実例: WAMR #5123）"]
```

**ランタイムが違うことが利点になる。** 同じ入力に対して結果が食い違えば、解析器ではなく
ランタイム側の問題だと切り分けられる。実際に見つけた
[WAMR の非整列 `i64.store`](https://github.com/wasm-micro-runtime/wasm-micro-runtime/pull/5123) は
まさにこの種類で、QEMU では再現せず実機だけで落ちた。ブラウザという第 3 のランタイムがあれば、
この手の差異を早く捕まえられる。

## 実機とブラウザの対応

```mermaid
flowchart TB
    subgraph dev["デバイス（RP2350、OS なし）"]
        d1["カメラ<br/>PIO + DMA"] --> d2["quirc<br/>ネイティブ"] --> d3["parser.wasm<br/>WAMR"] --> d4["core<br/>ネイティブ・鍵あり"] --> d5["液晶"]
    end

    subgraph web["ブラウザ（単一 HTML 56KB）"]
        b1["カメラ<br/>getUserMedia"] --> b2["qr.wasm<br/>V8"] --> b3["parser.wasm<br/>V8"] --> b4["address.wasm<br/>V8"] --> b5["画面"]
    end

    d3 -. "同じバイト列" .- b3
    d2 -. "同じソース" .- b2
    d4 -. "鍵はデバイスだけ" .- b4
```

同じ PSBT をどちらに食わせても、同じ出力・同じアドレス・同じ手数料が出る。
実際に確かめた値:

```
out 1  0.00100000  bc1q5knnnmmfe7xqdr55g43jyec388dtsah27rxamf
out 2  0.00020000  bc1qnpzzqjzet8gd5gl8l6gzhuc4s9xv0djt0rlu7a
out 3  0.00049000  bc1p3qkhfews2uk44qtvauqyr2ttdsw7svhkl9nkm9s9c3x4ax5h60wqwruhk7
fee    0.00001000 BTC
```

## 鍵は動かさない

「どこでも動く」のは**鍵を持たない部分だけ**。ここを混ぜると安全性の説明が崩れる。

```mermaid
flowchart LR
    subgraph keyzone["鍵が存在してよい範囲"]
        device["デバイスの RAM のみ<br/>Flash には書かない<br/>Lock でゼロ埋め"]
    end

    subgraph nokey["鍵を持たない（どこでも動く）"]
        parse["PSBT の解析"]
        qr["QR のデコード"]
        addr["アドレス生成"]
        view["表示"]
    end

    keyzone -.->|"署名だけを返す"| nokey
    nokey -->|"解析結果を渡す"| keyzone
```

例外として、ブラウザ版には**回復モード**を置く（デバイスが壊れたときの最後の手段）。
既定では鍵の入力欄すら出さず、明示的に切り替えたときだけ有効にする。
詳しくは [positioning.md](positioning.md) の「PWA に鍵と署名も載せるか」。

## 何が嬉しいか

1. **デバイスの表示を手元で再現できる。** 同じ PSBT をブラウザに食わせて、画面と見比べられる
2. **ランタイム間の差分テストになる。** 食い違えばランタイムのバグだと分かる
3. **解析器だけを独立に監査できる。** 15.6KB の WASM 1 個。ファジングも再現可能ビルドも現実的
4. **オフラインで配れる。** 56KB の単一 HTML。通信も Service Worker も要らず、USB で渡せる
5. **ハードが無くても検証できる。** 基板を買わずに、解析と表示のロジックを確かめられる

## できていること・いないこと

| | 状態 |
|---|---|
| 実機で一巡（SeedQR → UR 読取 → 確認 → 署名 → UR 出力） | 済。signet の取引がブロックに入った |
| 単一 HTML で PSBT を解析・表示 | 済（`make viewer`） |
| 単一 HTML でカメラから UR を読む | 実装済、実機の画面での確認はこれから |
| デバイスが自分の `parser.wasm` のハッシュを表示 | 未。これが入るとハッシュの突き合わせが閉じる |
| 自分の鍵かどうかの判定（ブラウザ側） | 未。xpub の入力欄が要る |
| 回復モード（`bitcoin-signer.wasm` を載せる） | 未。wasm 自体は動作確認済み |
| 再現可能ビルド（第三者が同じハッシュを出せる） | 未。公開前の必須項目 |

## 作り方

```
make viewer     # build/viewer.html（単一ファイル）を作ってブラウザで開く
```

`file://` で開いても動く。手元の Mac では `file://` のままカメラも使えた。
Android は未確認、iOS は Safari がローカルファイルを開けないので一度オンラインで読み込む必要がある。
