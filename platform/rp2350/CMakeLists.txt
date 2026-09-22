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
# QEMU 実測の highmark 33.5KB (classic interp) に余裕を持たせた値
target_compile_definitions(signer PRIVATE PICO_BUILD=1 POOL_KB=48)
target_include_directories(signer PRIVATE ${SIGNER_WASM_H_DIR} ${ROOT}/core)
target_link_libraries(signer pico_stdlib)
pico_enable_stdio_uart(signer 1)
pico_enable_stdio_usb(signer 0)
target_link_options(signer PRIVATE -Wl,--print-memory-usage)
pico_add_extra_outputs(signer)

# 実機の初期確認用アプリ（make build/rp2350/app.elf）。parser.wasm はインタプリタで動かす
set(SECP ${ROOT}/third_party/secp256k1)
add_executable(app ${CMAKE_CURRENT_LIST_DIR}/app_main.c ${CMAKE_CURRENT_LIST_DIR}/st7789.c
  ${CMAKE_CURRENT_LIST_DIR}/buttons.c ${ROOT}/runtime/host-abi/parser_host.c ${ROOT}/ui/ui.c
  ${ROOT}/core/core.c ${ROOT}/core/address.c ${ROOT}/core/bip32.c ${ROOT}/core/sighash.c ${ROOT}/core/tx.c
  ${ROOT}/core/sha256.c ${ROOT}/core/ripemd160.c ${ROOT}/core/sha512.c ${ROOT}/core/secp_callbacks.c
  ${ROOT}/signer/secp256k1_unity.c ${WAMR_RUNTIME_LIB_SOURCE})
target_compile_definitions(app PRIVATE PICO_BUILD=1 ENABLE_MODULE_EXTRAKEYS=1 ENABLE_MODULE_SCHNORRSIG=1
  ECMULT_WINDOW_SIZE=2 USE_EXTERNAL_DEFAULT_CALLBACKS=1 COMB_BLOCKS=2 COMB_TEETH=5)
target_compile_options(app PRIVATE -Wno-unused-function)
target_include_directories(app PRIVATE ${SIGNER_WASM_H_DIR} ${CMAKE_CURRENT_LIST_DIR} ${ROOT}/core
  ${ROOT}/runtime/host-abi ${ROOT}/ui ${SECP}/include)
target_link_libraries(app pico_stdlib pico_rand hardware_spi)
pico_enable_stdio_uart(app 1)
pico_enable_stdio_usb(app 0)
target_link_options(app PRIVATE -Wl,--print-memory-usage)
pico_add_extra_outputs(app)
