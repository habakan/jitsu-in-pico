# How the system is put together

The current device architecture. Detailed checks and the reason for the parser boundary are in
[signing-architecture.md](signing-architecture.md).

## 1. The trust boundary

UR and PSBT data are parsed inside a WASM module that holds no keys. Camera frames are decoded by
native quirc, which remains in the trusted computing base. The keys and signing stay native, and the
parser and signer communicate through a fixed-length record.

```mermaid
flowchart TB
    subgraph untrusted["untrusted (an attacker chooses these bytes)"]
        qr["animated QR / SeedQR"]
        wallet["the wallet on a PC"]
    end

    subgraph device["the device (RP2350, no OS)"]
        subgraph sandbox["WASM sandbox (holds no keys)"]
            parser["parser.wasm<br/>UR reassembly, CBOR, PSBT parsing, building the Plan<br/>then taking signatures back and encoding a UR"]
        end

        subgraph tcb["the TCB (native)"]
            boundary["host ABI<br/>copies, with every address checked against the linear memory"]
            core["signing core<br/>re-check the Plan, BIP32 derivation, confirm ownership,<br/>build what is displayed, sighash, sign"]
            seedqr["SeedQR parsing<br/>verifies the BIP39 checksum"]
            ui["UI<br/>review screens, menu, drawing QRs"]
        end

        subgraph drivers["peripherals (native)"]
            cam["camera<br/>PIO + DMA"]
            quirc["quirc<br/>QR decoding"]
            lcd["ST7789 panel"]
            btn["buttons"]
        end
    end

    qr --> cam --> quirc
    quirc -->|"the QR's string"| boundary
    quirc -->|"SeedQR"| seedqr
    seedqr -->|"64-byte seed"| core
    boundary <--> parser
    boundary <-->|"Plan / signatures"| core
    core --> ui --> lcd
    btn --> ui
    ui -->|"approval"| core
    boundary -->|"the signed UR"| lcd
    lcd -.->|"read with a camera"| wallet
    wallet -.->|"unsigned PSBT"| qr
```

What the boundary is actually enforcing:

- `parser.wasm` has **no imports at all**. No clock, no allocation, no network
- The host checks every address and length `parser.wasm` returns with
  `wasm_runtime_validate_app_addr` before copying anything
- What crosses is one fixed 5,016-byte `plan_t`, with its layout nailed down by `_Static_assert`
- **The native side re-checks the Plan.** If the parser lies, key derivation and the ownership check
  happen independently on the native side anyway
- That what was shown is what gets signed is enforced by `core_sign` requiring the SHA-256 of the Plan
  to match the one `core_review` was given

## 2. One signing round

```mermaid
sequenceDiagram
    participant W as the wallet on a PC
    participant C as camera + quirc
    participant P as parser.wasm
    participant K as signing core
    participant L as panel + buttons

    Note over K: read a SeedQR and put the key in RAM (0.47s)
    W->>C: shows the unsigned PSBT as an animated QR
    loop until the parts are complete (measured: 7 of 8 sufficed)
        C->>P: one UR part
        P-->>L: progress, "3/8 parts"
    end
    P->>K: the Plan (fixed length) and the previous transactions
    Note over K: re-check, derive, confirm ownership (147ms)
    K->>L: five review screens: where, how much, the fee
    L->>K: approval, after every screen has been seen
    Note over K: sign (126ms for two inputs)
    K->>P: the signatures
    P->>L: the signed PSBT as UR parts
    L-->>W: an animated QR to read back
```

## 3. The same `.wasm`, verifiable anywhere

The parser runs as the identical bytes in six places. That isolates bugs that only reproduce on the
hardware, and it lets someone else verify the same artifact independently.

```mermaid
flowchart LR
    src["components/parts/parser/src/*.c<br/>jitsu-in"] --> wasm["parser.wasm<br/>15,570 bytes"]
    wasm --> mac["native on a Mac<br/>make check-psbt"]
    wasm --> qemu["QEMU RV32<br/>counts instructions"]
    wasm --> dev["RP2350 hardware<br/>WAMR interpreter"]
    wasm --> browser["a browser<br/>the single-file viewer"]
    wasm --> other["Kotlin / Swift<br/>components/parts/parser/hosts"]
    mac --> ref["checked against reference implementations<br/>embit / @ngraveio/bc-ur /<br/>Bitcoin Core's rpc_psbt.json"]
    qemu --> ref
    dev --> ref
    browser --> ref
    other --> ref
```

## 4. Memory: what the 520KB goes on

```mermaid
flowchart LR
    subgraph ram["RAM, 520KB"]
        pool["WAMR pool, 160KB<br/>parser.wasm's linear memory and UR reassembly"]
        heap["shared heap, 92KB<br/>quirc while reading, then the PSBT buffers"]
        stack["stack, 32KB"]
        other["UI, qrcodegen, core, secp256k1, pico-sdk: 58KB"]
        wasmcopy["parser.wasm's copy in RAM, 16KB"]
        free["free, 162KB"]
    end
```

- **quirc (90KB) and the PSBT buffers (66KB) use the same heap one after the other.** Once reading is
  done quirc is not needed, and before parsing the PSBT buffers are not needed. Measured on the
  hardware, the peak stays at 93,696 B; without sharing it would be 159,232 B
- AOT makes `parser.wasm` faster but has to be expanded into RAM — executing in place measured seven
  times slower — which pushes the pool to 277KB and no longer leaves room for the camera. With the
  parser this light, a full round only changes by 8%, so **the interpreter stays**

## 5. Measured (Pico 2 H, 150MHz)

| step | where it runs | time |
|---|---|---|
| SeedQR to seed (PBKDF2, 2048 rounds) | native | 0.47s |
| capturing one QR frame | PIO + DMA | 120ms |
| decoding the QR | quirc | 62ms, or 300ms when it finds one |
| parsing the PSBT | **parser.wasm** | 25ms |
| checking it and building the display | native | 147ms |
| signing (two inputs, ECDSA + Schnorr) | native | 126ms |
| taking the signatures back | **parser.wasm** | 0.8ms |
| UR encoding and building the QR | wasm + native | 95ms per part |
