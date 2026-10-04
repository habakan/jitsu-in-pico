# Security policy

## Reporting a vulnerability

**Please do not report security vulnerabilities through public GitHub issues.**

Use GitHub's private vulnerability reporting on this repository
([Security → Report a vulnerability](https://github.com/habakan/baremetal-wasm-signer/security/advisories/new)),
or email the maintainer. Public key: https://github.com/habakan.gpg

We aim to acknowledge a report within one week and to publish a fix within 90 days.
If you do not hear back within a week, please escalate by opening a public issue that says only
that you are waiting for a response — with no details of the problem.

### What to include

- a description of the vulnerability and how it could be exploited
- its potential impact (for example: theft of funds, leaking a key or an xpub, wrong information
  shown on the review screen, denial of service)
- steps, an input, or code that reproduces it — a PSBT or a QR payload is ideal
- which surface it affects: the device firmware, `parser.wasm`, the browser viewer, or the host tools
- a proposed patch, if you have one

**Never include private keys, recovery phrases, or personally identifiable information** in a report.
Stack traces, memory dumps and exploit scripts should be scrubbed first. If a report needs a key to
reproduce, generate a throwaway one and say so.

## Scope

This policy covers this repository and the components it pulls in as submodules:

- [wasm-psbt-parser](https://github.com/habakan/wasm-psbt-parser) — the PSBT and UR parser
- [quirc (`mcu` branch)](https://github.com/habakan/quirc) — our fork of the QR decoder

Report problems in any of them here.

## What is already known

This project has **not been reviewed by a third party** and is not ready for real funds; see
[Disclaimer](README.md#disclaimer). The following are known and documented, so they are not
findings — but a concrete exploit of one of them still is:

- An attached debug probe (SWD) can read the master key out of RAM while a seed is loaded.
  Measured and documented in [docs/architecture-b.md](docs/architecture-b.md).
- Multisig, passphrases and PSBT v2 are not implemented. Unsupported input is rejected, not handled.
- The browser viewer is a display and transport tool. It holds no keys, and what it shows is not
  authoritative — the device screen is.
