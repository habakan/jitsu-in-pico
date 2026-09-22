#include <stdio.h>
#include <string.h>
#include "wasm_export.h"
#include "vectors.h"
#include "signer_wasm.h"
#ifdef PICO_BUILD
#include "pico/stdlib.h"
#define now() time_us_64()
#define UNIT "us"
#elif defined(QEMU_BUILD)
/* QEMU では時間が実機と無関係なので、リタイア命令数を数える */
static uint64_t now(void) { uint32_t lo, hi; __asm__ volatile("csrr %0, minstret; csrr %1, minstreth" : "=r"(lo), "=r"(hi)); return ((uint64_t)hi << 32) | lo; }
#define UNIT "instret"
#else
#include <time.h>
static uint64_t now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000; }
#define UNIT "us"
#endif

/* WAMR の全確保をこのプールに閉じ込め、highmark で実使用量を測る */
#ifndef POOL_KB
#define POOL_KB 128
#endif
static char pool[POOL_KB * 1024];

static int call(wasm_exec_env_t env, wasm_module_inst_t inst, const char *name, uint32_t *ret) {
    uint64_t t0 = now();
    wasm_function_inst_t f = wasm_runtime_lookup_function(inst, name);
    uint32_t argv[1] = {0};
    if (!f || !wasm_runtime_call_wasm(env, f, 0, argv)) {
        printf("call %s failed: %s\n", name, wasm_runtime_get_exception(inst));
        return 0;
    }
    if (ret) *ret = argv[0];
    printf("%s %s %llu\n", UNIT, name, (unsigned long long)(now() - t0));
    return 1;
}

static void hex(const char *label, const unsigned char *p) {
    printf("%s ", label);
    for (int i = 0; i < 64; i++) printf("%02x", p[i]);
    printf("\n");
}

#ifdef QEMU_BUILD
extern uint32_t qemu_stack[], qemu_stack_top[];
/* crt0 が BSS ごとスタックを消すので、main 入口で未使用部分を塗って使用量を測る */
static void paint_stack(void) {
    uint32_t marker, *p = qemu_stack;
    while (p < &marker - 64) *p++ = 0xdeadbeef;
}
#endif

int main(void) {
    RuntimeInitArgs args;
    char err[128];
#ifdef PICO_BUILD
    stdio_init_all();
#elif defined(QEMU_BUILD)
    paint_stack();
#endif
    memset(&args, 0, sizeof(args));
    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = pool;
    args.mem_alloc_option.pool.heap_size = sizeof(pool);
    if (!wasm_runtime_full_init(&args)) return 1;

    wasm_module_t mod = wasm_runtime_load(signer_wasm, signer_wasm_len, err, sizeof(err));
    if (!mod) { printf("load: %s\n", err); return 1; }
    wasm_module_inst_t inst = wasm_runtime_instantiate(mod, 4096, 0, err, sizeof(err));
    if (!inst) { printf("instantiate: %s\n", err); return 1; }
    wasm_exec_env_t env = wasm_runtime_create_exec_env(inst, 4096);

    uint32_t io_off, ok;
    if (!call(env, inst, "signer_io", &io_off)) return 1;
    unsigned char *io = wasm_runtime_addr_app_to_native(inst, io_off);
    if (!call(env, inst, "signer_init", &ok) || !ok) return 1;
    memcpy(io, TV_IN, 96);
    if (!call(env, inst, "signer_sign_ecdsa", &ok) || !ok) return 1;
    hex("ecdsa", io + 96);
    if (!call(env, inst, "signer_sign_schnorr", &ok) || !ok) return 1;
    hex("schnorr", io + 96);

    mem_alloc_info_t mi;
    wasm_runtime_get_mem_alloc_info(&mi);
    printf("wasm_size %u\npool_highmark %u\n", signer_wasm_len, mi.highmark_size);
#ifdef QEMU_BUILD
    uint32_t *p = qemu_stack;
    while (p < qemu_stack_top && *p == 0xdeadbeef) p++;
    printf("native_stack_used %u\n", (unsigned)((char *)qemu_stack_top - (char *)p));
#endif
    return 0;
}
