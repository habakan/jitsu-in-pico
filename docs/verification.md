# Verification

This page groups the verification procedures for the device. The host and QEMU checks are listed in
the [README](../README.md#check-it-yourself); the process for rebuilding the pinned artifacts is in
[Reproducible builds](reproducible-build.md).

## Signet transaction

This end-to-end check signs and broadcasts a transaction on signet. The PC holds no private key or
seed phrase; the device signs the PSBT.

### Requirements

Install Bitcoin Core and run a signet node:

```sh
brew install bitcoin
mkdir -p "$HOME/Library/Application Support/Bitcoin"
printf 'signet=1\ndaemon=1\n' > "$HOME/Library/Application Support/Bitcoin/bitcoin.conf"
bitcoind
```

Allow substantial disk space for the chain. Do not prune the node: the wallet import below rescans
from `timestamp: 0`. If you use a remote node, keep its data directory on that machine's local disk;
do not mount it over SMB or NFS. Set up SSH forwarding and configure `~/.bitcoin-signet-rpc` with the
RPC command for your node. The helper's `tunnel` command is configured for development, so use your
own SSH tunnel for a different host.

### Procedure

1. Create a watch-only wallet from the descriptor shown by **Show xpub**. Replace the example
   descriptor with the one from the device:

   ```sh
   tools/watchonly.sh init 'wpkh([fingerprint/84h/1h/0h]tpub.../<0;1>/*)'
   ```

   The helper imports the receive (`/0/*`) and change (`/1/*`) branches separately, marks the latter
   as internal, and rescans from the beginning.

2. Get a receive address, fund it from a signet faucet, and check the balance:

   ```sh
   tools/watchonly.sh addr
   tools/watchonly.sh balance
   ```

3. Create a PSBT and the animated QR file to show to the device:

   ```sh
   tools/watchonly.sh send DESTINATION_ADDRESS 0.0005 1
   ```

   Replace `DESTINATION_ADDRESS` with the address to pay.

4. On the device, choose **Scan PSBT**, review the transaction, and approve it. Capture the signed
   UR from the UART log:

   ```sh
   make run SECONDS=300 2>&1 | tee /tmp/run.log
   ```

5. Finalize and broadcast the signed PSBT:

   ```sh
   tools/watchonly.sh broadcast /tmp/run.log
   ```

### Notes

- Faucet transactions can have more than 2,000 outputs. Including their full previous transactions
  can make a PSBT 77KB, or about 770 QR frames. `tools/strip_psbt.py` removes `non_witness_utxo` for
  SegWit inputs and reduces this example to about 339 bytes. This procedure uses single-signature
  P2WPKH; do not apply that shortcut to multisig.
- Import receive and change descriptors separately. Node versions differ in their handling of the
  multipath `<0;1>` form; swapping the branches can make `getnewaddress` return a change address.
- Use `timestamp: 0` so the wallet rescans for existing funds.
- For a remote node, SSH-forward its RPC port to local port 38332 and put a command like this in
  `~/.bitcoin-signet-rpc` (replace the credentials with the node's RPC credentials):

  ```sh
  CLI="bitcoin-cli -signet -rpcconnect=127.0.0.1 -rpcport=38332 -rpcuser=... -rpcpassword=..."
  ```

### Result

On 2026-10-03, the transaction
[`de849e8c…`](https://mempool.space/signet/tx/de849e8c01a39fcf2aa84aaeeccb2ac8aea128086b2f4252539bcab90a0a432f)
was broadcast without putting a private key or seed phrase on the PC. It was 141 vB with a fee of
141 sat.

| Check | Result |
|---|---|
| `core_account_xpub` output | Accepted by Bitcoin Core 31.1; matches embit |
| Change-address detection | Matches the device's ownership check |
| UR round trip | 4 parts sent; 32 returned; reconstructed after 5 parts in 553µs |
| Signature | ECDSA, P2WPKH, one input and two outputs |
