cmake_minimum_required(VERSION 3.13)
project(signer_web C)

# The firmware's main.c, UI and parser.wasm on WAMR's classic interpreter, built with Emscripten.
# WAMR has no wasm32 target; X86_32 matches its pointer size and the C invokeNative needs no assembly
set(ROOT ${CMAKE_CURRENT_LIST_DIR}/..)
set(SRC ${ROOT}/src)
set(WAMR_ROOT_DIR ${ROOT}/third_party/wasm-micro-runtime)
set(SHARED_PLATFORM_CONFIG ${SRC}/runtime/wamr-platform/shared_platform.cmake)
set(WAMR_BUILD_PLATFORM rp2350)
set(WAMR_BUILD_TARGET X86_32)
set(WAMR_BUILD_INVOKE_NATIVE_GENERAL 1)
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_FAST_INTERP 0)
set(WAMR_BUILD_LIBC_BUILTIN 0)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_SIMD 0)
set(WAMR_BUILD_REF_TYPES 0)
set(WAMR_DISABLE_HW_BOUND_CHECK 1)
include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

set(TEST_SEED 1 CACHE STRING "allow selecting the BIP39 test vector seed")
set(TESTNET 0 CACHE STRING "testnet / signet addresses and derivation")
set(SECP ${ROOT}/third_party/secp256k1)
set(QUIRC ${ROOT}/components/qr/quirc/lib)
add_executable(signer ${CMAKE_CURRENT_LIST_DIR}/hal.c ${SRC}/main.c ${SRC}/runtime/parser_host.c ${SRC}/ui/ui.c
  ${ROOT}/third_party/QR-Code-generator/c/qrcodegen.c ${ROOT}/components/parts/signer/core.c ${ROOT}/components/parts/signer/address.c
  ${ROOT}/components/parts/signer/bip32.c ${ROOT}/components/parts/signer/sighash.c ${ROOT}/components/parts/parser/c/src/tx.c
  ${ROOT}/components/parts/parser/c/src/sha256.c ${ROOT}/components/parts/signer/ripemd160.c ${ROOT}/components/parts/signer/sha512.c
  ${ROOT}/components/parts/signer/secp_callbacks.c ${ROOT}/components/parts/signer/seedqr.c ${ROOT}/components/parts/signer/bip85.c
  ${ROOT}/components/parts/signer/secp256k1_unity.c ${QUIRC}/decode.c ${QUIRC}/identify.c ${QUIRC}/quirc.c ${QUIRC}/version_db.c
  ${WAMR_RUNTIME_LIB_SOURCE})
target_compile_definitions(signer PRIVATE ENABLE_MODULE_EXTRAKEYS=1 ENABLE_MODULE_SCHNORRSIG=1 ENABLE_MODULE_RECOVERY=1
  ECMULT_WINDOW_SIZE=2 USE_EXTERNAL_DEFAULT_CALLBACKS=1 COMB_BLOCKS=2 COMB_TEETH=5 NO_LCD=0 TEST_SEED=${TEST_SEED}
  TESTNET=${TESTNET} _PLATFORM_WASI_TYPES_H QUIRC_FIXED_POINT_FITNESS QUIRC_FLOAT_TYPE=float QUIRC_USE_TGMATH)
target_include_directories(signer PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${SIGNER_WASM_H_DIR} ${SRC}/drivers ${SRC}/runtime ${SRC}/ui
  ${ROOT}/components/parts/signer ${ROOT}/components/parts/parser/c/include ${ROOT}/third_party/QR-Code-generator/c ${QUIRC}
  ${SECP}/include ${SECP}/src)
target_compile_options(signer PRIVATE -Wno-unused-function)
# Without ASYNCIFY_IGNORE_INDIRECT every call through the interpreter's tables gets instrumented;
# nothing reached that way sleeps
target_link_options(signer PRIVATE -sASYNCIFY -sASYNCIFY_IGNORE_INDIRECT -sALLOW_MEMORY_GROWTH -sSTACK_SIZE=262144
  -sSINGLE_FILE -sENVIRONMENT=web --shell-file ${CMAKE_CURRENT_LIST_DIR}/shell.html)
set_target_properties(signer PROPERTIES SUFFIX .html)
