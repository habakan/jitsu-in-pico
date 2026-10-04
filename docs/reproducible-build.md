# 再現可能ビルド

デバイスが画面に出す `parser.wasm` の SHA-256 と、手元で作った `parser.wasm` の SHA-256 が
一致することを、第三者が確かめられるようにする。これが無いと「同じものが動いている」と言えない。

## 使い方

```sh
make check-repro
```

版とハッシュを固定したツールチェーンを `build/toolchain/` に落とし、4 つの wasm を
作り直して `checksums.txt` と突き合わせる。一致すれば「再現可能」。

```
a6766d13…  build/parser.wasm           解析器（実機にもブラウザにも同じものが載る）
f4841a11…  build/bitcoin-signer.wasm   鍵・導出・署名
55d8d85d…  build/address.wasm          scriptPubKey → アドレス
4409a16e…  build/qr.wasm               QR デコーダ（quirc）
```

## 固定しているもの

| | 版 | tar.gz の SHA-256 |
|---|---|---|
| wasi-sdk | 34.0 | arm64-macos `9c593981…` / x86_64-linux `b761e3a0…` |
| binaryen（`wasm-opt`） | 132 | arm64-macos `98aad827…` / x86_64-linux `195ddc94…` |

`tools/toolchain.sh` が落として検証する。別の環境を足すときは、その環境のハッシュを同じ場所に書く。

## 確かめたこと（2026-10-03）

**macOS arm64 と Linux x86_64 で同じハッシュが出る。** 別の OS、別の CPU、別のマシンで
同じバイト列になることを実際に確認した（`parser.wasm` は 2026-10-03、残り 3 つも同日）。

## 引っかかったこと: `wasm-opt` が PATH にあるだけで結果が変わる

最初に試したとき、同じ版の wasi-sdk なのに macOS は 15,598 byte、Linux は 18,278 byte になった。
ソースも、コンパイラの既定機能も、sysroot の `libc.a` も、リンカの版も同じだった。

原因は **clang のドライバが、PATH に `wasm-opt` があれば黙って後段で実行する**こと。
macOS には Homebrew の binaryen が入っていたので走り、gpu1 には無かったので走らなかった。

```
clang ... -o out.wasm     ->  wasm-ld ... && /opt/homebrew/bin/wasm-opt out.wasm -Oz -o out.wasm
```

対処として、`--no-wasm-opt` でドライバの自動実行を止め、版を固定した `wasm-opt` を明示的に呼ぶ。
このとき **`-Wl,--keep-section=target_features` が要る**。ドライバは wasm-opt を走らせるときだけ
この指定を足していて、外すと `--strip-all` が `target_features` を消し、後から呼んだ `wasm-opt` が
「bulk memory が有効か分からない」と言って検証に失敗する。

```make
WASM_OPT ?= wasm-opt
	$(LLVM)/clang ... --no-wasm-opt -Wl,--keep-section=target_features -o $@ $(SRC) -lc ...
	$(WASM_OPT) $@ -Oz -o $@
```

この罠は、**環境に何が入っているかで成果物が変わる**という最も厄介な種類で、
ハッシュを突き合わせて初めて見つかった。再現可能ビルドを用意する理由そのものでもある。

## 配る形が正しいかを検査する

```sh
make check-wasm     # wasm-tools が要る（brew install wasm-tools）
```

**利用者が「中身を信じなくても確かめられる」性質**を、こちらでも常に確かめる。

| 見るもの | なぜ |
|---|---|
| import が無い | ホスト関数を呼べない。時計もネットワークも触れない |
| メモリに上限がある | `memory.grow` でホストのメモリを食えない |
| 可変 global を輸出しない | ホストから内部状態を書き換えられない |
| table を輸出しない | 間接呼び出しの表を差し替えられない |
| start 関数が無い | 読み込んだだけでは何も動かない |
| 見覚えのない custom 節が無い | 余計なものが混ざっていない |

この検査を入れたときに、**`address.wasm` と `qr.wasm` のメモリに上限が無い**ことが見つかった
（`--max-memory` の付け忘れ）。固定バッファしか使わないので、伸ばせる必要はなかった。

## 依存も固定する

ツールチェーンだけ固定しても、入力が動けば成果物は動く。`third_party/` は全部 commit で固定してある
（`make check-deps` で確認）。とくに **secp256k1 を master の先頭から取ってはいけない**。
鍵を扱うライブラリを、取得した日によって変わる状態で使うことになる。

| | |
|---|---|
| libsecp256k1 | commit 固定 |
| WAMR / pico-sdk | タグに対応する commit 固定 |
| quirc / QR-Code-generator / spleen | commit 固定 |

上げるときは差分を読んでから `Makefile` の `*_REV` を書き換える。

## まだやっていないこと

- ツールチェーンの取得元は GitHub のリリース。配布物そのものの再現可能性は上流に依存する
- デバイスのファームウェア全体（pico-sdk、WAMR を含む）の再現可能ビルドは未着手
