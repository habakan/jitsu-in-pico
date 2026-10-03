#!/bin/sh
# デバイスが出したディスクリプタから、bitcoind にウォッチオンリーのウォレットを作って一巡する。
# PC 側に鍵は一切置かない。署名はデバイスだけが行う。
#
#   tools/watchonly.sh init 'wpkh([fp/84h/1h/0h]tpub.../<0;1>/*)'
#   tools/watchonly.sh addr                       受取アドレスを出す
#   tools/watchonly.sh balance
#   tools/watchonly.sh send <宛先> <BTC> [sat/vB] PSBT を作り、実機に見せる QR を書き出す
#   tools/watchonly.sh broadcast <署名済み.ur>     finalize して送信する
#   tools/watchonly.sh tunnel                     gpu1 のノードへ SSH トンネルを張る
set -e

CLI="bitcoin-cli -signet"
# 別ホストのノードを使う場合はここで CLI を上書きする（gpu1 への SSH トンネル越しなど）
[ -f "$HOME/.bitcoin-signet-rpc" ] && . "$HOME/.bitcoin-signet-rpc"
WALLET=signer
W="$CLI -rpcwallet=$WALLET"
OUT=build/psbt
HOST=build/host-classic/psbt_host

case "$1" in
init)
    [ -n "$2" ] || { echo "ディスクリプタを渡す"; exit 2; }
    desc=$2
    # 多経路 <0;1> は版によって扱いが違うので、受取と釣りの 2 本に分けて入れる
    recv=$(printf '%s' "$desc" | sed 's|/<0;1>/\*|/0/*|')
    chg=$(printf '%s' "$desc" | sed 's|/<0;1>/\*|/1/*|')
    $CLI -named createwallet wallet_name=$WALLET disable_private_keys=true blank=true descriptors=true \
        load_on_startup=true >/dev/null 2>&1 || $CLI loadwallet $WALLET >/dev/null 2>&1 || true
    # 受取と釣りを取り違えると getnewaddress が釣り側の鍵を返すので、internal は明示で渡す
    for pair in "$recv:false" "$chg:true"; do
        d=${pair%:*}; internal=${pair##*:}
        sum=$($CLI getdescriptorinfo "$d" | sed -n 's/.*"checksum": "\([^"]*\)".*/\1/p')
        [ -n "$sum" ] || { echo "ディスクリプタが不正: $d"; exit 1; }
        # timestamp 0 で頭から再走査する。signet なので数分で終わる
        $W importdescriptors "[{\"desc\":\"$d#$sum\",\"active\":true,\"internal\":$internal,\"timestamp\":0}]"
    done
    $W getwalletinfo | sed -n 's/.*"walletname"/  walletname/p;s/.*"descriptors"/  descriptors/p'
    ;;
addr)
    $W getnewaddress "" bech32
    ;;
balance)
    $W getbalances
    ;;
send)
    [ -n "$3" ] || { echo "使い方: send <宛先> <BTC> [sat/vB]"; exit 2; }
    rate=${4:-1}
    mkdir -p $OUT
    psbt=$($W -named walletcreatefundedpsbt outputs="{\"$2\":$3}" fee_rate=$rate \
        | sed -n 's/.*"psbt": "\([^"]*\)".*/\1/p')
    [ -n "$psbt" ] || { echo "PSBT を作れなかった"; exit 1; }
    printf '%s' "$psbt" | base64 -d > $OUT/spend.full.psbt
    # faucet の入力は出力 2000 個超で 77KB になる。前トランザクションを落とさないと QR に載らない
    uv run -q tools/strip_psbt.py $OUT/spend.full.psbt $OUT/spend.psbt
    $HOST bin2ur $OUT/spend.psbt $OUT/spend
    uv run -q tools/show_ur.py $OUT/spend.ur $OUT/spend.gif 400
    echo "$OUT/spend.gif を実機に見せる（$(wc -c < $OUT/spend.psbt) byte）"
    $CLI decodepsbt "$psbt" | sed -n 's/.*"fee"/  fee/p'
    ;;
tunnel)
    echo "gpu1 の signet RPC を 127.0.0.1:38332 に繋ぐ。終わるときは Ctrl-C"
    exec ssh -N -L 38332:127.0.0.1:38332 gpu1
    ;;
broadcast)
    [ -n "$2" ] || { echo "署名済みの .ur かログを渡す"; exit 2; }
    [ -f "$2" ] || { echo "ファイルが無い: $2"; exit 1; }
    # make run のログをそのまま渡せるよう、UR の行だけ拾う
    grep '^UR:' "$2" > $OUT/signed.ur || { echo "UR の行が無い: $2"; exit 1; }
    echo "$(wc -l < $OUT/signed.ur) パート"
    $HOST ur2bin $OUT/signed.ur $OUT/signed.psbt
    raw=$($CLI finalizepsbt "$(base64 < $OUT/signed.psbt | tr -d '\n')" \
        | sed -n 's/.*"hex": "\([^"]*\)".*/\1/p')
    [ -n "$raw" ] || { echo "finalize できなかった（署名が足りない）"; exit 1; }
    txid=$($CLI sendrawtransaction "$raw")
    echo "https://mempool.space/signet/tx/$txid"
    ;;
*)
    sed -n '2,12p' "$0"
    exit 2
    ;;
esac
