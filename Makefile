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
	cd third_party && git clone --depth 1 https://github.com/fcambus/spleen.git
	curl -sL -o third_party/rv.zip $(RISCV_TC_URL) && unzip -q third_party/rv.zip -d third_party/riscv-toolchain && rm third_party/rv.zip
	$(MAKE) patch-deps

# classic interp の i64.store は 4 byte 境界を前提にしており、Hazard3 では非整列ストアで例外になる
patch-deps:
	git -C third_party/wasm-micro-runtime apply $(CURDIR)/patches/wamr-classic-interp-unaligned-i64-store.patch
.PHONY: deps patch-deps

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
	  -DWAMR_BUILD_AOT=$(AOT) -DPOOL_KB=$(RP2350_POOL_KB) -DSIGNER_WASM_H_DIR=$(CURDIR)/build >/dev/null
	ninja -C build/rp2350

POOL_KB ?= 128
RP2350_POOL_KB ?= 48
PARSER_POOL_KB ?= 256
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

# 既定は自前のフォーク（submodule、mcu ブランチ）の固定小数点版。
# 上流と比べるときは QUIRC=third_party/quirc/lib QUIRC_DEFS= を渡す
QUIRC   ?= qr/quirc/lib
QUIRC_DEFS ?= -DQUIRC_FIXED_POINT_FITNESS -DQUIRC_FLOAT_TYPE=float -DQUIRC_USE_TGMATH
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
# tx.c / sha256.c は wasm-psbt-parser（submodule）と共有する
CORE_SRC := core/core.c core/address.c core/bip32.c core/sighash.c parser/src/tx.c parser/src/sha256.c core/ripemd160.c core/sha512.c \
            core/secp_callbacks.c
build/core_vectors.h: tools/gen_core_vectors.py test-vectors/bip341-wallet-test-vectors.json
	mkdir -p build && uv run -q $< test-vectors/bip341-wallet-test-vectors.json $@

build/test_core: core/tests/test_core.c $(CORE_SRC) core/*.h parser/include/*.h build/core_vectors.h signer/secp256k1_unity.c
	cc -O2 -Wall -Wextra -Wno-unused-function -Icore -Iparser/include -Ibuild -I$(SECP)/include $(SECP_DEFS) \
	  -o $@ core/tests/test_core.c $(CORE_SRC) signer/secp256k1_unity.c

check-core: build/test_core
	build/test_core
.PHONY: check-core

build/qemu-test-core.elf: core/tests/test_core.c $(CORE_SRC) core/*.h parser/include/*.h build/core_vectors.h platform/qemu-riscv32/start.S
	$(RISCV_TC)/bin/riscv32-pico-elf-gcc $(QEMU_MARCH) -O2 -Wall -Wno-unused-function -Icore -Iparser/include -Ibuild \
	  -I$(SECP)/include $(SECP_DEFS) --specs=semihost.specs -Wl,--section-start=.qemu_start=0x80000000 \
	  -Wl,-Ttext=0x80001000 -Wl,-e,qemu_start -Wl,--gc-sections -o $@ \
	  core/tests/test_core.c $(CORE_SRC) signer/secp256k1_unity.c platform/qemu-riscv32/start.S

check-qemu-core: build/qemu-test-core.elf
	qemu-system-riscv32 -M virt -cpu $(QEMU_CPU) -m 64M -nographic -bios none -semihosting \
	  -kernel $< </dev/null
.PHONY: check-qemu-core

# parser.wasm は wasm-psbt-parser（submodule）の Makefile でビルドする
build/parser.wasm: parser/src/*.c parser/include/*.h
	mkdir -p build
	$(MAKE) -C parser build/parser.wasm LLVM=$(LLVM) WASI=$(WASI) RTLIB=$(RTLIB)
	cp parser/build/parser.wasm $@

check-parser:
	$(MAKE) -C parser test
.PHONY: check-parser

# PARSER_AOT=1 では parser も wamrc で RV32 ネイティブにする。実機では XIP が 7 倍遅いので RAM 展開のみ
PARSER_AOT ?= 0
WAMRC_RAM_FLAGS := --target=riscv32 --target-abi=ilp32 --cpu=generic-rv32 --cpu-features=+m,+a,+c,+zba,+zbb,+zbs \
  --bounds-checks=1
PARSER_BIN := build/parser.$(if $(filter 1,$(PARSER_AOT)),aot,wasm)

build/parser.aot: build/parser.wasm $(WAMRC)
	$(WAMRC) $(WAMRC_RAM_FLAGS) -o $@ $< >/dev/null

build/parser_wasm.h: $(PARSER_BIN)
	xxd -i -n parser_wasm $< | sed 's/^unsigned char/const unsigned char/' > $@

build/psbt/own_p2wpkh_1in.psbt: tools/gen_psbt_vectors.py
	rm -rf build/psbt && uv run -q $< build/psbt

build/font8x16.h: tools/gen_font.py
	mkdir -p build && python3 $< third_party/spleen/spleen-8x16.bdf $@

build/host-classic/psbt_host: build/parser_wasm.h build/signer_wasm.h build/font8x16.h host/psbt_main.c host/CMakeLists.txt \
  runtime/host-abi/parser_host.c ui/ui.c $(CORE_SRC) parser/include/*.h
	cmake -S host -B build/host-classic -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DWAMR_BUILD_FAST_INTERP=0 \
	  -DSIGNER_WASM_H_DIR=$(CURDIR)/build >/dev/null
	ninja -C build/host-classic psbt_host >/dev/null

# 解析器リポジトリの UR ベクタ（参照エンコーダの出力）から、混在 PSBT の 60 byte 断片版を 1 行 1 パートで書き出す。
# 純粋なパートを 3 つに 1 つ落として、混ぜたパートでの復元も通す
build/psbt/own_mixed_nwu.ur: parser/tests/ur_vectors.json build/psbt/own_p2wpkh_1in.psbt
	python3 -c "import json,re; v=[x for x in json.load(open('$<'))['vectors'] if x['name']=='own_mixed_nwu' and x['fragment_len']==60][0]; \
	  print('\n'.join(p for p in v['parts'] if not (int(re.match(r'UR:[A-Z-]+/(\d+)', p).group(1)) <= v['seq_len'] and int(re.match(r'UR:[A-Z-]+/(\d+)', p).group(1)) % 3 == 0)))" > $@

check-psbt: build/host-classic/psbt_host build/psbt/own_p2wpkh_1in.psbt build/psbt/own_mixed_nwu.ur
	rm -f build/psbt/*.signed
	for f in build/psbt/own_*.psbt; do echo "== $$f"; build/host-classic/psbt_host sign $$f $${f%.psbt}.signed || true; done
	build/host-classic/psbt_host sign build/psbt/own_mixed_nwu.ur build/psbt/own_mixed_nwu_ur.out build/psbt/qr
	cmp build/psbt/own_mixed_nwu_ur.out build/psbt/own_mixed_nwu.signed && echo "UR path matches the binary PSBT path"
	# 署名済み PSBT の UR（混ぜたパートを含む）を読み戻すと、署名済み PSBT そのものに戻ること
	build/host-classic/psbt_host ur2bin build/psbt/own_mixed_nwu_ur.out.ur build/psbt/roundtrip.out
	cmp build/psbt/roundtrip.out build/psbt/own_mixed_nwu.signed && echo "signed PSBT survives the UR round trip"
	uv run -q tools/check_qr_screen.py build/psbt/qr build/psbt/own_mixed_nwu_ur.out.ur
	uv run -q tools/check_signed_psbt.py build/psbt
.PHONY: check-psbt

check-qemu-psbt: build/parser_wasm.h build/signer_wasm.h build/font8x16.h build/psbt/own_mixed_nwu.ur
	cmake -S platform/qemu-riscv32 -B build/qemu-psbt -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
	  -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
	  -DCMAKE_C_COMPILER=$(RISCV_TC)/bin/riscv32-pico-elf-gcc -DCMAKE_ASM_COMPILER=$(RISCV_TC)/bin/riscv32-pico-elf-gcc \
	  -DPOOL_KB=64 -DSIGNER_WASM_H_DIR=$(CURDIR)/build >/dev/null
	ninja -C build/qemu-psbt psbt.elf >/dev/null
	qemu-system-riscv32 -M virt -cpu $(QEMU_CPU) -m 64M -nographic -bios none -semihosting -icount shift=0 \
	  -kernel build/qemu-psbt/psbt.elf </dev/null
	cmp build/psbt/own_mixed_nwu.qemu build/psbt/own_mixed_nwu.signed && echo "qemu output matches host"
.PHONY: check-qemu-psbt

build/test_ui: ui/tests/test_ui.c ui/ui.c ui/ui.h build/font8x16.h core/core.h
	cc -O2 -Wall -Wextra -Icore -Iparser/include -Iui -Ibuild -I$(QRGEN) -o $@ ui/tests/test_ui.c ui/ui.c $(QRGEN)/qrcodegen.c $(CORE_SRC) \
	  signer/secp256k1_unity.c -I$(SECP)/include $(SECP_DEFS) -Wno-unused-function

check-ui: build/test_ui
	build/test_ui
.PHONY: check-ui

build/test_psbt.h: build/psbt/own_p2wpkh_1in.psbt
	cp build/psbt/own_mixed_nwu.psbt build/test_psbt.bin && cd build && xxd -i -n test_psbt test_psbt.bin \
	  | sed 's/^unsigned char/const unsigned char/' > test_psbt.h

build/rp2350/app.elf: build/parser_wasm.h build/signer_wasm.h build/font8x16.h build/test_psbt.h \
  platform/rp2350/app_main.c platform/rp2350/st7789.c platform/rp2350/buttons.c platform/rp2350/CMakeLists.txt \
  runtime/host-abi/parser_host.c ui/ui.c $(CORE_SRC) parser/include/*.h
	cmake -S platform/rp2350 -B build/rp2350 -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
	  -DPICO_SDK_PATH=$(CURDIR)/third_party/pico-sdk -DPICO_TOOLCHAIN_PATH=$(RISCV_TC) \
	  -DWAMR_BUILD_AOT=0 -DSIGNER_WASM_H_DIR=$(CURDIR)/build >/dev/null
	ninja -C build/rp2350 app

build/rp2350/psbt_bench.elf: build/parser_wasm.h build/test_psbt.h platform/rp2350/psbt_bench.c \
  platform/rp2350/CMakeLists.txt runtime/host-abi/parser_host.c ui/ui.c $(CORE_SRC) parser/include/*.h
	cmake -S platform/rp2350 -B build/rp2350 -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
	  -DPICO_SDK_PATH=$(CURDIR)/third_party/pico-sdk -DPICO_TOOLCHAIN_PATH=$(RISCV_TC) \
	  -DWAMR_BUILD_AOT=$(PARSER_AOT) -DPARSER_POOL_KB=$(PARSER_POOL_KB) -DSIGNER_WASM_H_DIR=$(CURDIR)/build >/dev/null
	ninja -C build/rp2350 psbt_bench

build/rp2350/qr_bench.elf: build/qr_frames.h host/qr_bench.c platform/rp2350/CMakeLists.txt build/rp2350/app.elf
	ninja -C build/rp2350 qr_bench

build/rp2350/pio_loopback_test.elf: platform/rp2350/pio_loopback_test.c platform/rp2350/dvp_gen.pio \
  platform/rp2350/camera.pio platform/rp2350/CMakeLists.txt build/rp2350/app.elf
	ninja -C build/rp2350 pio_loopback_test

build/rp2350/camera_test.elf: platform/rp2350/camera_test.c platform/rp2350/camera.c platform/rp2350/camera.pio \
  platform/rp2350/camera_ov7670.c platform/rp2350/CMakeLists.txt build/rp2350/app.elf
	ninja -C build/rp2350 camera_test

# camera.pio を実機なしで確かめる（pioasm が生成した命令語を最小の PIO シミュレータで実行する）
check-camera-sim: build/rp2350/camera_test.elf
	python3 tools/sim_dvp_pio.py build/rp2350/camera.pio.h
.PHONY: check-camera-sim

# 実機の立ち上げ: BOOTSEL を押しながら USB を挿すと RP2350 ドライブとして見えるので、そこへ uf2 をコピーする
# ドライブ名は RP2350 のこともラベル無し（NO NAME）のこともあるので、134MB の FAT16 を探す
UF2 ?= build/rp2350/signer.uf2
SECONDS ?= 60
BOOT_VOL = $$(diskutil list | awk '/Windows_FAT_16/ && /134.2 MB/ {print $$NF}' | head -1 | \
  xargs -I{} sh -c 'diskutil info {} | sed -n "s/.*Mount Point: *//p"')
flash: $(UF2)
	@vol="$(BOOT_VOL)"; test -n "$$vol" \
	  || (echo "ブートドライブが見えません。BOOTSEL を押しながら USB を挿してください"; false)
	@vol="$(BOOT_VOL)"; cp $(UF2) "$$vol/" 2>/dev/null \
	  && echo "$(UF2) を $$vol へ書き込みました（ドライブが外れて再起動します）" \
	  || (echo "$$vol へ書き込めません。macOS の「プライバシーとセキュリティ → ファイルとフォルダ」で"; \
	      echo "ターミナルに「リムーバブルボリューム」を許可するか、Finder で $(UF2) をドラッグしてください"; false)

# Debug Probe の UART（115200bps）を受ける。SECONDS=10 のように秒数を指定できる
monitor:
	mkdir -p build && uv run -q tools/monitor.py $(SECONDS)

# Debug Probe の SWD で書く。BOOTSEL も USB の抜き差しも要らない。
# Hazard3 を DAP 経由で叩く riscv ドライバは上流の OpenOCD に無いので、Raspberry Pi のフォークを使う
# （make deps-openocd でビルドする）
ELF ?= $(UF2:.uf2=.elf)
OPENOCD_DIR ?= $(HOME)/work/oss/openocd-rpi
OPENOCD = $(OPENOCD_DIR)/src/openocd -s $(OPENOCD_DIR)/tcl -f interface/cmsis-dap.cfg \
  -c "adapter speed 5000" -f target/rp2350-riscv.cfg
# 上流の OpenOCD は riscv ターゲットを DAP 経由で作れない（rp2350.cfg の -dap が通らない）
deps-openocd:
	mkdir -p $(dir $(OPENOCD_DIR))
	git clone --depth 1 https://github.com/raspberrypi/openocd.git $(OPENOCD_DIR)
	cd $(OPENOCD_DIR) && git submodule update --init --depth 1 jimtcl src/jtag/drivers/libjaylink \
	  && ./bootstrap && ./configure --enable-cmsis-dap --enable-internal-jimtcl --disable-werror && $(MAKE) -j8
.PHONY: deps-openocd

flash-swd: $(ELF)
	$(OPENOCD) -c "program $(ELF) verify reset exit"

# 書き込み → 受信開始 → リセット。出力を頭から取れる
run: $(ELF)
	@mkdir -p build
	$(OPENOCD) -c "program $(ELF) verify exit" 2>&1 | tail -3
	@uv run -q tools/monitor.py $(SECONDS) & \
	  sleep 2; $(OPENOCD) -c "init; reset run; exit" >/dev/null 2>&1; wait
.PHONY: flash flash-swd monitor run
