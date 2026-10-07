# Limitations

What this signer accepts, what it refuses, and what it deliberately does not do.
Written so a reviewer can tell a bug from a documented boundary. If something here surprises you,
that is worth [reporting](../SECURITY.md) even though it is listed.

## Status

Not reviewed by a third party. signet only. See [Disclaimer](../README.md#disclaimer).

## Transactions

- **PSBT v0 only** (BIP174). v2 is rejected.
- **Single-signature only.** The firmware supports P2WPKH (BIP84) and P2TR key-path (BIP86).
  Multisig support is planned, but the script formats have not been decided.
- **[BIP39](https://github.com/bitcoin/bips/blob/master/bip-0039.mediawiki) passphrase input is planned but not available in the firmware yet.** The shared `signer.wasm`
  interface accepts a passphrase; the Pico firmware currently derives the seed with an empty passphrase.
- At most **16 inputs and 16 outputs**, and a PSBT of at most **32,768 bytes**.
- scriptPubKey at most **83 bytes** (the standard OP_RETURN limit; P2TR and P2WSH are 34).
- Script-path taproot spends, miniscript, and time locks beyond nLockTime passthrough are not handled.
- Every input must carry `witness_utxo` or `non_witness_utxo`. When both are present, the previous
  transaction must hash to the stated txid and its output must match the `witness_utxo`.
- Signatures are **deterministic**: the same PSBT always produces the same bytes. BIP340 `aux_rand`
  is zero, as in Bitcoin Core, Trezor, Jade and BDK. Before adding multisig, the nonce strategy must
  change: [BIP340](https://github.com/bitcoin/bips/blob/master/bip-0340.mediawiki) says multisignature schemes are insecure with deterministic nonce generation.

## What the device checks before it signs

- It derives every key itself and compares the resulting scriptPubKey with the one in the PSBT.
  A claimed derivation path is never trusted on its own.
- An output counts as change only if the device re-derives it and the script matches.
- The fee is computed from input and output amounts the device has verified, and shown.
- Signing requires the same plan that was reviewed: `core_sign` recomputes the SHA-256 of the
  reviewed plan and refuses if it differs. What you approved is what gets signed.

## What it does not check

- **It cannot tell you whether an address belongs to the person you think.** Verify the destination
  out of band.
- It does not know the current fee market. A reasonable-looking fee can still be wrong.
- It does not see the chain. It cannot know whether an input is already spent, or whether a
  `non_witness_utxo` is a real confirmed transaction.
- It does not implement anti-exfil (sign-to-contract). A malicious firmware could bias nonces;
  reproducible builds and the on-screen parser hash are the defence offered instead.

## Keys and memory

- The seed exists **only in RAM**, never in flash. Scanning it again after a power cycle is required.
- While unlocked, the master key and chain code are in RAM — they must be, to sign.
  **An attached debug probe (SWD) can read them.** Measured: see
  [docs/signing-architecture.md](signing-architecture.md). Disconnect the probe for real use.
- `Lock (wipe seed)` zeroes them; this was verified by dumping RAM before and after.
- The mnemonic, the BIP39 seed, the SeedQR payload, the camera frame and derived child keys are
  wiped after use, and signing nonces do not survive — all verified with `tools/ram_keys.py`.
- Entering the USB bootloader (BOOTSEL) clears SRAM entirely, so that path cannot read a leftover key.
- Nothing defends against an attacker who can glitch or probe the chip at will. Signatures are
  verified before they leave the device and the secp256k1 context is re-randomized per signing
  session, which raises the cost; it does not make the device tamper-resistant.

## QR and transport

- Animated QR uses UR (BCR-2020-005) fountain encoding; fragments are 100 bytes so each frame stays
  at QR version 8, which is what a phone camera can read from a 240x240 display.
- A PSBT whose previous transactions are large may not fit. Strip `non_witness_utxo` for segwit
  inputs (`tools/strip_psbt.py`); a faucet transaction with 2,323 outputs made a PSBT of 77KB,
  which is 770 QR frames.
- The device's quirc decoder needs the code to fill most of the frame and cannot read a display from
  across a room.
- A QR decoder is not a trust boundary. Whatever it produces is parsed and re-validated.

## Hardware

- One board is supported: Raspberry Pi Pico 2 (RP2350, RISC-V Hazard3). No other target is tested.
- There is no secure element, no tamper detection, no encrypted storage, and no battery-backed RAM.
- The boot ROM of the RP2350 is not open source.
- Builds with `TEST_SEED=1` can select a published test seed. Never put funds on such a build.
