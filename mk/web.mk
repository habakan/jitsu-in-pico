# The firmware in the browser: main.c and parser.wasm on WAMR, compiled with Emscripten into one HTML
web: build/web/signer.html

build/web/signer.html: build/parser_wasm.h build/font8x16.h build/test_psbt.h web/CMakeLists.txt web/hal.c web/shell.html \
  src/main.c src/runtime/parser_host.c src/ui/ui.c $(CORE_SRC) components/parts/parser/c/include/*.h
	emcmake cmake -S web -B build/web -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIGNER_WASM_H_DIR=$(CURDIR)/build >/dev/null
	ninja -C build/web

.PHONY: web
