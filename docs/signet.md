# signet での送金手順（bitcoin-cli）

PC 側に鍵を一切置かずに、デバイスだけで署名する手順。開発中の確認にもそのまま使う。
ウォレットソフト（Sparrow など）を介さないので、どこで何が起きているかが全部見える。

## 前提条件

```sh
brew install bitcoin
printf 'signet=1\ndaemon=1\n' > ~/Library/Application\ Support/Bitcoin/bitcoin.conf
bitcoind
```

signet の同期は 26GB ほど。**剪定すると `timestamp:0` の再走査ができなくなる**ので剪定しない。
別ホストのノードを使う場合は下の「別ホストのノード」を見る。

## 送金手順

```sh
# 1. デバイスの Show xpub が出すディスクリプタでウォッチオンリーを作る（再走査に数分）
tools/watchonly.sh init 'wpkh([ee418dad/84h/1h/0h]tpub.../<0;1>/*)'

# 2. 入金先を出して faucet から入れる（https://signetfaucet.com/）
tools/watchonly.sh addr
tools/watchonly.sh balance

# 3. PSBT を作る。実機に見せる QR の GIF も一緒に出る
tools/watchonly.sh send <宛先> 0.0005 1

# 4. 実機で Scan PSBT → 確認 → 承認。署名済みの UR が画面と UART の両方に出る
make run SECONDS=300 2>&1 | tee /tmp/run.log

# 5. ログをそのまま渡す（UR: の行だけ拾う）
tools/watchonly.sh broadcast /tmp/run.log
```

## 注意点

**前トランザクションは落とす。** faucet の入力は出力が 2000 個を超えることがあり、
`walletcreatefundedpsbt` がそれを丸ごと入れると PSBT が 77KB になって QR 770 枚になる。
`tools/strip_psbt.py` が segwit 入力の non-witness UTXO を落とし、339 byte まで減らす。
単署名の P2WPKH では入力額を偽られても署名が無効になるだけなので落として差し支えない
（同じ入力に違う額で二度署名させられるマルチシグとは事情が違う）。

**受取と釣りは別々に import する。** 多経路 `<0;1>` の扱いは版によって違うので、
`/0/*` を `internal:false`、`/1/*` を `internal:true` で入れる。取り違えると
`getnewaddress` が釣り用の鍵を返し、残高の見え方がずれる。

**`timestamp:0` で頭から再走査する。** 既に残高のあるアドレスを拾うため。signet なら数分。

## 別ホストのノード

ディスクを食うので、手元ではなく別のマシンに置くこともできる。
**SMB や NFS のマウント先をデータディレクトリにしてはいけない**（chainstate が LevelDB で、
ロックと fsync の保証が弱く壊れる）。ノード自体をそのマシンで動かし、RPC だけ SSH で持ってくる。

```sh
# 置く側: bitcoind をそのマシンのローカルディスクで動かす
ssh gpu1 '/mnt/sandisk/bitcoin-29.1/bin/bitcoind -datadir=/mnt/sandisk/bitcoin-signet'

# 手元: トンネルを張り、CLI の向き先を書いた ~/.bitcoin-signet-rpc を置く
tools/watchonly.sh tunnel
```

手元の bitcoind が 38332 を握っていると、トンネルが張れない。先に止める。
ssh の設定に別のポート転送があると `ExitOnForwardFailure=yes` で全体が落ちるので付けない。

`~/.bitcoin-signet-rpc` に `CLI="bitcoin-cli -signet -rpcconnect=127.0.0.1 -rpcport=38332 -rpcuser=... -rpcpassword=..."`
を書くと `tools/watchonly.sh` がそれを使う。**ウォレットはノード側にある**ので、
切り替えたら `init` をやり直す。

## 実行結果（2026-10-03）

[`de849e8c...`](https://mempool.space/signet/tx/de849e8c01a39fcf2aa84aaeeccb2ac8aea128086b2f4252539bcab90a0a432f)
を、**PC に秘密鍵もシードフレーズも置かずに**送信した。141 vB、手数料 141 sat。

| 確かめられたこと | |
|---|---|
| `core_account_xpub` の出力 | Bitcoin Core v31.1 がそのまま受理。embit とも一致 |
| 釣りアドレスの判定 | 実機の「自分のもの」判定と Core の内部判定が一致 |
| UR のラウンドトリップ | 送り 4 パート、戻り 32 パート（5 パートで復元、553µs） |
| 署名 | 1 入力 2 出力の P2WPKH、ECDSA |

2026-10-03 に、手元の 26GB を消して gpu1 のノードへ移した。
ディスクリプタから作り直した残高が 1 sat まで一致することで、移行を確かめた。

テスト用のシードと期待値は `docs/internal/signet-test-seed.md`（コミットしない）。
