# Runs the same RV32 WAMR under QEMU virt instead of the RP2350, to measure 32-bit memory use and
# instruction counts
cmake_minimum_required(VERSION 3.13)
project(signer_qemu C ASM)
set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)

set(WAMR_ROOT_DIR ${ROOT}/third_party/wasm-micro-runtime)
set(SHARED_PLATFORM_CONFIG ${ROOT}/apps/device/runtime/wamr-platform/rp2350/shared_platform.cmake)
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
if(NOT DEFINED WAMR_BUILD_FAST_INTERP)
  set(WAMR_BUILD_FAST_INTERP 0)
endif()
set(WAMR_BUILD_LIBC_BUILTIN 0)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_SIMD 0)
set(WAMR_BUILD_REF_TYPES 0)
set(WAMR_DISABLE_HW_BOUND_CHECK 1)
include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)

add_executable(signer.elf ${ROOT}/apps/host/wamr_main.c ${ROOT}/components/parts/signer/sha512.c ${CMAKE_CURRENT_LIST_DIR}/start.S ${WAMR_RUNTIME_LIB_SOURCE})
target_compile_definitions(signer.elf PRIVATE QEMU_BUILD=1 POOL_KB=${POOL_KB})
target_compile_options(signer.elf PRIVATE -mcpu=hazard3-rp2350 -Os)
target_include_directories(signer.elf PRIVATE ${SIGNER_WASM_H_DIR} ${ROOT}/components/parts/signer)
target_link_options(signer.elf PRIVATE -mcpu=hazard3-rp2350 --specs=semihost.specs
  -Wl,--section-start=.qemu_start=0x80000000 -Wl,-Ttext=0x80001000 -Wl,-e,qemu_start -Wl,--gc-sections)
target_link_libraries(signer.elf m)

# A full round of parser.wasm plus the native core (make check-qemu-psbt)
set(SECP ${ROOT}/third_party/secp256k1)
add_executable(psbt.elf ${ROOT}/apps/host/psbt_main.c ${ROOT}/apps/device/runtime/host-abi/parser_host.c ${ROOT}/apps/device/ui/ui.c ${ROOT}/third_party/QR-Code-generator/c/qrcodegen.c ${ROOT}/components/parts/signer/core.c ${ROOT}/components/parts/signer/address.c ${ROOT}/components/parts/signer/bip32.c
  ${ROOT}/components/parts/signer/sighash.c ${ROOT}/components/parts/parser/c/src/tx.c ${ROOT}/components/parts/parser/c/src/sha256.c ${ROOT}/components/parts/signer/ripemd160.c ${ROOT}/components/parts/signer/sha512.c
  ${ROOT}/components/parts/signer/secp_callbacks.c ${ROOT}/components/parts/signer/secp256k1_unity.c ${CMAKE_CURRENT_LIST_DIR}/start.S ${WAMR_RUNTIME_LIB_SOURCE})
target_include_directories(psbt.elf PRIVATE ${SIGNER_WASM_H_DIR} ${ROOT}/components/parts/signer ${ROOT}/apps/device/runtime/host-abi ${ROOT}/components/parts/parser/c/include ${ROOT}/apps/device/ui ${ROOT}/third_party/QR-Code-generator/c ${SECP}/include ${SECP}/src)
target_compile_definitions(psbt.elf PRIVATE QEMU_BUILD=1 ENABLE_MODULE_EXTRAKEYS=1 ENABLE_MODULE_SCHNORRSIG=1
  ECMULT_WINDOW_SIZE=2 USE_EXTERNAL_DEFAULT_CALLBACKS=1 COMB_BLOCKS=2 COMB_TEETH=5)
# -mcpu=hazard3-rp2350 can emit Xh3bextm, which QEMU does not implement, so only the standard
# extensions are named
set(QEMU_MARCH -march=rv32imac_zicsr_zifencei_zba_zbb_zbs_zbkb_zcb_zcmp -mabi=ilp32)
target_compile_options(psbt.elf PRIVATE ${QEMU_MARCH} -Os -Wno-unused-function)
target_link_options(psbt.elf PRIVATE ${QEMU_MARCH} --specs=semihost.specs
  -Wl,--section-start=.qemu_start=0x80000000 -Wl,-Ttext=0x80001000 -Wl,-e,qemu_start -Wl,--gc-sections)
target_link_libraries(psbt.elf m)
