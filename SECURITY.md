# Security policy

## Reporting a vulnerability

**Please do not report security vulnerabilities through public GitHub issues.**

Use GitHub's private vulnerability reporting on this repository
([Security → Report a vulnerability](https://github.com/habakan/jitsu-in-pico/security/advisories/new)),
or email the maintainer. Public key: https://github.com/habakan.gpg

Fingerprint: `8BD4 8DD6 70AF 9B34 7EA0  41CF 36D4 93A2 8A8B EB79`

We aim to acknowledge a report within one week and to publish a fix within 90 days.
If you do not hear back within a week, please escalate by opening a public issue that says only
that you are waiting for a response — with no details of the problem.

### What to include

- a description of the vulnerability and how it could be exploited
- its potential impact (for example: theft of funds, leaking a key or an xpub, wrong information
  shown on the review screen, denial of service)
- steps, an input, or code that reproduces it — a PSBT or a QR payload is ideal
- which surface it affects: the device firmware, its UI and camera code, or the host tools
- a proposed patch, if you have one

**Never include private keys, recovery phrases, or personally identifiable information** in a report.
Stack traces, memory dumps and exploit scripts should be scrubbed first. If a report needs a key to
reproduce, generate a throwaway one and say so.

## Scope

This policy covers this repository and our quirc fork
([`mcu` branch](https://github.com/habakan/quirc)), which the firmware uses to read QR codes.

Problems in `parser.wasm`, `signer.wasm`, their host libraries or the browser viewer belong to
[jitsu-in](https://github.com/habakan/jitsu-in/security/policy). If you are not sure which one is
affected, report it here.

## What is already known

This project has **not been reviewed by a third party** and is not ready for real funds; see
[Disclaimer](README.md#disclaimer). The following are known and documented, so they are not
findings — but a concrete exploit of one of them still is:

- An attached debug probe (SWD) can read the master key out of RAM while a seed is loaded.
  Measured and documented in [docs/architecture-b.md](docs/architecture-b.md).
- Multisig and BIP39 passphrase input are planned but not supported by the current firmware. PSBT v2
  is also unsupported. The firmware rejects unsupported input rather than handling it.
