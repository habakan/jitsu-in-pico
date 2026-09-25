# Feasibility: BIP39 シード計算と BIP32 導出を WASM 内で回せるか

2026-09-25 更新（実機計測を追加）。`docs/feasibility.md` と同じ QEMU virt（`-icount shift=0`）で、
`bitcoin-signer.wasm` に追加した PBKDF2-HMAC-SHA512 と BIP32 導出の命令数を測った。

## 結論

- **素の WASM では PBKDF2 が遅すぎる。** BIP39 の 2048 回で 16.9 億命令、150MHz・CPI 1〜1.5 で 11〜17 秒。rv32 上の WAMR では SHA-512 の 64bit 演算がネイティブ比 25 倍になる
- **SHA-512 の圧縮関数だけをホスト import にすると 3.0 億命令（2〜3 秒）。** ループと HMAC/PBKDF2/BIP32 のロジックは WASM 内に残る。import は `host_sha512_compress` 1 個
- **BIP32 導出は secp256k1 の公開鍵計算が支配的で、import では速くならない。** `m/84'/0'/0'/0/0` で 2.3 億命令（1.5〜2.3 秒）、1 段追加ごとに約 7,700 万命令。署名と同じくインタプリタのオーバーヘッド（ネイティブ比 約 70 倍）が効く
- 1-in-2-out の P2WPKH を、キャッシュなし（入力 1 の導出と署名、お釣り 1 の導出検証）で処理すると約 5.4 億命令、3.6〜5.4 秒になる。UX 上の許容範囲かは要判断
- 結果は BIP39 公式ベクタ（12 / 24 単語、passphrase `TREZOR`）と BIP84 の `m/84'/0'/0'/0/0` 公開鍵で、Mac ネイティブ / Mac WAMR / RV32 ネイティブ / RV32 WAMR の全経路と、`hashlib` + embit による独立計算（`tools/ref_kdf.py`）で一致

## 計測結果（RV32, QEMU）

| 処理 | RV32 ネイティブ | WAMR classic | WAMR fast | classic + host SHA-512 |
|---|---|---|---|---|
| PBKDF2（BIP39 seed） | 66.4M | 1,693M | 1,030M | 300M |
| BIP32 `m/84'/0'/0'/0` | 2.2M | 161M | 95M | 154M |
| BIP32 `m/84'/0'/0'/0/0` | 3.2M | 239M | 142M | 231M |
| ECDSA 署名（参考） | 1.0M | 82M | 49M | 82M |

- 24 単語（215 byte）は HMAC の鍵長 128 byte を超えるため、鍵を SHA-512 してから使う経路を通る。命令数は 12 単語とほぼ同じ
- WAMR プールの最大使用量は classic で 36.7KB（署名だけの時は 33.5KB）。`.wasm` は 34KB（host SHA-512 版は 33KB）
- RP2350 ファームは FLASH 125KB、RAM 100.5KB（プール 48KB 込み）

## 実機計測（Pico 2 H、150MHz、2026-09-25）

| 処理 | classic | classic + host SHA-512 | AOT XIP | AOT RAM 展開 |
|---|---|---|---|---|
| PBKDF2（BIP39 seed） | 14.81 s | 3.053 s | 0.979 s | **0.832 s** |
| BIP32 `m/84'/0'/0'/0` | 1.368 s | 1.311 s | 0.625 s | **0.082 s** |
| BIP32 `m/84'/0'/0'/0/0` | 2.029 s | 1.960 s | 0.937 s | **0.122 s** |
| ECDSA 署名（参考） | 0.696 s | 0.696 s | 0.319 s | **0.041 s** |

- インタプリタは QEMU 命令数の見込み（CPI 1.27）どおり。AOT XIP は XIP キャッシュのミスで見込みの 7 倍遅く、RAM に展開すると命令数どおり（CPI 1.08）になる
- host SHA-512 が効くのは PBKDF2 だけで、BIP32 は 4% しか速くならない（公開鍵計算が支配的）という QEMU での見立ても実機で確認できた
- AOT を RAM 展開すれば host SHA-512 なしでも PBKDF2 は 1 秒を切る。import を足す動機は速度からはなくなる

## 操作ごとの見込み（classic + host SHA-512, 150MHz, CPI 1〜1.5）

| 操作 | 命令数 | 時間 |
|---|---|---|
| シード読込（SeedQR / 単語入力後、passphrase 変更ごと） | 300M | 2〜3 秒 |
| xpub 書き出し（`m/84'/0'/0'`、ハードン 3 段 + 公開鍵） | 80M 前後（推定） | 0.5〜0.8 秒 |
| アドレス 1 件の追加表示（1 段導出） | 77M | 0.5〜0.8 秒 |
| PSBT 1 入力の導出 + 署名 | 313M | 2.1〜3.1 秒 |
| お釣り 1 出力の検証 | 231M | 1.5〜2.3 秒 |

アカウント鍵（`m/84'/0'/0'`）をシード読込時に導出して RAM に保持すれば、入力・お釣りごとの導出は 2 段で済み、上表の導出分は 3 分の 2 程度に減る。

## 選択肢

1. **host SHA-512（今回実装）:** PBKDF2 は実用域。ホストは元々 TCB 内でニーモニックも見ているので、安全性の前提は変わらない。ブラウザや WASI ホストでも圧縮関数 1 個を実装すれば同じ `.wasm` が動く
2. **`host_pbkdf2_hmac_sha512` まで出す:** ネイティブ相当の 0.4〜0.7 秒（未計測、ネイティブ値からの推定）。ブラウザでは WebCrypto の PBKDF2 に対応付けられる。署名ロジックを WASM に閉じる範囲はさらに狭まる
3. **WAMR AOT（`wamrc` で RV32 ネイティブ化）:** BIP32 と署名の遅さを根本から解消できる唯一の案。インタプリタ比の改善幅は未計測。ビルド時の LLVM と AOT ローダが TCB に入る
4. **ecmult_gen テーブル 22KB:** 公開鍵計算が 1.3 倍速（署名で計測済み）、RAM +20KB

## 次の判断事項

- `SHA512_HOST=1` を既定にするか（現状は既定 0、`make ... SHA512_HOST=1` で切替）。AOT を RAM 展開するなら不要
- インタプリタのまま行くなら host SHA-512 は必須（14.8 秒 → 3.05 秒）

## 再現手順

```
make check-host                              # Mac: ネイティブ / WAMR classic / fast
make check-qemu-native                       # RV32 ネイティブ
make check-qemu POOL_KB=64                   # RV32 WAMR classic
make check-qemu FAST=1 POOL_KB=256           # RV32 WAMR fast
rm build/bitcoin-signer.wasm && make check-qemu POOL_KB=64 SHA512_HOST=1
uv run tools/ref_kdf.py                      # 期待値の独立計算
```

`SHA512_HOST` や `COMB` を切り替えたときは、Makefile が依存を追わないので `build/bitcoin-signer.wasm` を消してから実行する。
