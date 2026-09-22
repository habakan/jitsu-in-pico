/* parser.wasm → core → parser.wasm を一巡させるホスト。parse だけの検査と、署名までの一巡の 2 モード */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"
#include "parser_host.h"
#include "ui.h"
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

#define MNEMONIC "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"

static char pool[256 * 1024];
/* classic interp がロード時に書き換えるので、書き込み可能なコピーを渡す */
static uint8_t parser_wasm_rw[sizeof(parser_wasm)];
static uint8_t file_buf[PARSER_PSBT_MAX + 1];
static uint8_t prevtx_arena[PARSER_PSBT_MAX];

static long read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    long n;
    if (!f) return -1;
    n = (long)fread(file_buf, 1, sizeof(file_buf), f);
    fclose(f);
    return n;
}

/* 確認画面を 240x240 の PPM に書き出す（実機が無くてもレイアウトを確かめるため） */
static void write_screens(const core_display_t *d, const char *prefix) {
    static ui_review_t ui;
    uint16_t line[UI_W];
    char path[256];
    ui_review_init(&ui, d);
    for (unsigned i = 0; i < ui.n; i++) {
        FILE *f;
        snprintf(path, sizeof(path), "%s_%02u.ppm", prefix, i);
        if (!(f = fopen(path, "wb"))) return;
        fprintf(f, "P6 %d %d 255\n", UI_W, UI_H);
        for (int y = 0; y < UI_H; y++) {
            ui_render_line(&ui.screens[i], y, line);
            for (int x = 0; x < UI_W; x++) {
                uint8_t rgb[3] = {(uint8_t)((line[x] >> 11) << 3), (uint8_t)(((line[x] >> 5) & 63) << 2),
                                  (uint8_t)((line[x] & 31) << 3)};
                fwrite(rgb, 1, 3, f);
            }
        }
        fclose(f);
    }
}

static int zero_rng(uint8_t *b, size_t n) { memset(b, 0, n); return 1; } /* 検査用。実機は TRNG */

static int sign(const char *in_path, const char *out_path, const char *preview) {
    static plan_t plan;
    core_prevtx_t prev[PLAN_MAX_INPUTS];
    core_review_t r;
    core_display_t d;
    core_sig_t sigs[PLAN_MAX_INPUTS];
    unsigned n_sigs;
    uint32_t rc = 0, out_len;
    uint64_t t0 = now(), t1, t2, t3, t4;
    long len = read_file(in_path);
    char btc[21];
    int err;
    FILE *f;

    if (len < 0 || !parser_host_parse(file_buf, (uint32_t)len, core_fingerprint(), &rc, &plan, prev, prevtx_arena,
                                      sizeof(prevtx_arena)) || rc)
        return printf("parse rc=%u\n", (unsigned)rc), 1;
    t1 = now();
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
    if (preview) write_screens(&d, preview);
    if ((err = core_sign(&plan, zero_rng, sigs, &n_sigs)) != CORE_OK) return printf("sign err=%d\n", err), 1;
    t3 = now();
    if (!parser_host_finalize(sigs, n_sigs, file_buf, sizeof(file_buf), &out_len)) return printf("finalize\n"), 1;
    t4 = now();
    if (!(f = fopen(out_path, "wb"))) return 1;
    fwrite(file_buf, 1, out_len, f);
    fclose(f);
    printf("  signed %u input(s); %s parse=%llu review=%llu sign=%llu finalize=%llu; pool_highmark=%u\n", n_sigs,
           UNIT, (unsigned long long)(t1 - t0), (unsigned long long)(t2 - t1), (unsigned long long)(t3 - t2),
           (unsigned long long)(t4 - t3), (unsigned)parser_host_pool_highmark());
    return 0;
}

int main(int argc, char **argv) {
    uint8_t seed[64];

#ifdef QEMU_BUILD
    /* libgloss の crt0 は semihosting のコマンドラインを渡さないので固定する */
    static char *qemu_argv[] = {"psbt_host", "sign", "build/psbt/own_mixed_nwu.psbt", "build/psbt/own_mixed_nwu.qemu"};
    argc = 4, argv = qemu_argv;
#endif
    if (argc < 3) return fprintf(stderr, "usage: %s parse FILE... | sign IN OUT [PREVIEW_PREFIX]\n", argv[0]), 2;
    memcpy(parser_wasm_rw, parser_wasm, sizeof(parser_wasm_rw));
    if (!core_init(CORE_MAINNET) || !parser_host_init(parser_wasm_rw, sizeof(parser_wasm_rw), pool, sizeof(pool)))
        return 1;
    pbkdf2_hmac_sha512((const uint8_t *)MNEMONIC, sizeof(MNEMONIC) - 1, (const uint8_t *)"mnemonic", 8, 2048, seed);
    if (!core_load_seed(seed)) return 1;

    if (!strcmp(argv[1], "sign")) return argc >= 4 ? sign(argv[2], argv[3], argc > 4 ? argv[4] : NULL) : 2;
    for (int i = 2; i < argc; i++) {
        static plan_t plan;
        core_prevtx_t prev[PLAN_MAX_INPUTS];
        uint32_t rc = 0;
        long len = read_file(argv[i]);
        int ok = len >= 0 && parser_host_parse(file_buf, (uint32_t)len, core_fingerprint(), &rc, &plan, prev,
                                               prevtx_arena, sizeof(prevtx_arena));
        printf("%s %s\n", argv[i], ok ? (rc ? "rejected" : "accepted") : "trap");
        if (ok && rc) printf("  rc=%u\n", (unsigned)rc);
    }
    return 0;
}
