#include <stdio.h>
#include <string.h>
#include "wasm_export.h"
#include "sha512.h"
#include "vectors.h"
#include "signer_wasm.h"
#ifdef PICO_BUILD
#include "pico/stdlib.h"
#include "hardware/uart.h"
#define now() time_us_64()
#define UNIT "us"
#elif defined(QEMU_BUILD)
/* Time under QEMU says nothing about the device, so retired instructions are counted instead */
static uint64_t now(void) { uint32_t lo, hi; __asm__ volatile("csrr %0, minstret; csrr %1, minstreth" : "=r"(lo), "=r"(hi)); return ((uint64_t)hi << 32) | lo; }
#define UNIT "instret"
#else
#include <time.h>
static uint64_t now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000; }
#define UNIT "us"
#endif

/* Every WAMR allocation is confined to this pool, and highmark reports what was actually used */
#ifndef POOL_KB
#define POOL_KB 128
#endif
static char pool[POOL_KB * 1024];

/* argv carries the arguments in and the return value back out in argv[0] */
static int call_args(wasm_exec_env_t env, wasm_module_inst_t inst, const char *name, uint32_t argc, uint32_t *argv) {
    uint32_t last = argc ? argv[argc - 1] : 0;
    /* The start is printed too, outside the timing, so a hang can be traced to a call */
    printf("> %s/%u\n", name, last);
    uint64_t t0 = now();
    wasm_function_inst_t f = wasm_runtime_lookup_function(inst, name);
    if (!f || !wasm_runtime_call_wasm(env, f, argc, argv)) {
        printf("call %s failed: %s\n", name, wasm_runtime_get_exception(inst));
        return 0;
    }
    printf("%s %s/%u %llu\n", UNIT, name, last, (unsigned long long)(now() - t0));
    return 1;
}

static int call(wasm_exec_env_t env, wasm_module_inst_t inst, const char *name, uint32_t *ret) {
    uint32_t argv[1] = {0};
    if (!call_args(env, inst, name, 0, argv)) return 0;
    if (ret) *ret = argv[0];
    return 1;
}

/* Imported by a wasm built with SHA512_HOST_COMPRESS. An out-of-range address raises an exception */
static void host_sha512_compress(wasm_exec_env_t env, uint32_t s_off, uint32_t blk_off) {
    wasm_module_inst_t inst = wasm_runtime_get_module_inst(env);
    uint64_t s[8];
    if (!wasm_runtime_validate_app_addr(inst, s_off, sizeof(s)) || !wasm_runtime_validate_app_addr(inst, blk_off, 128))
        return;
    uint8_t *sp = wasm_runtime_addr_app_to_native(inst, s_off);
    memcpy(s, sp, sizeof(s));
    sha512_compress(s, wasm_runtime_addr_app_to_native(inst, blk_off));
    memcpy(sp, s, sizeof(s));
}

static NativeSymbol natives[] = {{"host_sha512_compress", host_sha512_compress, "(ii)", NULL}};

#ifdef PICO_BUILD
/* The SDK's default handler stops the core silently, so the cause goes straight to the UART; printf
 * may not be usable from here */
void __attribute__((used, noinline)) trap_report(uint32_t sp) {
    uint32_t csr[4];
    __asm__ volatile("csrr %0, mcause; csrr %1, mepc; csrr %2, mtval; csrr %3, mstatus"
                     : "=r"(csr[0]), "=r"(csr[1]), "=r"(csr[2]), "=r"(csr[3]));
    const char *names[4] = {"mcause", "mepc", "mtval", "mstatus"};
    for (int i = 0; i < 4; i++) {
        for (const char *c = names[i]; *c; c++) uart_putc_raw(uart_default, *c);
        uart_putc_raw(uart_default, '=');
        for (int b = 28; b >= 0; b -= 4) uart_putc_raw(uart_default, "0123456789abcdef"[csr[i] >> b & 15]);
        uart_putc_raw(uart_default, i == 3 ? '\n' : ' ');
    }
    for (const char *c = "sp="; *c; c++) uart_putc_raw(uart_default, *c);
    for (int b = 28; b >= 0; b -= 4) uart_putc_raw(uart_default, "0123456789abcdef"[sp >> b & 15]);
    uart_putc_raw(uart_default, '\n');
    while (1) tight_loop_contents();
}

/* The vector table is in RAM and dispatches with a j instruction, so these entry points cannot live
 * in flash */
void __attribute__((naked, section(".time_critical.trap"))) isr_riscv_machine_exception(void) {
    __asm__ volatile("mv a0, sp\n tail trap_report");
}
#endif

static void hex(const char *label, const unsigned char *p, int n) {
    printf("%s ", label);
    for (int i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
}

#ifdef QEMU_BUILD
extern uint32_t qemu_stack[], qemu_stack_top[];
/* crt0 clears the stack along with BSS, so the unused part is painted at the top of main to measure
 * how much gets used */
static void __attribute__((noinline)) paint_stack(void) {
    uint32_t *sp, *p = qemu_stack;
    __asm__ volatile("mv %0, sp" : "=r"(sp));
    while (p < sp - 64) *p++ = 0xdeadbeef;
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
    wasm_runtime_register_natives("env", natives, sizeof(natives) / sizeof(natives[0]));

#ifdef QEMU_BUILD
    /* To find out whether this can be executed in place from flash: compare afterwards and see if the
     * loader wrote back into the original bytes */
    static unsigned char orig[sizeof(signer_wasm)];
    memcpy(orig, signer_wasm, sizeof(orig));
#endif
    uint8_t *wasm_buf = (uint8_t *)signer_wasm;
#if defined(PICO_BUILD) && WASM_ENABLE_INTERP != 0
    /* The classic interpreter rewrites the bytecode as it loads. On the device .rodata is in flash and
     * cannot be written, so it is copied to RAM first */
    static unsigned char wasm_ram[sizeof(signer_wasm)];
    memcpy(wasm_ram, signer_wasm, sizeof(wasm_ram));
    wasm_buf = wasm_ram;
#endif
    wasm_module_t mod = wasm_runtime_load(wasm_buf, signer_wasm_len, err, sizeof(err));
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
    hex("ecdsa", io + 96, 64);
    if (!call(env, inst, "signer_sign_schnorr", &ok) || !ok) return 1;
    hex("schnorr", io + 96, 64);

    uint32_t in_off, a[2];
    if (!call(env, inst, "signer_in", &in_off)) return 1;
    unsigned char *in = wasm_runtime_addr_app_to_native(inst, in_off);
    memcpy(in, TV_MN12 "TREZOR", sizeof(TV_MN12) + 5);
    a[0] = sizeof(TV_MN12) - 1, a[1] = 6;
    if (!call_args(env, inst, "signer_seed_from_mnemonic", 2, a) || !a[0]) return 1;
    hex("seed12", io + 96, 64);
    memcpy(in, TV_MN24 "TREZOR", sizeof(TV_MN24) + 5);
    a[0] = sizeof(TV_MN24) - 1, a[1] = 6;
    if (!call_args(env, inst, "signer_seed_from_mnemonic", 2, a) || !a[0]) return 1;
    hex("seed24", io + 96, 64);
    memcpy(in, TV_MN12, sizeof(TV_MN12) - 1);
    a[0] = sizeof(TV_MN12) - 1, a[1] = 0;
    if (!call_args(env, inst, "signer_seed_from_mnemonic", 2, a) || !a[0]) return 1;
    memcpy(in, TV_PATH, sizeof(TV_PATH));
    a[0] = 4;
    if (!call_args(env, inst, "signer_bip32_derive", 1, a) || !a[0]) return 1;
    a[0] = 5;
    if (!call_args(env, inst, "signer_bip32_derive", 1, a) || !a[0]) return 1;
    hex("bip84_pub", io + 96, 33);

    mem_alloc_info_t mi;
    wasm_runtime_get_mem_alloc_info(&mi);
    printf("wasm_size %u\npool_highmark %u\n", signer_wasm_len, mi.highmark_size);
#ifdef QEMU_BUILD
    printf("binary_modified %s\n", memcmp(orig, signer_wasm, sizeof(orig)) ? "yes" : "no");
    uint32_t *p = qemu_stack;
    while (p < qemu_stack_top && *p == 0xdeadbeef) p++;
    printf("native_stack_used %u\n", (unsigned)((char *)qemu_stack_top - (char *)p));
#endif
#ifdef PICO_BUILD
    /* Returning from main makes the SDK ebreak, which would read like a trap, so stop here instead */
    printf("done\n");
    while (1) tight_loop_contents();
#endif
    return 0;
}
