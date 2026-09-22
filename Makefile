LLVM    ?= /opt/homebrew/opt/llvm/bin
WASI    ?= /opt/homebrew/opt/wasi-libc/share/wasi-sysroot
SECP    := third_party/secp256k1
# COMB 設定と ecmult_gen テーブル: 2,5=2KB / 11,6=22KB / 43,6=86KB。WASM では data segment として RAM に載る
COMB    ?= -DCOMB_BLOCKS=2 -DCOMB_TEETH=5
SECP_DEFS := -DENABLE_MODULE_EXTRAKEYS=1 -DENABLE_MODULE_SCHNORRSIG=1 -DECMULT_WINDOW_SIZE=2 \
             -DUSE_EXTERNAL_DEFAULT_CALLBACKS=1 $(COMB)
RTLIB   ?= /opt/homebrew/opt/wasi-runtimes/share/wasi-runtimes/lib/wasm32-unknown-wasip1
# SHA512_HOST=1 で SHA-512 圧縮関数をホストの import にする
SHA512_HOST ?= 0
CFLAGS  := -Oz -Wall -Wno-unused-function -Icore -I$(SECP)/include $(SECP_DEFS) $(if $(filter 1,$(SHA512_HOST)),-DSHA512_HOST_COMPRESS)
STACK   ?= 16384

build/bitcoin-signer.wasm: signer/signer.c core/sha512.c core/bip32.c core/secp_callbacks.c signer/secp256k1_unity.c
	mkdir -p build
	$(LLVM)/clang --target=wasm32-wasip1 --sysroot=$(WASI) -nostartfiles -nodefaultlibs $(CFLAGS) \
	  -Wl,--no-entry -Wl,--gc-sections -Wl,--strip-all -Wl,-z,stack-size=$(STACK) \
	  -Wl,--export=__heap_base -Wl,--export=__data_end -Wl,--initial-memory=65536 -Wl,--max-memory=65536 \
	  -o $@ signer/signer.c core/sha512.c core/bip32.c core/secp_callbacks.c signer/secp256k1_unity.c -lc $(RTLIB)/libclang_rt.builtins.a

clean:
	rm -rf build
.PHONY: clean

RISCV_TC_URL := https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.3.1-0/riscv-toolchain-16-mac.zip
deps:
	mkdir -p third_party
	cd third_party && git clone --depth 1 https://github.com/bitcoin-core/secp256k1.git
	cd third_party && git clone --depth 1 -b WAMR-2.4.3 https://github.com/bytecodealliance/wasm-micro-runtime.git
	cd third_party && git clone --depth 1 -b 2.3.1 https://github.com/raspberrypi/pico-sdk.git
	cd third_party && git clone --depth 1 https://github.com/dlbeer/quirc.git
	cd third_party && git clone --depth 1 https://github.com/nayuki/QR-Code-generator.git
	curl -sL -o third_party/rv.zip $(RISCV_TC_URL) && unzip -q third_party/rv.zip -d third_party/riscv-toolchain && rm third_party/rv.zip
.PHONY: deps

# AOT=1 では wamrc で RV32 ネイティブにした .aot を Flash に置いて XIP 実行する。--bounds-checks=1 は MMU 無しでの線形メモリ保護。
# XIP の既定は i64 の乗算・シフトまで関数呼び出しにするが、rv32 で libgcc 呼び出しになるのは除算・剰余だけなので絞る
AOT     ?= 0
WAMRC   := build/wamrc/wamrc
WAMRC_FLAGS ?= --target=riscv32 --target-abi=ilp32 --cpu=generic-rv32 --cpu-features=+m,+a,+c,+zba,+zbb,+zbs \
  --bounds-checks=1 --xip --enable-builtin-intrinsics=i64.div_s,i64.div_u,i64.rem_s,i64.rem_u,i32.const,f32.common,f64.common
SIGNER_BIN := build/bitcoin-signer.$(if $(filter 1,$(AOT)),aot,wasm)

$(WAMRC):
	cmake -S third_party/wasm-micro-runtime/wamr-compiler -B build/wamrc -G Ninja -DCMAKE_BUILD_TYPE=Release \
	  -DWAMR_BUILD_WITH_CUSTOM_LLVM=1 -DLLVM_DIR=/opt/homebrew/opt/llvm@18/lib/cmake/llvm >/dev/null
	ninja -C build/wamrc >/dev/null

build/bitcoin-signer.aot: build/bitcoin-signer.wasm $(WAMRC)
	$(WAMRC) $(WAMRC_FLAGS) -o $@ $< >/dev/null

build/signer_wasm.h: $(SIGNER_BIN)
	xxd -i -n signer_wasm $< $(if $(filter 1,$(AOT)),| sed 's/^unsigned char/const unsigned char/') > $@

build/native: host/native.c signer/signer.c core/sha512.c core/bip32.c core/secp_callbacks.c signer/secp256k1_unity.c
	mkdir -p build && cc -O2 -Wall -Wno-unused-function -Icore -I$(SECP)/include $(SECP_DEFS) -o $@ $^

build/host-%/signer_wamr: build/signer_wasm.h host/wamr_main.c host/CMakeLists.txt
	cmake -S host -B build/host-$* -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
	  -DWAMR_BUILD_FAST_INTERP=$(if $(filter fast,$*),1,0) -DSIGNER_WASM_H_DIR=$(CURDIR)/build >/dev/null
	ninja -C build/host-$* >/dev/null

check-host: build/native build/host-classic/signer_wamr build/host-fast/signer_wamr
	build/native
	build/host-classic/signer_wamr
	build/host-fast/signer_wamr
.PHONY: check-host

RISCV_TC ?= $(CURDIR)/third_party/riscv-toolchain
build/rp2350/signer.elf: build/signer_wasm.h host/wamr_main.c platform/rp2350/CMakeLists.txt runtime/wamr-platform/rp2350/rp2350_platform.c
	cmake -S platform/rp2350 -B build/rp2350 -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
	  -DPICO_SDK_PATH=$(CURDIR)/third_party/pico-sdk -DPICO_TOOLCHAIN_PATH=$(RISCV_TC) \
	  -DWAMR_BUILD_AOT=$(AOT) -DSIGNER_WASM_H_DIR=$(CURDIR)/build >/dev/null
	ninja -C build/rp2350

POOL_KB ?= 128
FAST    ?= 0
QEMU_DIR := build/qemu-fast$(FAST)-aot$(AOT)-$(POOL_KB)
$(QEMU_DIR)/signer.elf: build/signer_wasm.h host/wamr_main.c platform/qemu-riscv32/CMakeLists.txt runtime/wamr-platform/rp2350/rp2350_platform.c
	cmake -S platform/qemu-riscv32 -B $(QEMU_DIR) -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
	  -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
	  -DCMAKE_C_COMPILER=$(RISCV_TC)/bin/riscv32-pico-elf-gcc -DCMAKE_ASM_COMPILER=$(RISCV_TC)/bin/riscv32-pico-elf-gcc \
	  -DPOOL_KB=$(POOL_KB) -DWAMR_BUILD_FAST_INTERP=$(FAST) -DWAMR_BUILD_AOT=$(AOT) -DSIGNER_WASM_H_DIR=$(CURDIR)/build >/dev/null
	ninja -C $(QEMU_DIR) >/dev/null

QEMU_CPU := rv32,f=off,d=off,zfa=off,zba=on,zbb=on,zbs=on,zbkb=on,zcb=on,zcmp=on,zcmt=off
check-qemu: $(QEMU_DIR)/signer.elf
	qemu-system-riscv32 -M virt -cpu $(QEMU_CPU) -m 64M -nographic -bios none -semihosting -icount shift=0 \
	  -kernel $< </dev/null
.PHONY: check-qemu

# 比較用: WASM を通さず同じ signer を RV32 ネイティブで動かす
build/qemu-native.elf: host/native.c signer/signer.c core/sha512.c core/bip32.c core/secp_callbacks.c signer/secp256k1_unity.c platform/qemu-riscv32/start.S
	$(RISCV_TC)/bin/riscv32-pico-elf-gcc -mcpu=hazard3-rp2350 -Os -DQEMU_BUILD=1 -Wall -Wno-unused-function \
	  -Icore -I$(SECP)/include $(SECP_DEFS) --specs=semihost.specs -Wl,--section-start=.qemu_start=0x80000000 \
	  -Wl,-Ttext=0x80001000 -Wl,-e,qemu_start -Wl,--gc-sections -o $@ $^

check-qemu-native: build/qemu-native.elf
	qemu-system-riscv32 -M virt -cpu $(QEMU_CPU) -m 64M -nographic -bios none -semihosting -icount shift=0 \
	  -kernel $< </dev/null
.PHONY: check-qemu-native

# 既定はパッチ適用版。QUIRC=third_party/quirc/lib で素の quirc と比較できる
QUIRC   ?= build/quirc/lib
QUIRC_DEFS ?= -DQUIRC_FLOAT_TYPE=float -DQUIRC_USE_TGMATH
build/quirc/lib/identify.c: qr/quirc-fixed-point-fitness.patch
	rm -rf build/quirc && mkdir -p build/quirc && cp -r third_party/quirc/lib build/quirc/
	patch -s -d build/quirc -p1 < $<
QRGEN   := third_party/QR-Code-generator/c
build/qr_frames.h: tools/gen_qr_frames.py
	mkdir -p build && uv run -q $< $@

# -O2 だと Hazard3 独自の Xh3bextm 命令が出て QEMU で落ちるため、標準拡張だけを指定する
QEMU_MARCH := -march=rv32imac_zicsr_zifencei_zba_zbb_zbs_zbkb_zcb_zcmp -mabi=ilp32
build/qemu-qr.elf: host/qr_bench.c build/qr_frames.h platform/qemu-riscv32/start.S $(QUIRC)/identify.c
	$(RISCV_TC)/bin/riscv32-pico-elf-gcc $(QEMU_MARCH) -O2 -DQEMU_BUILD=1 -Wall \
	  $(QUIRC_DEFS) -I$(QUIRC) -I$(QRGEN) -Ibuild --specs=semihost.specs -Wl,--section-start=.qemu_start=0x80000000 \
	  -Wl,-Ttext=0x80001000 -Wl,-e,qemu_start -Wl,--gc-sections -o $@ \
	  host/qr_bench.c $(wildcard $(QUIRC)/*.c) $(QRGEN)/qrcodegen.c platform/qemu-riscv32/start.S -lm

check-qemu-qr: build/qemu-qr.elf
	qemu-system-riscv32 -M virt -cpu $(QEMU_CPU) -m 64M -nographic -bios none -semihosting -icount shift=0 \
	  -kernel $< </dev/null
.PHONY: check-qemu-qr

# 読取可否は Mac ネイティブで quirc と zxing-cpp を比べる（命令数は check-qemu-qr で測る）
build/qr_bench_mac: host/qr_bench.c build/qr_frames.h $(QUIRC)/identify.c
	cc -O2 -Wall $(QUIRC_DEFS) -I$(QUIRC) -I$(QRGEN) -Ibuild -o $@ host/qr_bench.c $(QUIRC)/*.c $(QRGEN)/qrcodegen.c

check-qr-mac: build/qr_bench_mac
	build/qr_bench_mac | awk '{print $$1, $$4}'
	uv run -q tools/zxing_check.py build/qr_frames | sed 's/^/zxing /'
.PHONY: check-qr-mac

# 案 B のネイティブ署名中核（docs/architecture-b.md）
CORE_SRC := core/core.c core/address.c core/bip32.c core/sighash.c core/tx.c core/sha256.c core/ripemd160.c core/sha512.c \
            core/secp_callbacks.c
build/core_vectors.h: tools/gen_core_vectors.py test-vectors/bip341-wallet-test-vectors.json
	mkdir -p build && uv run -q $< test-vectors/bip341-wallet-test-vectors.json $@

build/test_core: core/tests/test_core.c $(CORE_SRC) core/*.h runtime/host-abi/plan.h build/core_vectors.h signer/secp256k1_unity.c
	cc -O2 -Wall -Wextra -Wno-unused-function -Icore -Iruntime/host-abi -Ibuild -I$(SECP)/include $(SECP_DEFS) \
	  -o $@ core/tests/test_core.c $(CORE_SRC) signer/secp256k1_unity.c

check-core: build/test_core
	build/test_core
.PHONY: check-core

build/qemu-test-core.elf: core/tests/test_core.c $(CORE_SRC) core/*.h runtime/host-abi/plan.h build/core_vectors.h platform/qemu-riscv32/start.S
	$(RISCV_TC)/bin/riscv32-pico-elf-gcc $(QEMU_MARCH) -O2 -Wall -Wno-unused-function -Icore -Iruntime/host-abi -Ibuild \
	  -I$(SECP)/include $(SECP_DEFS) --specs=semihost.specs -Wl,--section-start=.qemu_start=0x80000000 \
	  -Wl,-Ttext=0x80001000 -Wl,-e,qemu_start -Wl,--gc-sections -o $@ \
	  core/tests/test_core.c $(CORE_SRC) signer/secp256k1_unity.c platform/qemu-riscv32/start.S

check-qemu-core: build/qemu-test-core.elf
	qemu-system-riscv32 -M virt -cpu $(QEMU_CPU) -m 64M -nographic -bios none -semihosting \
	  -kernel $< </dev/null
.PHONY: check-qemu-core
