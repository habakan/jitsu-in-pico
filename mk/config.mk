LLVM    ?= /opt/homebrew/opt/llvm/bin
WASI    ?= /opt/homebrew/opt/wasi-libc/share/wasi-sysroot
SECP    := third_party/secp256k1
RTLIB   ?= /opt/homebrew/opt/wasi-runtimes/share/wasi-runtimes/lib/wasm32-unknown-wasip1
WASM_OPT ?= wasm-opt
RISCV_TC ?= $(CURDIR)/third_party/riscv-toolchain

# COMB settings and ecmult_gen table sizes: 2,5=2KB / 11,6=22KB / 43,6=86KB; WASM loads this data segment into RAM
COMB    ?= -DCOMB_BLOCKS=2 -DCOMB_TEETH=5
SECP_DEFS := -DENABLE_MODULE_EXTRAKEYS=1 -DENABLE_MODULE_SCHNORRSIG=1 -DENABLE_MODULE_RECOVERY=1 -DECMULT_WINDOW_SIZE=2 \
             -DUSE_EXTERNAL_DEFAULT_CALLBACKS=1 $(COMB)

# Lime1 is WebAssembly 1.0 plus seven phase-5 features, defined in WebAssembly/tool-conventions/Lime.md.
# Passing it to the linker rejects dependencies that require SIMD or threads.
LIME1 := mutable-globals,multivalue,sign-ext,nontrapping-fptoint,bulk-memory-opt,extended-const,call-indirect-overlong
LIME_FLAGS := -mcpu=lime1 -Xlinker --features=$(LIME1)

# Default to the fixed-point fork on the mcu submodule branch.
# To compare with upstream, pass QUIRC=third_party/quirc/lib QUIRC_DEFS=.
QUIRC       ?= components/qr/quirc/lib
QUIRC_DEFS  ?= -DQUIRC_FIXED_POINT_FITNESS -DQUIRC_FLOAT_TYPE=float -DQUIRC_USE_TGMATH
QRGEN       := third_party/QR-Code-generator/c

# tx.c and sha256.c are shared with the jitsu-in submodule.
CORE_SRC := components/parts/signer/core.c components/parts/signer/address.c components/parts/signer/bip32.c components/parts/signer/sighash.c components/parts/parser/c/src/tx.c components/parts/parser/c/src/sha256.c components/parts/signer/ripemd160.c components/parts/signer/sha512.c \
            components/parts/signer/secp_callbacks.c components/parts/signer/seedqr.c components/parts/signer/bip85.c
