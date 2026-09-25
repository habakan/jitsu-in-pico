cmake_minimum_required(VERSION 3.13)
set(PICO_PLATFORM rp2350-riscv)
set(PICO_BOARD pico2)
set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
include(${ROOT}/third_party/pico-sdk/external/pico_sdk_import.cmake)
project(signer_rp2350 C CXX ASM)
pico_sdk_init()

set(WAMR_ROOT_DIR ${ROOT}/third_party/wasm-micro-runtime)
set(SHARED_PLATFORM_CONFIG ${ROOT}/runtime/wamr-platform/rp2350/shared_platform.cmake)
set(WAMR_BUILD_PLATFORM rp2350)
set(WAMR_BUILD_TARGET RISCV32_ILP32)
if(NOT DEFINED WAMR_BUILD_AOT)
  set(WAMR_BUILD_AOT 0)
endif()
if(WAMR_BUILD_AOT)
  set(WAMR_BUILD_INTERP 0)
else()
  set(WAMR_BUILD_INTERP 1)
endif()
set(WAMR_BUILD_FAST_INTERP 0)
set(WAMR_BUILD_LIBC_BUILTIN 0)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_SIMD 0)
set(WAMR_BUILD_REF_TYPES 0)
set(WAMR_DISABLE_HW_BOUND_CHECK 1)
include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)
# runtime_lib.cmake が -std=gnu99 を足すが、pico-sdk は static_assert に C11 が要る
string(REPLACE "-std=gnu99" "-std=gnu11" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

add_executable(signer ${ROOT}/host/wamr_main.c ${ROOT}/core/sha512.c ${WAMR_RUNTIME_LIB_SOURCE})
# プールは QEMU 実測の highmark に余裕を持たせた値。AOT を RAM 展開するときは -DPOOL_KB=320 を渡す
if(NOT DEFINED POOL_KB)
  set(POOL_KB 48)
endif()
target_compile_definitions(signer PRIVATE PICO_BUILD=1 POOL_KB=${POOL_KB} PICO_STACK_SIZE=0x4000)
target_include_directories(signer PRIVATE ${SIGNER_WASM_H_DIR} ${ROOT}/core)
target_link_libraries(signer pico_stdlib)
pico_enable_stdio_uart(signer 1)
pico_enable_stdio_usb(signer 0)
target_link_options(signer PRIVATE -Wl,--print-memory-usage)
pico_add_extra_outputs(signer)

# 実機の初期確認用アプリ（make build/rp2350/app.elf）。parser.wasm はインタプリタで動かす
set(SECP ${ROOT}/third_party/secp256k1)
add_executable(app ${CMAKE_CURRENT_LIST_DIR}/app_main.c ${CMAKE_CURRENT_LIST_DIR}/st7789.c
  ${CMAKE_CURRENT_LIST_DIR}/buttons.c ${ROOT}/runtime/host-abi/parser_host.c ${ROOT}/ui/ui.c ${ROOT}/third_party/QR-Code-generator/c/qrcodegen.c
  ${ROOT}/core/core.c ${ROOT}/core/address.c ${ROOT}/core/bip32.c ${ROOT}/core/sighash.c ${ROOT}/parser/src/tx.c
  ${ROOT}/parser/src/sha256.c ${ROOT}/core/ripemd160.c ${ROOT}/core/sha512.c ${ROOT}/core/secp_callbacks.c
  ${ROOT}/signer/secp256k1_unity.c ${WAMR_RUNTIME_LIB_SOURCE})
target_compile_definitions(app PRIVATE PICO_BUILD=1 ENABLE_MODULE_EXTRAKEYS=1 ENABLE_MODULE_SCHNORRSIG=1
  ECMULT_WINDOW_SIZE=2 USE_EXTERNAL_DEFAULT_CALLBACKS=1 COMB_BLOCKS=2 COMB_TEETH=5)
target_compile_options(app PRIVATE -Wno-unused-function)
target_compile_definitions(app PRIVATE PICO_STACK_SIZE=0x8000)
target_include_directories(app PRIVATE ${SIGNER_WASM_H_DIR} ${CMAKE_CURRENT_LIST_DIR} ${ROOT}/core
  ${ROOT}/runtime/host-abi ${ROOT}/parser/include ${ROOT}/ui ${ROOT}/third_party/QR-Code-generator/c ${SECP}/include)
target_link_libraries(app pico_stdlib pico_rand hardware_spi)
pico_enable_stdio_uart(app 1)
pico_enable_stdio_usb(app 0)
target_link_options(app PRIVATE -Wl,--print-memory-usage)
pico_add_extra_outputs(app)

# PSBT 一巡の実機計測（make build/rp2350/psbt_bench.elf）。表示とボタンは使わない
add_executable(psbt_bench ${CMAKE_CURRENT_LIST_DIR}/psbt_bench.c ${ROOT}/runtime/host-abi/parser_host.c ${ROOT}/ui/ui.c
  ${ROOT}/third_party/QR-Code-generator/c/qrcodegen.c ${ROOT}/core/core.c ${ROOT}/core/address.c ${ROOT}/core/bip32.c
  ${ROOT}/core/sighash.c ${ROOT}/parser/src/tx.c ${ROOT}/parser/src/sha256.c ${ROOT}/core/ripemd160.c
  ${ROOT}/core/sha512.c ${ROOT}/core/secp_callbacks.c ${ROOT}/signer/secp256k1_unity.c ${WAMR_RUNTIME_LIB_SOURCE})
if(NOT DEFINED PARSER_POOL_KB)
  set(PARSER_POOL_KB 256)
endif()
target_compile_definitions(psbt_bench PRIVATE PICO_BUILD=1 POOL_KB=${PARSER_POOL_KB} PICO_STACK_SIZE=0x8000
  ENABLE_MODULE_EXTRAKEYS=1 ENABLE_MODULE_SCHNORRSIG=1 ECMULT_WINDOW_SIZE=2 USE_EXTERNAL_DEFAULT_CALLBACKS=1
  COMB_BLOCKS=2 COMB_TEETH=5)
target_compile_options(psbt_bench PRIVATE -Wno-unused-function)
target_include_directories(psbt_bench PRIVATE ${SIGNER_WASM_H_DIR} ${CMAKE_CURRENT_LIST_DIR} ${ROOT}/core
  ${ROOT}/runtime/host-abi ${ROOT}/parser/include ${ROOT}/ui ${ROOT}/third_party/QR-Code-generator/c ${SECP}/include)
target_link_libraries(psbt_bench pico_stdlib)
pico_enable_stdio_uart(psbt_bench 1)
pico_enable_stdio_usb(psbt_bench 0)
target_link_options(psbt_bench PRIVATE -Wl,--print-memory-usage)
pico_add_extra_outputs(psbt_bench)

# カメラの初期確認（make build/rp2350/camera_test.elf）。PIO で Y を拾い、自前フォークの quirc で読む
set(QUIRC ${ROOT}/qr/quirc/lib)
add_executable(camera_test ${CMAKE_CURRENT_LIST_DIR}/camera_test.c ${CMAKE_CURRENT_LIST_DIR}/camera.c
  ${CMAKE_CURRENT_LIST_DIR}/camera_ov7670.c ${CMAKE_CURRENT_LIST_DIR}/st7789.c
  ${QUIRC}/decode.c ${QUIRC}/identify.c ${QUIRC}/quirc.c ${QUIRC}/version_db.c)
pico_generate_pio_header(camera_test ${CMAKE_CURRENT_LIST_DIR}/camera.pio)
target_compile_definitions(camera_test PRIVATE QUIRC_FIXED_POINT_FITNESS QUIRC_FLOAT_TYPE=float QUIRC_USE_TGMATH
  PICO_STACK_SIZE=0x8000)
target_include_directories(camera_test PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${QUIRC})
target_link_libraries(camera_test pico_stdlib hardware_pio hardware_dma hardware_i2c hardware_spi hardware_clocks)
pico_enable_stdio_uart(camera_test 1)
pico_enable_stdio_usb(camera_test 0)
target_link_options(camera_test PRIVATE -Wl,--print-memory-usage)
pico_add_extra_outputs(camera_test)
