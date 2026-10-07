# The signer module validates, displays, and signs a plan_t; it pairs with parser.wasm.
# The device runs the same core.c natively so keys stay on the native side.
SIGNER_WASM_SRC := components/parts/signer/wasm_main.c components/parts/signer/core.c components/parts/signer/address.c \
  components/parts/signer/bip32.c components/parts/signer/sighash.c components/parts/signer/ripemd160.c \
  components/parts/signer/sha512.c components/parts/signer/secp_callbacks.c components/parts/signer/secp256k1_unity.c \
  components/parts/parser/c/src/tx.c components/parts/parser/c/src/sha256.c

build/signer.wasm: $(SIGNER_WASM_SRC) components/parts/signer/*.h components/parts/parser/c/include/*.h
	mkdir -p build
	$(LLVM)/clang --target=wasm32-wasip1 --sysroot=$(WASI) -nostartfiles -nodefaultlibs \
	  -Oz -Wall -Wno-unused-function -DNDEBUG $(LIME_FLAGS) -Icomponents/parts/signer -Icomponents/parts/parser/c/include \
	  -I$(SECP)/include -I$(SECP)/src $(SECP_DEFS) \
	  -Wl,--no-entry -Wl,--gc-sections -Wl,--strip-all -Wl,-z,stack-size=16384 \
	  -Wl,--export=__heap_base -Wl,--export=__data_end \
	  -Wl,--initial-memory=196608 -Wl,--no-growable-memory \
	  --no-wasm-opt -Wl,--keep-section=target_features \
	  -o $@ $(SIGNER_WASM_SRC) -lc $(RTLIB)/libclang_rt.builtins.a
	$(WASM_OPT) $@ -Oz -o $@
	@shasum -a 256 $@

clean:
	rm -rf build
.PHONY: clean

WAMRC   := build/wamrc/wamrc
$(WAMRC):
	cmake -S third_party/wasm-micro-runtime/wamr-compiler -B build/wamrc -G Ninja -DCMAKE_BUILD_TYPE=Release \
	  -DWAMR_BUILD_WITH_CUSTOM_LLVM=1 -DLLVM_DIR=/opt/homebrew/opt/llvm@18/lib/cmake/llvm >/dev/null
	ninja -C build/wamrc >/dev/null


# Build parser.wasm with the jitsu-in submodule's Makefile.
build/parser.wasm: components/parts/parser/c/src/*.c components/parts/parser/c/include/*.h
	mkdir -p build
	$(MAKE) -C components/parts/parser build/parser.wasm LLVM=$(LLVM) WASI=$(WASI) RTLIB=$(RTLIB) WASM_OPT=$(WASM_OPT)
	cp components/parts/parser/build/parser.wasm $@

# Rebuild the WASM artifacts used by the device with pinned tools and compare their hashes.
SDK = $(shell ./tools/build/toolchain.sh)
REPRO_WASM := build/parser.wasm build/signer.wasm

# Check that the distributable WASM modules have the expected shape; requires wasm-tools.
check-wasm: $(REPRO_WASM)
	uv run -q tests/host/check_wasm.py $(REPRO_WASM)
.PHONY: check-wasm

check-repro: tools/build/toolchain.sh checksums.txt
	rm -f $(REPRO_WASM) components/parts/parser/build/parser.wasm
	$(MAKE) $(REPRO_WASM) \
	  LLVM=$(CURDIR)/$(SDK)/bin WASI=$(CURDIR)/$(SDK)/share/wasi-sysroot \
	  RTLIB=$(CURDIR)/$(SDK)/lib/clang/23/lib/wasm32-unknown-wasi \
	  WASM_OPT=$(CURDIR)/build/toolchain/binaryen-version_132/bin/wasm-opt
	@(shasum -a 256 $(REPRO_WASM) 2>/dev/null || sha256sum $(REPRO_WASM)) > /tmp/repro.txt
	@diff /tmp/repro.txt checksums.txt && echo "すべて一致した（再現可能）" \
	  || { echo "一致しない。docs/reproducible-build.md を見る"; exit 1; }
.PHONY: check-repro

check-parser:
	$(MAKE) -C components/parts/parser test
.PHONY: check-parser

# PARSER_AOT=1 compiles the parser to RV32 with wamrc; on hardware, XIP is 7x slower, so use RAM only.
PARSER_AOT ?= 0
WAMRC_RAM_FLAGS := --target=riscv32 --target-abi=ilp32 --cpu=generic-rv32 --cpu-features=+m,+a,+c,+zba,+zbb,+zbs \
  --bounds-checks=1
PARSER_BIN := build/parser.$(if $(filter 1,$(PARSER_AOT)),aot,wasm)

build/parser.aot: build/parser.wasm $(WAMRC)
	$(WAMRC) $(WAMRC_RAM_FLAGS) -o $@ $< >/dev/null

build/parser_wasm.h: $(PARSER_BIN)
	xxd -i -n parser_wasm $< | sed 's/^unsigned char/const unsigned char/' > $@
