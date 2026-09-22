# baremetal-wasm-signer

Raspberry Pi Pico 2 (RP2350 / RISC-V) 上で動く、署名ロジックを WASM で分離したベアメタル Bitcoin 署名器。

設計: [docs/design.md](docs/design.md)
メモリ・速度の実現性検証: [docs/feasibility.md](docs/feasibility.md)
QR 読み書きの実現性検証: [docs/qr-feasibility.md](docs/qr-feasibility.md)
BIP39 / BIP32 の速度検証: [docs/kdf-feasibility.md](docs/kdf-feasibility.md)
