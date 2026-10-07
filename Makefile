LLVM    ?= /opt/homebrew/opt/llvm/bin
WASI    ?= /opt/homebrew/opt/wasi-libc/share/wasi-sysroot
SECP    := third_party/secp256k1
C_SRC_FILES := $(shell git ls-files '*.c')
C_FORMAT_FILES := $(C_SRC_FILES) $(shell git ls-files '*.h')
C_TIDY_FILES := $(addprefix components/parts/,$(shell git -C components/parts ls-files 'parser/c/src/*.c'))
# COMB settings and ecmult_gen table sizes: 2,5=2KB / 11,6=22KB / 43,6=86KB; WASM loads this data segment into RAM
COMB    ?= -DCOMB_BLOCKS=2 -DCOMB_TEETH=5
SECP_DEFS := -DENABLE_MODULE_EXTRAKEYS=1 -DENABLE_MODULE_SCHNORRSIG=1 -DECMULT_WINDOW_SIZE=2 \
             -DUSE_EXTERNAL_DEFAULT_CALLBACKS=1 $(COMB)
RTLIB   ?= /opt/homebrew/opt/wasi-runtimes/share/wasi-runtimes/lib/wasm32-unknown-wasip1
# The clang driver silently runs wasm-opt from PATH; pin its version and invoke it explicitly
WASM_OPT ?= wasm-opt
# Lime1 is WebAssembly 1.0 plus seven phase-5 features, defined in WebAssembly/tool-conventions/Lime.md.
# Passing it to the linker rejects dependencies that require SIMD or threads.
LIME1 := mutable-globals,multivalue,sign-ext,nontrapping-fptoint,bulk-memory-opt,extended-const,call-indirect-overlong
LIME_FLAGS := -mcpu=lime1 -Xlinker --features=$(LIME1)

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

RISCV_TC_REL := https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.3.1-0
ifeq ($(shell uname -s),Darwin)
RISCV_TC_PKG := riscv-toolchain-16-mac.zip
RISCV_TC_SUM := 186538414857e31012aaa41f5f7ecfdee5bbd42fe3f4ec047232ac06d35a24c1
else
RISCV_TC_PKG := riscv-toolchain-16-x86_64-lin.tar.gz
RISCV_TC_SUM := fc36f1b37f99de54a358115443638023de34e15fe199e718e50cca4d212e7e9f
endif

# Pin every dependency to a commit, especially code that handles keys; never track the tip of a branch.
# Review the diff before updating a pin.
SECP_REV   := 46db787112beabdb5e17e0dc35680716f1057e7b
WAMR_REV   := f5f57c09aee623436f5fb87a90798fdd2cdf39fd
PICO_REV   := 079c6f39023649b154152db30f1d781e884879bc
QUIRC_REV  := 927d680904dc95fdff4cd9d022eb374b438ff8f2
QRGEN_REV  := 3c6d0b3cefb4e049dc337e82237c9644399716a8
SPLEEN_REV := 57f9219328c9f5873085320fe8bc8f7dd34b8791

# $(1) destination, $(2) URL, $(3) commit
define clone_at
	git clone --filter=blob:none $(2) third_party/$(1)
	cd third_party/$(1) && git checkout --detach $(3)
endef

deps:
	mkdir -p third_party
	$(call clone_at,secp256k1,https://github.com/bitcoin-core/secp256k1.git,$(SECP_REV))
	$(call clone_at,wasm-micro-runtime,https://github.com/bytecodealliance/wasm-micro-runtime.git,$(WAMR_REV))
	$(call clone_at,pico-sdk,https://github.com/raspberrypi/pico-sdk.git,$(PICO_REV))
	$(call clone_at,quirc,https://github.com/dlbeer/quirc.git,$(QUIRC_REV))
	$(call clone_at,QR-Code-generator,https://github.com/nayuki/QR-Code-generator.git,$(QRGEN_REV))
	$(call clone_at,spleen,https://github.com/fcambus/spleen.git,$(SPLEEN_REV))
	cd third_party/pico-sdk && git submodule update --init --depth 1 lib/tinyusb 2>/dev/null || true
	curl -sL -o third_party/$(RISCV_TC_PKG) $(RISCV_TC_REL)/$(RISCV_TC_PKG)
	cd third_party && echo "$(RISCV_TC_SUM)  $(RISCV_TC_PKG)" | (shasum -a 256 -c 2>/dev/null || sha256sum -c)
	mkdir -p third_party/riscv-toolchain && cd third_party && case $(RISCV_TC_PKG) in \
	  *.zip) unzip -q $(RISCV_TC_PKG) -d riscv-toolchain ;; *) tar xzf $(RISCV_TC_PKG) -C riscv-toolchain ;; esac
	rm third_party/$(RISCV_TC_PKG)

# Dependencies for CI host checks; excludes hardware and QEMU requirements
deps-host:
	mkdir -p third_party
	$(call clone_at,secp256k1,https://github.com/bitcoin-core/secp256k1.git,$(SECP_REV))
	$(call clone_at,wasm-micro-runtime,https://github.com/bytecodealliance/wasm-micro-runtime.git,$(WAMR_REV))
	$(call clone_at,QR-Code-generator,https://github.com/nayuki/QR-Code-generator.git,$(QRGEN_REV))
	$(call clone_at,spleen,https://github.com/fcambus/spleen.git,$(SPLEEN_REV))
.PHONY: deps-host

# Check that each available third_party dependency matches its pinned commit
check-deps:
	@for d in secp256k1:$(SECP_REV) wasm-micro-runtime:$(WAMR_REV) pico-sdk:$(PICO_REV) \
	          quirc:$(QUIRC_REV) QR-Code-generator:$(QRGEN_REV) spleen:$(SPLEEN_REV); do \
	  n=$${d%%:*}; want=$${d#*:}; \
	  if [ ! -d third_party/$$n ]; then echo "$$n: 未取得（飛ばす）"; continue; fi; \
	  got=$$(git -C third_party/$$n rev-parse HEAD 2>/dev/null); \
	  if [ "$$got" != "$$want" ]; then echo "$$n: $$got != $$want"; exit 1; fi; done
	@echo "取得済みの third_party はすべて固定した commit"
.PHONY: check-deps

.PHONY: deps

WAMRC   := build/wamrc/wamrc
$(WAMRC):
	cmake -S third_party/wasm-micro-runtime/wamr-compiler -B build/wamrc -G Ninja -DCMAKE_BUILD_TYPE=Release \
	  -DWAMR_BUILD_WITH_CUSTOM_LLVM=1 -DLLVM_DIR=/opt/homebrew/opt/llvm@18/lib/cmake/llvm >/dev/null
	ninja -C build/wamrc >/dev/null

RISCV_TC ?= $(CURDIR)/third_party/riscv-toolchain

PARSER_POOL_KB ?= 256
TESTNET ?= 0
# Allow selecting the test seed; keep this at 0 for production builds.
TEST_SEED ?= 0
QEMU_CPU := rv32,f=off,d=off,zfa=off,zba=on,zbb=on,zbs=on,zbkb=on,zcb=on,zcmp=on,zcmt=off

# Default to the fixed-point fork on the mcu submodule branch.
# To compare with upstream, pass QUIRC=third_party/quirc/lib QUIRC_DEFS=.
QUIRC   ?= components/qr/quirc/lib
QUIRC_DEFS ?= -DQUIRC_FIXED_POINT_FITNESS -DQUIRC_FLOAT_TYPE=float -DQUIRC_USE_TGMATH
QRGEN   := third_party/QR-Code-generator/c
build/qr_frames.h: tools/generate/gen_qr_frames.py
	mkdir -p build && uv run -q $< $@

# Use only standard extensions: -O2 may emit Hazard3-specific Xh3bextm instructions that QEMU cannot run.
QEMU_MARCH := -march=rv32imac_zicsr_zifencei_zba_zbb_zbs_zbkb_zcb_zcmp -mabi=ilp32
build/qemu-qr.elf: tests/host/qr_bench.c build/qr_frames.h tests/qemu/start.S $(QUIRC)/identify.c
	$(RISCV_TC)/bin/riscv32-pico-elf-gcc $(QEMU_MARCH) -O2 -DQEMU_BUILD=1 -Wall \
	  $(QUIRC_DEFS) -I$(QUIRC) -I$(QRGEN) -Ibuild --specs=semihost.specs -Wl,--section-start=.qemu_start=0x80000000 \
	  -Wl,-Ttext=0x80001000 -Wl,-e,qemu_start -Wl,--gc-sections -o $@ \
	  tests/host/qr_bench.c $(wildcard $(QUIRC)/*.c) $(QRGEN)/qrcodegen.c tests/qemu/start.S -lm

check-qemu-qr: build/qemu-qr.elf
	qemu-system-riscv32 -M virt -cpu $(QEMU_CPU) -m 64M -nographic -bios none -semihosting -icount shift=0 \
	  -kernel $< </dev/null
.PHONY: check-qemu-qr

# Compare quirc and zxing-cpp decoding on macOS; check-qemu-qr measures instruction counts.
build/qr_bench_mac: tests/host/qr_bench.c build/qr_frames.h $(QUIRC)/identify.c
	cc -O2 -Wall $(QUIRC_DEFS) -I$(QUIRC) -I$(QRGEN) -Ibuild -o $@ tests/host/qr_bench.c $(QUIRC)/*.c $(QRGEN)/qrcodegen.c

check-qr-mac: build/qr_bench_mac
	build/qr_bench_mac | awk '{print $$1, $$4}'
	uv run -q tests/host/zxing_check.py build/qr_frames | sed 's/^/zxing /'
.PHONY: check-qr-mac

# Native signing core for architecture B (docs/architecture-b.md).
# tx.c and sha256.c are shared with the jitsu-in submodule.
CORE_SRC := components/parts/signer/core.c components/parts/signer/address.c components/parts/signer/bip32.c components/parts/signer/sighash.c components/parts/parser/c/src/tx.c components/parts/parser/c/src/sha256.c components/parts/signer/ripemd160.c components/parts/signer/sha512.c \
            components/parts/signer/secp_callbacks.c
build/core_vectors.h: tools/generate/gen_core_vectors.py test-vectors/bip341-wallet-test-vectors.json
	mkdir -p build && uv run -q $< test-vectors/bip341-wallet-test-vectors.json $@

build/test_core: components/parts/signer/tests/test_core.c $(CORE_SRC) components/parts/signer/*.h components/parts/parser/c/include/*.h build/core_vectors.h components/parts/signer/secp256k1_unity.c
	cc -O2 -Wall -Wextra -Wno-unused-function -Icomponents/parts/signer -Icomponents/parts/parser/c/include -Ibuild -I$(SECP)/include -I$(SECP)/src $(SECP_DEFS) \
	  -o $@ components/parts/signer/tests/test_core.c $(CORE_SRC) components/parts/signer/secp256k1_unity.c

check-core: build/test_core
	build/test_core
.PHONY: check-core

format-c:
	$(LLVM)/clang-format -i $(C_FORMAT_FILES)
.PHONY: format-c

check-c-format:
	$(LLVM)/clang-format --dry-run --Werror $(C_FORMAT_FILES)
.PHONY: check-c-format

check-c-tidy:
	$(LLVM)/clang-tidy $(C_TIDY_FILES) -- -std=c11 -Icomponents/parts/parser/c/include \
	  --target=wasm32-wasip1 --sysroot=$(WASI)
.PHONY: check-c-tidy

build/test_xpub: components/parts/signer/tests/test_xpub.c $(CORE_SRC) components/parts/signer/*.h components/parts/signer/secp256k1_unity.c
	cc -O2 -Wall -Wextra -Wno-unused-function -Icomponents/parts/signer -Icomponents/parts/parser/c/include -I$(SECP)/include -I$(SECP)/src $(SECP_DEFS) \
	  -o $@ components/parts/signer/tests/test_xpub.c $(CORE_SRC) components/parts/signer/secp256k1_unity.c

check-xpub: build/test_xpub
	build/test_xpub
.PHONY: check-xpub

build/qemu-test-core.elf: components/parts/signer/tests/test_core.c $(CORE_SRC) components/parts/signer/*.h components/parts/parser/c/include/*.h build/core_vectors.h tests/qemu/start.S
	$(RISCV_TC)/bin/riscv32-pico-elf-gcc $(QEMU_MARCH) -O2 -Wall -Wno-unused-function -Icomponents/parts/signer -Icomponents/parts/parser/c/include -Ibuild \
	  -I$(SECP)/include -I$(SECP)/src $(SECP_DEFS) --specs=semihost.specs -Wl,--section-start=.qemu_start=0x80000000 \
	  -Wl,-Ttext=0x80001000 -Wl,-e,qemu_start -Wl,--gc-sections -o $@ \
	  components/parts/signer/tests/test_core.c $(CORE_SRC) components/parts/signer/secp256k1_unity.c tests/qemu/start.S

check-qemu-core: build/qemu-test-core.elf
	qemu-system-riscv32 -M virt -cpu $(QEMU_CPU) -m 64M -nographic -bios none -semihosting \
	  -kernel $< </dev/null
.PHONY: check-qemu-core

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

build/psbt/own_p2wpkh_1in.psbt: tools/generate/gen_psbt_vectors.py
	rm -rf build/psbt && uv run -q $< build/psbt

build/font8x16.h: tools/generate/gen_font.py
	mkdir -p build && python3 $< third_party/spleen/spleen-8x16.bdf $@

build/host-classic/psbt_host: build/parser_wasm.h build/font8x16.h tests/host/psbt_main.c tests/host/CMakeLists.txt \
  src/runtime/parser_host.c src/ui/ui.c $(CORE_SRC) components/parts/parser/c/include/*.h
	cmake -S tests/host -B build/host-classic -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DWAMR_BUILD_FAST_INTERP=0 \
	  -DSIGNER_WASM_H_DIR=$(CURDIR)/build >/dev/null
	ninja -C build/host-classic psbt_host

# Write 60-byte mixed-PSBT UR fragments from the parser's reference-encoder vectors, one part per line.
# Drop every third original part and verify recovery from the mixed set.
build/psbt/own_mixed_nwu.ur: components/parts/parser/tests/ur_vectors.json build/psbt/own_p2wpkh_1in.psbt
	python3 -c "import json,re; v=[x for x in json.load(open('$<'))['vectors'] if x['name']=='own_mixed_nwu' and x['fragment_len']==60][0]; \
	  print('\n'.join(p for p in v['parts'] if not (int(re.match(r'UR:[A-Z-]+/(\d+)', p).group(1)) <= v['seq_len'] and int(re.match(r'UR:[A-Z-]+/(\d+)', p).group(1)) % 3 == 0)))" > $@

# check-layout, the host libraries and the parser's vectors all live in the parts repository now;
# running them here would duplicate them with the wrong paths. What this repository checks is that
# the device and the browser agree with those parts

# Independent hosts driving the same module have to agree byte for byte. If they do not, one of them
# is reading the layout wrong, which no single-host test would catch: a lone host's tests pass just
# as happily when the library and its expectations are wrong together.
# One shell for the whole recipe, so a guard can actually skip the rest. REQUIRE_KOTLIN=1 and
# REQUIRE_SWIFT=1 turn a missing tool into a failure, because a check that silently succeeds without
# its tools is worse than no check. CI passes REQUIRE_KOTLIN=1; it cannot require Swift, because
# WasmKit needs Swift 6.3 or newer and the runners do not have it
check-hosts-agree: build/signer.wasm build/parser.wasm build/psbt/own_mixed_nwu.psbt
	@set -e; \
	node components/parts/signer/hosts/js/dump.mjs \
	  build/signer.wasm build/parser.wasm build/psbt/own_mixed_nwu.psbt > build/host-js.out; \
	if command -v kotlinc >/dev/null; then \
	  $(MAKE) -s -C components/parts/signer/hosts/kotlin dump.jar; \
	  $(MAKE) -s -C components/parts/signer/hosts/kotlin dump \
	    SIGNER=$(PWD)/build/signer.wasm PARSER=$(PWD)/build/parser.wasm \
	    PSBT=$(PWD)/build/psbt/own_mixed_nwu.psbt > build/host-kotlin.out; \
	  diff build/host-js.out build/host-kotlin.out && echo "JavaScript and Kotlin agree"; \
	elif [ "$(REQUIRE_KOTLIN)" = "1" ]; then echo "kotlinc not found and REQUIRE_KOTLIN=1"; exit 1; \
	else echo "kotlinc not found; skipping the Kotlin host"; fi; \
	if command -v swift >/dev/null; then \
	  $(MAKE) -s -C components/parts/signer/hosts/swift dump \
	    SIGNER=$(PWD)/build/signer.wasm PARSER=$(PWD)/build/parser.wasm \
	    PSBT=$(PWD)/build/psbt/own_mixed_nwu.psbt 2>/dev/null \
	    | grep -vE '^Building|^Build complete|^\[' > build/host-swift.out; \
	  diff build/host-js.out build/host-swift.out && echo "JavaScript and Swift agree"; \
	elif [ "$(REQUIRE_SWIFT)" = "1" ]; then echo "swift not found and REQUIRE_SWIFT=1"; exit 1; \
	else echo "swift not found; skipping the Swift host"; fi
.PHONY: check-hosts-agree

# The host libraries are tested in the parts repository, which is where they live and where their
# own build/ sits. Running them from here would just duplicate it with the wrong paths; what this
# repository checks is that the device and the browser agree with them (check-psbt, check-core-diff)

# Bitcoin Core as the oracle. Core decides what a PSBT means, so agreeing with it is worth more than
# agreeing with our own expectations. Needs bitcoind and bitcoin-cli on PATH.
# Its own port, so a regtest node already running on the default one does not get in the way
BTCDIR ?= build/btcregtest
BTCPORT ?= 18999
BTCCLI = bitcoin-cli -datadir=$(PWD)/$(BTCDIR) -regtest -rpcport=$(BTCPORT)
check-core-diff: build/host-classic/psbt_host build/psbt/own_p2wpkh_1in.psbt
	@command -v bitcoind >/dev/null || { echo "bitcoind not found (brew install bitcoin)"; exit 1; }
	@rm -rf $(BTCDIR) && mkdir -p $(BTCDIR)
	@bitcoind -regtest -datadir=$(PWD)/$(BTCDIR) -rpcport=$(BTCPORT) -daemon -fallbackfee=0.0001
	@for i in 1 2 3 4 5 6 7 8 9 10; do \
	  $(BTCCLI) getblockchaininfo >/dev/null 2>&1 && break; sleep 1; done
	@$(BTCCLI) getblockchaininfo >/dev/null || { echo "regtest node did not come up"; exit 1; }
	@rc=0; \
	uv run -q --with embit tests/host/check_against_core.py "$(BTCCLI)" ./build/host-classic/psbt_host \
	  components/parts/parser/tests/rpc_psbt.json build/psbt/*.psbt || rc=1; \
	uv run -q --with embit tests/host/check_sigs_against_core.py "$(BTCCLI)" ./build/host-classic/psbt_host \
	  build/psbt/own_*.psbt || rc=1; \
	$(BTCCLI) stop >/dev/null 2>&1 || true; \
	exit $$rc
.PHONY: check-core-diff

check-psbt: build/host-classic/psbt_host build/psbt/own_p2wpkh_1in.psbt build/psbt/own_mixed_nwu.ur
	rm -f build/psbt/*.signed
	for f in build/psbt/own_*.psbt; do echo "== $$f"; build/host-classic/psbt_host sign $$f $${f%.psbt}.signed || true; done
	build/host-classic/psbt_host sign build/psbt/own_mixed_nwu.ur build/psbt/own_mixed_nwu_ur.out build/psbt/qr
	cmp build/psbt/own_mixed_nwu_ur.out build/psbt/own_mixed_nwu.signed && echo "UR path matches the binary PSBT path"
	# Decoding the signed-PSBT UR, including mixed fragments, must recover the signed PSBT byte for byte.
	build/host-classic/psbt_host ur2bin build/psbt/own_mixed_nwu_ur.out.ur build/psbt/roundtrip.out
	cmp build/psbt/roundtrip.out build/psbt/own_mixed_nwu.signed && echo "signed PSBT survives the UR round trip"
	uv run -q tests/host/check_qr_screen.py build/psbt/qr build/psbt/own_mixed_nwu_ur.out.ur
	uv run -q tests/host/check_signed_psbt.py build/psbt
.PHONY: check-psbt

check-e2e-wasm: build/signer.wasm build/parser.wasm build/psbt/own_p2wpkh_1in.psbt
	node tests/host/e2e_wasm.mjs
.PHONY: check-e2e-wasm

check-qemu-psbt: build/parser_wasm.h build/font8x16.h build/psbt/own_mixed_nwu.ur
	cmake -S tests/qemu -B build/qemu-psbt -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
	  -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
	  -DCMAKE_C_COMPILER=$(RISCV_TC)/bin/riscv32-pico-elf-gcc -DCMAKE_ASM_COMPILER=$(RISCV_TC)/bin/riscv32-pico-elf-gcc \
	  -DPOOL_KB=64 -DSIGNER_WASM_H_DIR=$(CURDIR)/build >/dev/null
	ninja -C build/qemu-psbt psbt.elf >/dev/null
	qemu-system-riscv32 -M virt -cpu $(QEMU_CPU) -m 64M -nographic -bios none -semihosting -icount shift=0 \
	  -kernel build/qemu-psbt/psbt.elf </dev/null
	cmp build/psbt/own_mixed_nwu.qemu build/psbt/own_mixed_nwu.signed && echo "qemu output matches host"
.PHONY: check-qemu-psbt

build/test_ui: tests/ui/test_ui.c src/ui/ui.c src/ui/ui.h build/font8x16.h components/parts/signer/core.h
	cc -O2 -Wall -Wextra -Icomponents/parts/signer -Icomponents/parts/parser/c/include -Isrc/ui -Ibuild -I$(QRGEN) -o $@ tests/ui/test_ui.c src/ui/ui.c $(QRGEN)/qrcodegen.c $(CORE_SRC) \
	  components/parts/signer/secp256k1_unity.c -I$(SECP)/include -I$(SECP)/src $(SECP_DEFS) -Wno-unused-function

build/bip39_words.h: tools/generate/gen_bip39_words.py
	mkdir -p build && uv run -q $< $@

# SeedQR is untrusted input; run this check with sanitizers to catch out-of-bounds access.
build/test_seedqr: components/parts/signer/tests/test_seedqr.c components/parts/signer/seedqr.c components/parts/signer/seedqr.h build/bip39_words.h components/parts/parser/c/src/sha256.c
	cc -O1 -g -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all \
	  -Icomponents/parts/signer -Icomponents/parts/parser/c/include -Ibuild -o $@ components/parts/signer/tests/test_seedqr.c components/parts/signer/seedqr.c components/parts/parser/c/src/sha256.c

check-seedqr: build/test_seedqr
	build/test_seedqr
.PHONY: check-seedqr

check-ui: build/test_ui
	build/test_ui
.PHONY: check-ui

build/test_psbt.h: build/psbt/own_p2wpkh_1in.psbt
	cp build/psbt/own_mixed_nwu.psbt build/test_psbt.bin && cd build && xxd -i -n test_psbt test_psbt.bin \
	  | sed 's/^unsigned char/const unsigned char/' > test_psbt.h

build/rp2350/app.elf: build/parser_wasm.h build/font8x16.h build/test_psbt.h build/bip39_words.h \
  src/main.c src/drivers/st7789.c src/drivers/buttons.c CMakeLists.txt \
  src/runtime/parser_host.c src/ui/ui.c $(CORE_SRC) components/parts/parser/c/include/*.h
	cmake -S . -B build/rp2350 -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
	  -DPICO_SDK_PATH=$(CURDIR)/third_party/pico-sdk -DPICO_TOOLCHAIN_PATH=$(RISCV_TC) \
	  -DWAMR_BUILD_AOT=0 -DTESTNET=$(TESTNET) -DTEST_SEED=$(TEST_SEED) -DSIGNER_WASM_H_DIR=$(CURDIR)/build >/dev/null
	ninja -C build/rp2350 app

build/rp2350/psbt_bench.elf: build/parser_wasm.h build/test_psbt.h bringup/psbt_bench.c \
  CMakeLists.txt src/runtime/parser_host.c src/ui/ui.c $(CORE_SRC) components/parts/parser/c/include/*.h
	cmake -S . -B build/rp2350 -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
	  -DPICO_SDK_PATH=$(CURDIR)/third_party/pico-sdk -DPICO_TOOLCHAIN_PATH=$(RISCV_TC) \
	  -DWAMR_BUILD_AOT=$(PARSER_AOT) -DPARSER_POOL_KB=$(PARSER_POOL_KB) -DSIGNER_WASM_H_DIR=$(CURDIR)/build >/dev/null
	ninja -C build/rp2350 psbt_bench

build/rp2350/qr_bench.elf: build/qr_frames.h tests/host/qr_bench.c CMakeLists.txt build/rp2350/app.elf
	ninja -C build/rp2350 qr_bench

build/rp2350/pio_loopback_test.elf: bringup/pio_loopback_test.c bringup/dvp_gen.pio \
  src/drivers/camera.pio CMakeLists.txt build/rp2350/app.elf
	ninja -C build/rp2350 pio_loopback_test

build/rp2350/camera_test.elf: bringup/camera_test.c src/drivers/camera.c src/drivers/camera.pio \
  src/drivers/camera_ov7670.c CMakeLists.txt build/rp2350/app.elf
	ninja -C build/rp2350 camera_test

# Check camera.pio without hardware by running pioasm-generated instructions in a minimal PIO simulator.
check-camera-sim: build/rp2350/camera_test.elf
	python3 tests/host/sim_dvp_pio.py build/rp2350/camera.pio.h
.PHONY: check-camera-sim

# To start the device, hold BOOTSEL while connecting USB, then copy the UF2 to the RP2350 drive.
# The drive may be named RP2350 or NO NAME; identify it by its 134MB FAT16 volume.
UF2 ?= build/rp2350/app.uf2
SECONDS ?= 60
BOOT_VOL = $$(diskutil list | awk '/Windows_FAT_16/ && /134.2 MB/ {print $$NF}' | head -1 | \
  xargs -I{} sh -c 'diskutil info {} | sed -n "s/.*Mount Point: *//p"')
flash: $(UF2)
	@vol="$(BOOT_VOL)"; test -n "$$vol" \
	  || (echo "Boot drive not found. Hold BOOTSEL while connecting USB."; false)
	@vol="$(BOOT_VOL)"; cp $(UF2) "$$vol/" 2>/dev/null \
	  && echo "Copied $(UF2) to $$vol; the drive will disconnect and reboot." \
	  || (echo "Could not write to $$vol. In macOS Privacy & Security > Files and Folders,"; \
	      echo "allow Terminal to access removable volumes, or drag $(UF2) in Finder."; false)

# The concept diagram lives in the submodule because it describes the modules.
everywhere:
	$(MAKE) -C components/parts everywhere
	open components/parts/docs/everywhere.svg
.PHONY: everywhere

# Generate diagrams from the wiring data. wiring shows signal connections (WireViz and Graphviz); breadboard shows hole positions.
wiring: docs/wiring.yml
	uv run -q --with wireviz wireviz $< -o build/wiring
	cp build/wiring/wiring.svg docs/wiring.svg
	open build/wiring/wiring.html

breadboard: docs/breadboard.yml tools/generate/draw_breadboard.py
	uv run -q tools/generate/draw_breadboard.py $< docs/breadboard.svg
	open docs/breadboard.svg
.PHONY: wiring breadboard

# Read the Debug Probe UART at 115200bps; set a duration with SECONDS=10.
monitor:
	mkdir -p build && uv run -q tools/device/monitor.py $(SECONDS)

# Flash over the Debug Probe's SWD; this does not require BOOTSEL or reconnecting USB.
# Use Raspberry Pi's OpenOCD fork because upstream lacks a RISC-V DAP driver for Hazard3 (make deps-openocd builds it).
ELF ?= $(UF2:.uf2=.elf)
OPENOCD_DIR ?= $(HOME)/work/oss/openocd-rpi
OPENOCD = $(OPENOCD_DIR)/src/openocd -s $(OPENOCD_DIR)/tcl -f interface/cmsis-dap.cfg \
  -c "adapter speed 5000" -f target/rp2350-riscv.cfg
# Upstream OpenOCD cannot create the RISC-V target over DAP; rp2350.cfg's -dap option fails.
deps-openocd:
	mkdir -p $(dir $(OPENOCD_DIR))
	git clone --depth 1 https://github.com/raspberrypi/openocd.git $(OPENOCD_DIR)
	cd $(OPENOCD_DIR) && git submodule update --init --depth 1 jimtcl src/jtag/drivers/libjaylink \
	  && ./bootstrap && ./configure --enable-cmsis-dap --enable-internal-jimtcl --disable-werror && $(MAKE) -j8
.PHONY: deps-openocd

flash-swd: $(ELF)
	$(OPENOCD) -c "program $(ELF) verify reset exit"

# Flash, start receiving, and reset; capture output from the beginning.
run: $(ELF)
	@mkdir -p build
	$(OPENOCD) -c "program $(ELF) verify exit" 2>&1 | tail -3
	@uv run -q tools/device/monitor.py $(SECONDS) & \
	  sleep 2; $(OPENOCD) -c "init; reset run; exit" >/dev/null 2>&1; wait
.PHONY: flash flash-swd monitor run
