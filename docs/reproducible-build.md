# 再現可能ビルド

デバイスが画面に出す `parser.wasm` の SHA-256 と、手元で作った `parser.wasm` の SHA-256 が
一致することを、第三者が確かめられるようにする。これが無いと「同じものが動いている」と言えない。

## 使い方

```sh
make repro
```

版とハッシュを固定したツールチェーンを `build/toolchain/` に落とし、それで `parser.wasm` を
作り直して `checksums.txt` と突き合わせる。一致すれば「再現可能」。

```
a53bd5f7772268b776752b7b7a95e479f4196920e0feab9331559dd70320a07f  build/parser.wasm
```

## 固定しているもの

| | 版 | tar.gz の SHA-256 |
|---|---|---|
| wasi-sdk | 34.0 | arm64-macos `9c593981…` / x86_64-linux `b761e3a0…` |
| binaryen（`wasm-opt`） | 132 | arm64-macos `98aad827…` / x86_64-linux `195ddc94…` |

`tools/toolchain.sh` が落として検証する。別の環境を足すときは、その環境のハッシュを同じ場所に書く。

## 確かめたこと（2026-10-03）

**macOS arm64 と Linux x86_64 で、同じ `a53bd5f7…` が出る。** 別の OS、別の CPU、別のマシンで
同じバイト列になることを実際に確認した。

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

## まだやっていないこと

- `address.wasm` と `qr.wasm`（ビューアが載せている残り 2 つ）は親の Makefile で作っていて、
  同じ暗黙の `wasm-opt` の影響を受ける。`repro` の対象に入れる
- `bitcoin-signer.wasm` も同じ
- ツールチェーンの取得元は GitHub のリリース。配布物そのものの再現可能性は上流に依存する
- デバイスのファームウェア全体（pico-sdk、WAMR を含む）の再現可能ビルドは未着手
