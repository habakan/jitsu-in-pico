cmake_minimum_required(VERSION 3.14)
project(signer_host C ASM)

set(WAMR_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../../third_party/wasm-micro-runtime)
if(NOT WAMR_BUILD_PLATFORM)
  string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} WAMR_BUILD_PLATFORM)
endif()
set(WAMR_BUILD_TARGET AARCH64)
set(WAMR_BUILD_INTERP 1)
if(NOT DEFINED WAMR_BUILD_FAST_INTERP)
  set(WAMR_BUILD_FAST_INTERP 0)
endif()
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_LIBC_BUILTIN 0)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_SIMD 0)
set(WAMR_BUILD_REF_TYPES 0)
include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

add_executable(signer_wamr wamr_main.c ../../components/signer/sha512.c ${WAMR_RUNTIME_LIB_SOURCE})
target_include_directories(signer_wamr PRIVATE ${SIGNER_WASM_H_DIR} ../../components/signer)
target_compile_definitions(signer_wamr PRIVATE POOL_KB=256)

# parser.wasm + ネイティブ中核の一巡（make check-psbt）
set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(SECP ${ROOT}/third_party/secp256k1)
add_executable(psbt_host psbt_main.c ${ROOT}/apps/device/runtime/host-abi/parser_host.c ${ROOT}/apps/device/ui/ui.c ${ROOT}/third_party/QR-Code-generator/c/qrcodegen.c ${ROOT}/components/signer/core.c ${ROOT}/components/signer/address.c ${ROOT}/components/signer/bip32.c
  ${ROOT}/components/signer/sighash.c ${ROOT}/components/parser/src/tx.c ${ROOT}/components/parser/src/sha256.c ${ROOT}/components/signer/ripemd160.c ${ROOT}/components/signer/sha512.c
  ${ROOT}/components/signer/secp_callbacks.c ${ROOT}/components/signer/secp256k1_unity.c ${WAMR_RUNTIME_LIB_SOURCE})
target_include_directories(psbt_host PRIVATE ${SIGNER_WASM_H_DIR} ${ROOT}/components/signer ${ROOT}/apps/device/runtime/host-abi ${ROOT}/components/parser/include ${ROOT}/apps/device/ui ${ROOT}/third_party/QR-Code-generator/c ${SECP}/include)
target_compile_definitions(psbt_host PRIVATE ENABLE_MODULE_EXTRAKEYS=1 ENABLE_MODULE_SCHNORRSIG=1 ECMULT_WINDOW_SIZE=2
  USE_EXTERNAL_DEFAULT_CALLBACKS=1 COMB_BLOCKS=2 COMB_TEETH=5)
target_compile_options(psbt_host PRIVATE -Wno-unused-function)
