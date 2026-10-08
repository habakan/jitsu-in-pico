## What this changes, and why

<!-- One or two sentences. The diff says what; say why. -->

## Evidence

<!-- Tick what you actually ran. Delete rows that do not apply. -->

- [ ] `make check-core`
- [ ] `make check-psbt` (PSBT round trip, signatures verified with embit)
- [ ] `make check-xpub` / `check-ui` / `check-seedqr`
- [ ] `make -C components/parts/parser test` (529 vectors)
- [ ] `make -C components/parts/parser check-fuzz`
- [ ] `make check-repro` still passes

Tested on:

- [ ] real hardware (say which build: `TEST_SEED=`, `TESTNET=`)
- [ ] QEMU
- [ ] host only

## If this changes a `.wasm`

- [ ] `checksums.txt` is updated in this PR, and the change is intentional
- [ ] the new hash was produced by `make check-repro`, not by a local toolchain

## If this changes a screen

<!-- Attach a photo or the `app_nolcd` UART output showing the new screen. -->

## For anything near signing or parsing

- [ ] I added a test, and I checked it catches the bug by breaking the code on purpose
- [ ] I read docs/limitations.md and this does not quietly widen what is accepted
