#include "parser_host.h"
#include <stdio.h>
#include <string.h>
#include "wasm_export.h"

static wasm_module_inst_t inst;
static wasm_exec_env_t env;

static int call(const char *name, uint32_t argc, uint32_t *argv) {
    wasm_function_inst_t f = wasm_runtime_lookup_function(inst, name);
    if (!f || !wasm_runtime_call_wasm(env, f, argc, argv)) {
        printf("trap in %s: %s\n", name, wasm_runtime_get_exception(inst));
        wasm_runtime_clear_exception(inst);
        return 0;
    }
    return 1;
}

static int copy_out(uint32_t off, uint32_t len, void *dst) {
    if (!wasm_runtime_validate_app_addr(inst, off, len)) return 0;
    memcpy(dst, wasm_runtime_addr_app_to_native(inst, off), len);
    return 1;
}

static int copy_in(const char *buf_fn, const void *src, uint32_t len) {
    uint32_t a[1] = {0};
    if (!call(buf_fn, 0, a) || !wasm_runtime_validate_app_addr(inst, a[0], len)) return 0;
    memcpy(wasm_runtime_addr_app_to_native(inst, a[0]), src, len);
    return 1;
}

int parser_host_init(const uint8_t *wasm, uint32_t wasm_len, void *pool, uint32_t pool_size) {
    RuntimeInitArgs args;
    char err[128];
    wasm_module_t mod;

    memset(&args, 0, sizeof(args));
    args.mem_alloc_type = Alloc_With_Pool;
    args.mem_alloc_option.pool.heap_buf = pool;
    args.mem_alloc_option.pool.heap_size = pool_size;
    if (!wasm_runtime_full_init(&args)) return 0;
    /* classic interp はロード時にバイトコードを書き換えるので、呼び出し側は書き込み可能な RAM 上の wasm を渡す */
    if (!(mod = wasm_runtime_load((uint8_t *)wasm, wasm_len, err, sizeof(err)))
        || !(inst = wasm_runtime_instantiate(mod, 8192, 0, err, sizeof(err)))) {
        printf("parser.wasm: %s\n", err);
        return 0;
    }
    env = wasm_runtime_create_exec_env(inst, 8192);
    return env != NULL;
}

int parser_host_parse(const uint8_t *psbt, uint32_t len, uint32_t fingerprint, uint32_t *rc, plan_t *plan,
                      core_prevtx_t prev[PLAN_MAX_INPUTS], uint8_t *arena, size_t arena_cap) {
    uint32_t a[2] = {len, fingerprint};
    size_t used = 0;

    memset(prev, 0, sizeof(core_prevtx_t) * PLAN_MAX_INPUTS);
    if (len > PARSER_PSBT_MAX || !copy_in("parser_input", psbt, len) || !call("parser_parse", 2, a)) return 0;
    if ((*rc = a[0]) != 0) return 1;
    if (!call("parser_plan", 0, a) || !copy_out(a[0], sizeof(*plan), plan)) return 0;
    for (unsigned i = 0; i < plan->n_inputs && i < PLAN_MAX_INPUTS; i++) {
        uint32_t off[1] = {i}, n[1] = {i};
        if (!call("parser_prevtx_off", 1, off) || !call("parser_prevtx_len", 1, n)) return 0;
        if (!n[0]) continue;
        if (n[0] > arena_cap - used || !copy_out(off[0], n[0], arena + used)) return 0;
        prev[i].raw = arena + used, prev[i].len = n[0];
        used += n[0];
    }
    return 1;
}

int parser_host_finalize(const core_sig_t *sigs, unsigned n, uint8_t *out, size_t cap, uint32_t *out_len) {
    uint32_t a[1] = {n};
    if (n > PLAN_MAX_INPUTS || !copy_in("parser_sigs", sigs, (uint32_t)(sizeof(core_sig_t) * n))) return 0;
    if (!call("parser_finalize", 1, a) || (int32_t)a[0] < 0 || a[0] > cap) return 0;
    *out_len = a[0];
    return call("parser_output", 0, a) && copy_out(a[0], *out_len, out);
}

uint32_t parser_host_pool_highmark(void) {
    mem_alloc_info_t mi;
    wasm_runtime_get_mem_alloc_info(&mi);
    return mi.highmark_size;
}
