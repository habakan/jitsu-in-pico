PARSER_POOL_KB ?= 256
TESTNET ?= 0
# Allow selecting the test seed; keep this at 0 for production builds.
TEST_SEED ?= 0

build/test_psbt.h: build/psbt/own_p2wpkh_1in.psbt
	cp build/psbt/own_mixed_nwu.psbt build/test_psbt.bin && cd build && xxd -i -n test_psbt test_psbt.bin \
	  | sed 's/^unsigned char/const unsigned char/' > test_psbt.h

build/rp2350/app.elf: build/parser_wasm.h build/font8x16.h build/test_psbt.h \
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
