# Contributing

## Before you start

Read [README.md](README.md) for what this is, [docs/limitations.md](docs/limitations.md) for where
the boundaries are, and [components/parts/parser/docs/abi.md](components/parts/parser/docs/abi.md) if you are
touching the parser or writing a host for it.

**Security problems do not go in issues.** See [SECURITY.md](SECURITY.md).

## Language

- **Issues, pull requests and code review: English.**
- This repository's commit messages and `docs/` are **Japanese**; `README.md` and
  `docs/limitations.md` are English. Translations of existing Japanese docs are welcome.
- The [wasm-bitcoin-signer](https://github.com/habakan/wasm-bitcoin-signer) submodule is **English
  throughout**, including its commits.

Write a patch in whichever of the two you are comfortable with; the maintainer will not reject a
change over language.

## Workflow

1. **Branch.** Prefix with `fix/`, `feat/`, `refactor/` or `docs/`.
2. **Commit.** Subject line plus at most one body line. The body says *why*, not *what* — the diff
   already says what. Stage the files you changed; do not `git add .`.
3. **Test.** Run the `make check-*` targets that cover what you touched (see
   [README.md](README.md#check-it-yourself)). `make check-core` and `make check-psbt` are the
   two that matter most for anything near signing.
4. **Open a pull request** against `main` with the PR template filled in.

If a change alters any `.wasm`, `checksums.txt` changes too. Include it in the same commit and say so
in the PR — a reviewer needs to see that the hash moved on purpose.

## Conventions

- **Comments: at most two lines per block.** Keep only what you cannot get from reading the code —
  why a value is what it is, what breaks otherwise. No restating the next line, no history of what
  the code used to be. Public API documentation is the exception.
- **Simple over clever.** Extract a helper when the same thing appears three times, not before.
- Match the surrounding code's style rather than introducing your own.
- New tests must be checked with mutation testing: break the thing on purpose and confirm the test
  fails. A test that passes against a broken implementation is worse than no test.

## Testing

`make check-core` `check-xpub` `check-psbt` `check-ui` `check-seedqr` `check-repro`,
`make -C components/parts/parser test` and `check-fuzz`. The full list with one line each is in
[README.md](README.md#check-it-yourself). Expected values come from independent implementations
(embit, Bitcoin Core, `@ngraveio/bc-ur`, zxing-cpp), not from this code.

Hardware changes need a real board. Say in the PR what you tested on: device, QEMU, browser, or host
only.

## AI assistance

Parts of this repository were written with AI assistance. The rule here is the same one Bitcoin Core
states: **there has to be a human author in the loop who understands the work and is responsible for
it.** If you used a tool, you still own the patch — be ready to explain any line of it, and do not
open a PR you have not read. Review comments should be written by a person.
