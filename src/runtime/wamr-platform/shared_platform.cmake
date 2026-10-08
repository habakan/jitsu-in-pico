set(PLATFORM_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR})
add_definitions(-DBH_PLATFORM_RP2350)
include_directories(${PLATFORM_SHARED_DIR} ${WAMR_ROOT_DIR}/core/shared/platform/include)
set(PLATFORM_SHARED_SOURCE ${PLATFORM_SHARED_DIR}/rp2350_platform.c)
