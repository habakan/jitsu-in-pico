/* parser.wasm → core → parser.wasm を一巡させるホスト。parse だけの検査と、署名までの一巡の 2 モード */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wasm_export.h"
#include "core.h"
#include "sha512.h"
#include "parser_wasm.h"
#ifdef QEMU_BUILD
static uint64_t now(void) { uint32_t lo, hi; __asm__ volatile("csrr %0, minstret; csrr %1, minstreth" : "=r"(lo), "=r"(hi)); return ((uint64_t)hi << 32) | lo; }
#define UNIT "instret"
#else
#include <time.h>
static uint64_t now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000; }
#define UNIT "us"
#endif

#define PSBT_MAX 32768
#define MNEMONIC "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"

static char pool[256 * 1024];
static wasm_module_inst_t inst;
static wasm_exec_env_t env;
static uint8_t file_buf[PSBT_MAX + 1];
static uint8_t prevtx_arena[PSBT_MAX];

static int call(const char *name, uint32_t argc, uint32_t *argv) {
    wasm_function_inst_t f = wasm_runtime_lookup_function(inst, name);
    if (!f || !wasm_runtime_call_wasm(env, f, argc, argv)) {
        printf("trap in %s: %s\n", name, wasm_runtime_get_exception(inst));
        wasm_runtime_clear_exception(inst);
        return 0;
    }
    return 1;
}

/* parser.wasm が返したアドレスを検証してからネイティブ側に写す。範囲外なら 0 */
static int copy_out(uint32_t off, uint32_t len, void *dst) {
    if (!wasm_runtime_validate_app_addr(inst, off, len)) return 0;
    memcpy(dst, wasm_runtime_addr_app_to_native(inst, off), len);
    return 1;
}

static long read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    long n;
    if (!f) return -1;
    n = (long)fread(file_buf, 1, sizeof(file_buf), f);
    fclose(f);
    return n;
}

static int parse(long len, uint32_t *rc) {
    uint32_t a[2] = {0};
    if (len < 0 || len > PSBT_MAX || !call("parser_input", 0, a)) return 0;
    if (!wasm_runtime_validate_app_addr(inst, a[0], (uint32_t)len)) return 0;
    memcpy(wasm_runtime_addr_app_to_native(inst, a[0]), file_buf, (size_t)len);
    a[0] = (uint32_t)len, a[1] = core_fingerprint();
    if (!call("parser_parse", 2, a)) return 0;
    *rc = a[0];
    return 1;
}

static int zero_rng(uint8_t *b, size_t n) { memset(b, 0, n); return 1; } /* 検査用。実機は TRNG */

static int sign(const char *in_path, const char *out_path) {
    static plan_t plan;
    core_prevtx_t prev[PLAN_MAX_INPUTS] = {{0}};
    core_review_t r;
    core_display_t d;
    core_sig_t sigs[PLAN_MAX_INPUTS];
    unsigned n_sigs;
    uint32_t a[1], rc, used = 0;
    uint64_t t0 = now(), t1, t2, t3, t4;
    char btc[21];
    int err;

    if (!parse(read_file(in_path), &rc) || rc) return printf("parse rc=%u\n", (unsigned)rc), 1;
    t1 = now();
    if (!call("parser_plan", 0, a) || !copy_out(a[0], sizeof(plan), &plan)) return printf("plan copy\n"), 1;
    for (unsigned i = 0; i < plan.n_inputs && i < PLAN_MAX_INPUTS; i++) {
        uint32_t off[1] = {i}, len[1] = {i};
        if (!call("parser_prevtx_off", 1, off) || !call("parser_prevtx_len", 1, len)) return 1;
        if (!len[0]) continue;
        if (len[0] > sizeof(prevtx_arena) - used || !copy_out(off[0], len[0], prevtx_arena + used)) return 1;
        prev[i].raw = prevtx_arena + used, prev[i].len = len[0];
        used += len[0];
    }
    if ((err = core_review(&plan, prev, &r)) != CORE_OK) return printf("review err=%d\n", err), 1;
    if ((err = core_display(&plan, &r, &d)) != CORE_OK) return printf("display err=%d\n", err), 1;
    t2 = now();
    for (unsigned i = 0; i < d.n_outputs; i++) {
        static const char *owner[] = {"send", "change", "self"};
        core_format_btc(d.outputs[i].amount, btc);
        printf("  %-6s %s BTC %s\n", owner[d.outputs[i].owner], btc, d.outputs[i].text);
    }
    core_format_btc(d.fee, btc);
    printf("  fee    %s BTC\n", btc);
    if ((err = core_sign(&plan, zero_rng, sigs, &n_sigs)) != CORE_OK) return printf("sign err=%d\n", err), 1;
    t3 = now();
    if (!call("parser_sigs", 0, a) || !wasm_runtime_validate_app_addr(inst, a[0], sizeof(core_sig_t) * n_sigs)) return 1;
    memcpy(wasm_runtime_addr_app_to_native(inst, a[0]), sigs, sizeof(core_sig_t) * n_sigs);
    a[0] = n_sigs;
    if (!call("parser_finalize", 1, a) || (int32_t)a[0] < 0) return printf("finalize rc=%d\n", (int)a[0]), 1;
    {
        uint32_t out_len = a[0], o[1];
        FILE *f;
        if (!call("parser_output", 0, o) || out_len > sizeof(file_buf) || !copy_out(o[0], out_len, file_buf)) return 1;
        t4 = now();
        if (!(f = fopen(out_path, "wb"))) return 1;
        fwrite(file_buf, 1, out_len, f);
        fclose(f);
    }
    printf("  signed %u input(s); %s parse=%llu review=%llu sign=%llu finalize=%llu\n", n_sigs, UNIT,
           (unsigned long long)(t1 - t0), (unsigned long long)(t2 - t1), (unsigned long long)(t3 - t2),
           (unsigned long long)(t4 - t3));
    return 0;
}

int main(int argc, char **argv) {
    RuntimeInitArgs args;
    char err[128];
    uint8_t seed[64];

#ifdef QEMU_BUILD
    /* libgloss の crt0 は semihosting のコマンドラインを渡さないので固定する */
    static char *qemu_argv[] = {"psbt_host", "sign", "build/psbt/own_mixed_nwu.psbt", "build/psbt/own_mixed_nwu.qemu"};
    argc = 4, argv = qemu_argv;
#endif
    if (argc < 3) return fprintf(stderr, "usage: %s parse FILE... | sign IN OUT\n", argv[0]), 2;
    memset(&args, 0, sizeof(args));
    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = pool;
    args.mem_alloc_option.pool.heap_size = sizeof(pool);
    if (!wasm_runtime_full_init(&args) || !core_init(CORE_MAINNET)) return 1;
    wasm_module_t mod = wasm_runtime_load(parser_wasm, parser_wasm_len, err, sizeof(err));
    if (!mod || !(inst = wasm_runtime_instantiate(mod, 8192, 0, err, sizeof(err)))) return printf("%s\n", err), 1;
    env = wasm_runtime_create_exec_env(inst, 8192);

    pbkdf2_hmac_sha512((const uint8_t *)MNEMONIC, sizeof(MNEMONIC) - 1, (const uint8_t *)"mnemonic", 8, 2048, seed);
    if (!core_load_seed(seed)) return 1;

    if (!strcmp(argv[1], "sign")) return argc == 4 ? sign(argv[2], argv[3]) : 2;
    for (int i = 2; i < argc; i++) {
        uint32_t rc = 0;
        int ok = parse(read_file(argv[i]), &rc);
        printf("%s %s\n", argv[i], ok ? (rc ? "rejected" : "accepted") : "trap");
        if (ok && rc) printf("  rc=%u\n", (unsigned)rc);
    }
    return 0;
}
