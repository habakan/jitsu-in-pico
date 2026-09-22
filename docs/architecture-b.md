# Architecture B: 解析器を WASM に隔離し、鍵と署名はネイティブに置く

Version 0.1 | 2026-09-22 | 状態: ドラフト（§8 の判断事項が未決）

design.md §3・§7・§8 は、署名ロジック全体を `bitcoin-signer.wasm` に入れる案（案 A）で書かれている。
本書はそれを置き換える案 B を定義する。判断事項が決まったら design.md に反映する。

## 1. 目的

WASM に「信頼できない入力の解析器が乗っ取られても、秘密鍵に届かない」という役割を持たせる。
案 A では PSBT 解析器と鍵が同じモジュールにあり、解析器のバグを突かれると、ノンスや出力 QR 経由で鍵を漏らされる。

## 2. 守るもの・守らないもの

| 攻撃 | 案 B での扱い |
|---|---|
| 細工した PSBT / UR で解析器を乗っ取り、鍵を読む | 防ぐ。鍵は WASM 線形メモリに入らない |
| 乗っ取った解析器が「画面は A、署名は B」をさせる | 防ぐ。表示と sighash を、ネイティブが同じ構造体から作る（§5） |
| 乗っ取った解析器が不正な取引を正直に表示させる | 防がない。ユーザーの確認に委ねる |
| 乗っ取った解析器が出力 PSBT を壊す | 防がない。署名は取引にコミットしているので、壊れた PSBT は無効になるだけ（DoS） |
| QR デコーダ（quirc）の乗っ取り | 防がない。quirc はネイティブのまま TCB に残る（§7） |
| SeedQR の解析器の乗っ取り | 解析器を WASM に置かない（§4）ことで対象外にする |

## 3. 全体像

```
Camera ─> quirc (native) ─> QR payload bytes
                                 │
                                 ▼
             ┌──────── parser.wasm（鍵なし）────────┐
             │ UR / BBQr 復元 → CBOR → PSBT 解析     │
             │ → Transaction Plan（固定長レコード）  │
             └───────────────┬──────────────────────┘
                             │ host_submit_plan(ptr, len)
                             ▼
             ┌──────── signing core (native) ───────┐
             │ Plan 検証 → 鍵導出と所有確認          │
             │ → 表示モデル生成 → sighash → 署名     │
             └───────────────┬──────────────────────┘
                             │ 署名一覧（pubkey, sig）
                             ▼
             parser.wasm: 元 PSBT に partial_sig を挿入して直列化
                             │
                             ▼
                     qrcodegen (native) ─> LCD
```

parser.wasm の import は `host_submit_plan` と、PSBT の再直列化に使う `host_take_signatures` の 2 個だけにする。
乱数、確認、表示、鍵に関わる import は持たせない。

## 4. 責務の分担

| 処理 | 置き場所 | 理由 |
|---|---|---|
| QR 画像デコード（quirc） | native | WASM では 10 秒/フレーム以上（qr-feasibility.md） |
| UR / BBQr の復元、CBOR、PSBT 解析 | parser.wasm | 最も複雑な untrusted 入力解析。暗号処理を含まず軽い |
| xpub / descriptor の解析（multisig） | parser.wasm | untrusted 入力。結果は Plan と同様に固定長レコードで渡す |
| SeedQR / 単語入力の解析 | native | 入力そのものが秘密。WASM に入れると隔離の意味が消える |
| Plan の検証、鍵導出、所有確認、sighash、署名 | native | 鍵を扱う |
| アドレス文字列の生成（bech32 / base58） | native | 解析器が文字列を偽れないように、Plan には scriptPubKey のバイト列だけを載せる |
| 確認画面の文言と数値 | native | 表示と sighash を同じ構造体から作るため |
| 署名済み PSBT の直列化 | parser.wasm | 署名は取引にコミットしているので、ここが乗っ取られても偽造はできない |

## 5. 中核の不変条件

**表示する値と sighash に入れる値は、ネイティブが同じ `plan_t` から作る。**
parser.wasm が元 PSBT と違う Plan を出しても、ユーザーが見るのはその Plan で、署名されるのもその Plan になる。
ネイティブは「Plan が元 PSBT に忠実か」を確かめる必要がない。確かめるのは次の 2 点だけ。

1. Plan が内部で整合しているか（§6）
2. Plan のうち、ユーザーに見せない値（入力額など）が嘘だった場合に被害が出ないか（§6 の 4〜5）

## 6. ネイティブ側の検証

1. **形式:** レコード数と長さが上限内、未使用フィールドがゼロ、予約値が無いこと。可変長の解析はしない
2. **署名対象の入力:** Plan が示すパスで鍵を導出し、公開鍵と `witness_utxo.scriptPubKey` が一致すること（P2WPKH は `0014 || HASH160(pub)`、P2TR は BIP86 tweak 後の x-only key）。一致しない入力には署名しない
3. **お釣り判定:** Plan の「お釣りフラグ」は参考情報として扱う。ネイティブが同じアカウントの change チェーン（`.../1/i`、`i` は上限付き）でスクリプトを再導出し、一致したものだけをお釣りとして表示する
4. **手数料:** 入力額の合計 − 出力額の合計をネイティブで計算する。オーバーフローと負値は拒否する
5. **入力額の嘘への対策:**
   - Taproot（BIP341）は sighash が全入力の額と scriptPubKey にコミットするので、嘘の額で作った署名は無効になる
   - SegWit v0（BIP143）は署名する入力の額にしかコミットしない。そのため 2020 年の手数料攻撃（入力ごとに別の額で 2 回署名させる）が成り立つ。対策は §8-1 で決める
6. **sighash type:** `SIGHASH_ALL` と Taproot の `SIGHASH_DEFAULT` だけを受け付ける（それ以外は将来の設定で許可）
7. **ネットワーク:** mainnet / testnet は Plan ではなく、ネイティブの設定で決める

## 7. 残るリスク

- **quirc は TCB 内の untrusted 入力解析器のまま残る。** ヘッダ込み約 3,000 行。ファジングで補う。WASM に入れると遅すぎる
- **Plan の復号器もネイティブで untrusted 入力を読む。** 固定長レコードに限定し、100 行程度に収める
- **parser.wasm の停止しない入力（無限ループ）。** WAMR の命令数制限の有無を確認する。無ければ UI 側のタイムアウトで中断する
- **WAMR 自体の脆弱性。** 線形メモリの境界チェックが破られると、隔離の前提が崩れる。インタプリタと AOT のどちらを使うかで、TCB が変わる（§8-3）

## 8. 判断事項

1. **SegWit v0 入力の `non_witness_utxo`**
   - (a) 必須にし、ネイティブの最小限の tx パーサ（varint と出力の位置の特定だけ）で txid と額を確かめる。安全だが PSBT が大きくなり、UR のフレーム数が増える
   - (b) 入力が 1 個なら不要、2 個以上なら必須
   - (c) 不要にして警告だけ出す
2. **初期対象のスクリプト種別:** P2WPKH のみか、P2TR（BIP86）も含めるか
3. **parser.wasm のランタイム:** インタプリタ（TCB が小さい、解析なら速度は足りる見込み）か AOT XIP（速いが LLVM がビルド時の TCB に入る）
4. **multisig の時期:** 初期版から入れるか。入れる場合、descriptor の確認画面とセッション中の保持方法（stateless なので毎回読み込むのか）も決める

## 9. Plan の形式（案）

C 構造体をそのまま共有し、`_Static_assert` でサイズとオフセットを固定する（wasm32 と rv32 はどちらも ILP32 のリトルエンディアンで、配置が一致する）。ネイティブは 1 回コピーしてから検証する。

```c
#define PLAN_MAX_INPUTS  16
#define PLAN_MAX_OUTPUTS 16
#define PLAN_MAX_SPK     83   /* OP_RETURN の標準上限。P2TR / P2WSH は 34 */
#define PLAN_MAX_DEPTH   8

typedef struct {
    uint8_t  len;
    uint8_t  bytes[PLAN_MAX_SPK];
} plan_script_t;

typedef struct {
    uint8_t  depth;
    uint32_t fingerprint;
    uint32_t path[PLAN_MAX_DEPTH];
} plan_keypath_t;

typedef struct {
    uint8_t  prev_txid[32];
    uint32_t prev_vout;
    uint32_t sequence;
    uint64_t amount;            /* witness_utxo */
    plan_script_t spk;          /* witness_utxo */
    plan_keypath_t key;         /* 署名しない入力は depth = 0 */
    uint8_t  sighash_type;
} plan_input_t;

typedef struct {
    uint64_t amount;
    plan_script_t spk;
    plan_keypath_t key;         /* お釣り候補。ネイティブが再導出で確かめる */
} plan_output_t;

typedef struct {
    uint32_t magic, version;
    int32_t  tx_version;
    uint32_t locktime;
    uint8_t  n_inputs, n_outputs;
    plan_input_t  inputs[PLAN_MAX_INPUTS];
    plan_output_t outputs[PLAN_MAX_OUTPUTS];
} plan_t;
```

- 上限 16 入力 / 16 出力で 5,016 byte（入力 176、出力 136 byte）。RAM への影響は小さい
- 入出力数の上限は仮置き。SeedSigner / Krux の上限と実際の PSBT を見て決める
- §8-1 で (a) か (b) を選んだ場合、`non_witness_utxo` は Plan とは別のバッファで渡す（上限は PSBT 全体の上限と揃える）

## 10. 次の作業

1. §8 の判断
2. Plan の形式を `runtime/host-abi/plan.h` に確定し、ネイティブの復号器と検証（§6）を書く
3. parser.wasm に PSBT 解析（P2WPKH から）を実装し、Bitcoin Core / BIP174 のテストベクタで Plan を検証する
4. BIP143 / BIP341 の sighash をネイティブで実装し、署名を Bitcoin Core のテストベクタと照合する
