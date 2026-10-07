QEMU_CPU := rv32,f=off,d=off,zfa=off,zba=on,zbb=on,zbs=on,zbkb=on,zcb=on,zcmp=on,zcmt=off

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

build/qemu-test-core.elf: components/parts/signer/tests/test_core.c $(CORE_SRC) components/parts/signer/*.h components/parts/parser/c/include/*.h build/core_vectors.h tests/qemu/start.S
	$(RISCV_TC)/bin/riscv32-pico-elf-gcc $(QEMU_MARCH) -O2 -Wall -Wno-unused-function -Icomponents/parts/signer -Icomponents/parts/parser/c/include -Ibuild \
	  -I$(SECP)/include -I$(SECP)/src $(SECP_DEFS) --specs=semihost.specs -Wl,--section-start=.qemu_start=0x80000000 \
	  -Wl,-Ttext=0x80001000 -Wl,-e,qemu_start -Wl,--gc-sections -o $@ \
	  components/parts/signer/tests/test_core.c $(CORE_SRC) components/parts/signer/secp256k1_unity.c tests/qemu/start.S

check-qemu-core: build/qemu-test-core.elf
	qemu-system-riscv32 -M virt -cpu $(QEMU_CPU) -m 64M -nographic -bios none -semihosting \
	  -kernel $< </dev/null
.PHONY: check-qemu-core

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
