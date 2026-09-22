LLVM    ?= /opt/homebrew/opt/llvm/bin
WASI    ?= /opt/homebrew/opt/wasi-libc/share/wasi-sysroot
SECP    := third_party/secp256k1
# COMB 設定と ecmult_gen テーブル: 2,5=2KB / 11,6=22KB / 43,6=86KB。WASM では data segment として RAM に載る
COMB    ?= -DCOMB_BLOCKS=2 -DCOMB_TEETH=5
SECP_DEFS := -DENABLE_MODULE_EXTRAKEYS=1 -DENABLE_MODULE_SCHNORRSIG=1 -DECMULT_WINDOW_SIZE=2 \
             -DUSE_EXTERNAL_DEFAULT_CALLBACKS=1 $(COMB)
RTLIB   ?= /opt/homebrew/opt/wasi-runtimes/share/wasi-runtimes/lib/wasm32-unknown-wasip1
CFLAGS  := -Oz -Wall -Wno-unused-function -I$(SECP)/include $(SECP_DEFS)
STACK   ?= 16384

build/bitcoin-signer.wasm: signer/signer.c signer/secp256k1_unity.c
	mkdir -p build
	$(LLVM)/clang --target=wasm32-wasip1 --sysroot=$(WASI) -nostartfiles -nodefaultlibs $(CFLAGS) \
	  -Wl,--no-entry -Wl,--gc-sections -Wl,--strip-all -Wl,-z,stack-size=$(STACK) \
	  -Wl,--export=__heap_base -Wl,--export=__data_end -Wl,--initial-memory=65536 -Wl,--max-memory=65536 \
	  -o $@ signer/signer.c signer/secp256k1_unity.c -lc $(RTLIB)/libclang_rt.builtins.a

clean:
	rm -rf build
.PHONY: clean

RISCV_TC_URL := https://github.com/raspberrypi/pico-sdk-tools/releases/download/v2.3.1-0/riscv-toolchain-16-mac.zip
deps:
	mkdir -p third_party
	cd third_party && git clone --depth 1 https://github.com/bitcoin-core/secp256k1.git
	cd third_party && git clone --depth 1 -b WAMR-2.4.3 https://github.com/bytecodealliance/wasm-micro-runtime.git
	cd third_party && git clone --depth 1 -b 2.3.1 https://github.com/raspberrypi/pico-sdk.git
	curl -sL -o third_party/rv.zip $(RISCV_TC_URL) && unzip -q third_party/rv.zip -d third_party/riscv-toolchain && rm third_party/rv.zip
.PHONY: deps

build/signer_wasm.h: build/bitcoin-signer.wasm
	cd build && xxd -i -n signer_wasm bitcoin-signer.wasm > signer_wasm.h

build/native: host/native.c signer/signer.c signer/secp256k1_unity.c
	mkdir -p build && cc -O2 -Wall -Wno-unused-function -I$(SECP)/include $(SECP_DEFS) -o $@ $^

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
	  -DSIGNER_WASM_H_DIR=$(CURDIR)/build >/dev/null
	ninja -C build/rp2350

POOL_KB ?= 128
FAST    ?= 0
QEMU_DIR := build/qemu-fast$(FAST)-$(POOL_KB)
$(QEMU_DIR)/signer.elf: build/signer_wasm.h host/wamr_main.c platform/qemu-riscv32/CMakeLists.txt runtime/wamr-platform/rp2350/rp2350_platform.c
	cmake -S platform/qemu-riscv32 -B $(QEMU_DIR) -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
	  -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
	  -DCMAKE_C_COMPILER=$(RISCV_TC)/bin/riscv32-pico-elf-gcc -DCMAKE_ASM_COMPILER=$(RISCV_TC)/bin/riscv32-pico-elf-gcc \
	  -DPOOL_KB=$(POOL_KB) -DWAMR_BUILD_FAST_INTERP=$(FAST) -DSIGNER_WASM_H_DIR=$(CURDIR)/build >/dev/null
	ninja -C $(QEMU_DIR) >/dev/null

QEMU_CPU := rv32,f=off,d=off,zfa=off,zba=on,zbb=on,zbs=on,zbkb=on,zcb=on,zcmp=on,zcmt=off
check-qemu: $(QEMU_DIR)/signer.elf
	qemu-system-riscv32 -M virt -cpu $(QEMU_CPU) -m 64M -nographic -bios none -semihosting -icount shift=0 \
	  -kernel $< </dev/null
.PHONY: check-qemu

# 比較用: WASM を通さず同じ signer を RV32 ネイティブで動かす
build/qemu-native.elf: host/native.c signer/signer.c signer/secp256k1_unity.c platform/qemu-riscv32/start.S
	$(RISCV_TC)/bin/riscv32-pico-elf-gcc -mcpu=hazard3-rp2350 -Os -DQEMU_BUILD=1 -Wall -Wno-unused-function \
	  -I$(SECP)/include $(SECP_DEFS) --specs=semihost.specs -Wl,--section-start=.qemu_start=0x80000000 \
	  -Wl,-Ttext=0x80001000 -Wl,-e,qemu_start -Wl,--gc-sections -o $@ $^

check-qemu-native: build/qemu-native.elf
	qemu-system-riscv32 -M virt -cpu $(QEMU_CPU) -m 64M -nographic -bios none -semihosting -icount shift=0 \
	  -kernel $< </dev/null
.PHONY: check-qemu-native
