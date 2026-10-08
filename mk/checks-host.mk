C_SRC_FILES := $(shell git ls-files '*.c')
C_FORMAT_FILES := $(C_SRC_FILES) $(shell git ls-files '*.h')
C_TIDY_FILES := $(addprefix components/parts/,$(shell git -C components/parts ls-files 'parser/c/src/*.c'))

build/qr_frames.h: tools/generate/gen_qr_frames.py
	mkdir -p build && uv run -q $< $@


# Compare quirc and zxing-cpp decoding on macOS; check-qemu-qr measures instruction counts.
build/qr_bench_mac: tests/host/qr_bench.c build/qr_frames.h $(QUIRC)/identify.c
	cc -O2 -Wall $(QUIRC_DEFS) -I$(QUIRC) -I$(QRGEN) -Ibuild -o $@ tests/host/qr_bench.c $(QUIRC)/*.c $(QRGEN)/qrcodegen.c

check-qr-mac: build/qr_bench_mac
	build/qr_bench_mac | awk '{print $$1, $$4}'
	uv run -q tests/host/zxing_check.py build/qr_frames | sed 's/^/zxing /'
.PHONY: check-qr-mac


build/core_vectors.h: tools/generate/gen_core_vectors.py tests/vectors/bip341-wallet-test-vectors.json
	mkdir -p build && uv run -q $< tests/vectors/bip341-wallet-test-vectors.json $@

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

build/test_ui: tests/ui/test_ui.c src/ui/ui.c src/ui/ui.h build/font8x16.h components/parts/signer/core.h
	cc -O2 -Wall -Wextra -Icomponents/parts/signer -Icomponents/parts/parser/c/include -Isrc/ui -Ibuild -I$(QRGEN) -o $@ tests/ui/test_ui.c src/ui/ui.c $(QRGEN)/qrcodegen.c $(CORE_SRC) \
	  components/parts/signer/secp256k1_unity.c -I$(SECP)/include -I$(SECP)/src $(SECP_DEFS) -Wno-unused-function

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

# Check camera.pio without hardware by running pioasm-generated instructions in a minimal PIO simulator.
check-camera-sim: build/rp2350/camera_test.elf
	python3 tests/host/sim_dvp_pio.py build/rp2350/camera.pio.h
.PHONY: check-camera-sim
