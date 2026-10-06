# 再現可能ビルド

デバイスは読み込んだ `parser.wasm` の SHA-256 を画面に表示する。このページでは、誰でも同じファイルをビルドし、同じハッシュを得る方法を説明する。再現できなければ、「デバイスは読めるソースコードと同じものを実行している」という主張を確かめられない。

## 実行

```sh
make check-repro
```

バージョンとハッシュを固定したツールチェーンを `build/toolchain/` に取得し、このリポジトリが使う WASM 成果物を2つビルドして、`checksums.txt` と照合する。

```
7c89bf15…  build/parser.wasm   デバイスが読み込む解析器
96b78cd6…  build/signer.wasm   鍵導出と署名を行う WASM コンポーネント
```

## 固定しているもの

| | バージョン | アーカイブの SHA-256 |
|---|---|---|
| wasi-sdk | 34.0 | arm64-macos `9c593981…` / x86_64-linux `b761e3a0…` |
| binaryen（`wasm-opt`） | 132 | arm64-macos `98aad827…` / x86_64-linux `195ddc94…` |

取得と検証は `tools/toolchain.sh` が行う。別のプラットフォームを追加するときは、同じスクリプトにハッシュを追加する。

## 確認済みの環境

**macOS arm64 と Linux x86_64 で同じハッシュになる。** 別 OS、別 CPU、別マシンで確認した（2026-10-03。Lime1 採用後の 2026-10-04 にも再確認）。CI は各コミットで Linux 上の再ビルドを行う。

## 注意点: `wasm-opt` が PATH にあるだけで成果物が変わる

最初の試行では、同じ wasi-sdk を使ったのに macOS で 15,598 byte、Linux で 18,278 byte になった。ソース、コンパイラの既定機能、sysroot の `libc.a`、リンカのバージョンは同じだった。

原因は、clang のドライバが PATH 上の `wasm-opt` を見つけると、後から暗黙に実行することだった。Homebrew 版 Binaryen が入っていた macOS では実行され、Linux では実行されなかった。

```sh
clang ... -o out.wasm     ->  wasm-ld ... && /opt/homebrew/bin/wasm-opt out.wasm -Oz -o out.wasm
```

`--no-wasm-opt` でドライバの自動実行を止め、固定した `wasm-opt` を明示的に呼ぶ。さらに `-Wl,--keep-section=target_features` が必要になる。ドライバは自動で `wasm-opt` を実行するときだけこのセクションを残すため、指定しないと `--strip-all` が `target_features` を削除する。その後に呼ぶ `wasm-opt` は bulk memory が有効か判定できず、検証に失敗する。

```make
WASM_OPT ?= wasm-opt
	$(LLVM)/clang ... --no-wasm-opt -Wl,--keep-section=target_features -o $@ $(SRC) -lc ...
	$(WASM_OPT) $@ -Oz -o $@
```

インストール済みのツールに左右されるビルド不具合だった。ハッシュの比較で見つかった。

## Lime1 による WASM 機能の固定

[Lime1](https://github.com/WebAssembly/tool-conventions/blob/main/Lime.md) は、WebAssembly 1.0 と7つの標準化済み phase-5 機能を定めた名前付きレベルである。仕様の作者は内容を変更しないとしている。

```sh
-mcpu=lime1 -Xlinker --features=mutable-globals,multivalue,sign-ext,nontrapping-fptoint,bulk-memory-opt,extended-const,call-indirect-overlong
```

リンカに指定することで、依存コードが SIMD やスレッドを持ち込んだ場合にリンクが失敗する。ランタイムが必要とする機能が知らないうちに増えるのを防ぐ。

| | 変更前 | 変更後 |
|---|---:|---:|
| `parser.wasm` | 15,603 | **15,570** |
| `signer.wasm` | 56,508 | **56,475** |
| `bitcoin-signer.wasm` | 34,410 | **34,377** |

検証に必要な機能も `bulk-memory` から `bulk-memory-opt` に絞られた。

`-mcpu=mvp` は逆の結果になるため使わない。測定では 2.5KB 増え、`bulk-memory` も引き続き必要だった。`-mcpu` が適用されるのは自プロジェクトの翻訳単位だけで、wasi-libc はすでに別の機能でビルドされている。`target_features` は入力の和集合になる。

`--max-memory=N` ではなく `--no-growable-memory` を使う。出力はバイト単位で同じで、メモリ上限が `--initial-memory` とずれることもない。

## 配布物の形式を確認する

```sh
make check-wasm     # wasm-tools が必要（brew install wasm-tools）
```

| 検査項目 | 理由 |
|---|---|
| import がない | ホスト関数（時計、ネットワークなど）を呼び出せない |
| memory に上限がある | `memory.grow` でホストのメモリを使い切れない |
| mutable global を export しない | ホストからモジュール内部の状態を書き換えられない |
| table を export しない | 間接呼び出しの table を差し替えられない |
| start function がない | 読み込み時にコードが実行されない |
| 未知の custom section がない | 余分なデータが追加されていない |

ブラウザの例と補助モジュールは [jitsu-in](https://github.com/habakan/jitsu-in/tree/main/examples/viewer) で管理する。

## 依存関係も固定する

入力が変われば、ツールチェーンを固定しても意味がない。`third_party/` の各依存はコミットに固定してあり、`make check-deps` で確認できる。特に **secp256k1 を master の先端から取得してはならない**。鍵を扱うライブラリの内容が取得日によって変わるためである。

| | 固定方法 |
|---|---|
| libsecp256k1 | コミット |
| WAMR / pico-sdk | タグに対応するコミット |
| quirc / QR-Code-generator / spleen | コミット |

依存を更新するときは差分を確認し、`Makefile` の対応する `*_REV` も更新する。

## ファームウェア

リリース用 UF2 も同じ手順でビルドする。RISC-V ツールチェーンは `make deps` でハッシュを固定して取得する。

```sh
make deps && ./tools/toolchain.sh && make check-repro
make build/rp2350/app.elf               # build/rp2350/app.uf2、mainnet
rm -rf build/rp2350 && make build/rp2350/app.elf TESTNET=1   # signet
```

macOS arm64 と Linux x86_64 で、異なるビルドディレクトリから同じ UF2 を得た（2026-10-06）。差が出たのは pico-sdk がバイナリ情報に書くビルド日だけだったため、ファームウェアでは `PICO_NO_BI_PROGRAM_BUILD_DATE=1` を指定している。

## 未確認事項

- ツールチェーンは GitHub Releases から取得する。アーカイブ自体が再現可能にビルドされているかは確認していない。
