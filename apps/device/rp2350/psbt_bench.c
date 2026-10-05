/* Times a full PSBT round on the hardware. No screen and no buttons: a built-in test PSBT goes
 * through parser.wasm, core and parser.wasm again, and the timings go to the UART. Used to compare
 * the interpreter against AOT. The seed is BIP39's test vector (abandon ... about), so this build
 * must never hold funds */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "core.h"
#include "parser_host.h"
#include "sha512.h"
#include "ui.h"
#include "quirc.h"
#include "parser_wasm.h"
#include "test_psbt.h"

#define TEST_MNEMONIC "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"

#ifndef POOL_KB
#define POOL_KB 256
#endif
static char pool[POOL_KB * 1024];
#if WASM_ENABLE_INTERP != 0
#define MODE "classic interp"
/* The classic interpreter rewrites the bytecode on load, so it cannot be handed the copy in flash */
static uint8_t parser_wasm_rw[sizeof(parser_wasm)];
#else
#define MODE "AOT"
#endif
/* Uses the same heap as quirc, one after the other, since the two are never needed at once. What this
 * is checking is that taking the memory back after releasing quirc keeps the peak at the larger of
 * the two rather than their sum */
#define SIGNED_PSBT_MAX (PARSER_PSBT_MAX + 2048)
static uint8_t *prevtx_arena, *signed_psbt;
static plan_t plan;

/* Schnorr aux is fixed at zero so the signatures can be compared between device and host */
static int zero_rng(uint8_t *buf, size_t len) {
    return memset(buf, 0, len), 1;
}

int main(void) {
    core_prevtx_t prev[PLAN_MAX_INPUTS];
    core_review_t review;
    core_display_t display;
    core_sig_t sigs[PLAN_MAX_INPUTS];
    uint8_t seed[64];
    uint32_t rc = 0, out_len = 0;
    unsigned n_sigs = 0;
    uint64_t t, ur_total, worst = 0;
    const uint8_t *image = parser_wasm;
    int err;

    stdio_init_all();
    printf("\npsbt_bench: TEST SEED ONLY (" MODE ", pool %u KB)\n", (unsigned)POOL_KB);

#if WASM_ENABLE_INTERP != 0
    memcpy(parser_wasm_rw, parser_wasm, sizeof(parser_wasm_rw));
    image = parser_wasm_rw;
#endif
    /* The reading step, where quirc takes the camera image and its working memory */
    struct quirc *q = quirc_new();
    if (!q || quirc_resize(q, 320, 240) < 0) return printf("quirc failed\n"), 1;
    printf("scan phase: quirc heap %d B (arena %d B)\n", mallinfo().uordblks, mallinfo().arena);
    quirc_destroy(q);

    /* The parse-and-sign step, taking the PSBT buffers from that same heap */
    prevtx_arena = malloc(PARSER_PSBT_MAX);
    signed_psbt = malloc(SIGNED_PSBT_MAX);
    if (!prevtx_arena || !signed_psbt) return printf("psbt buffers failed\n"), 1;
    printf("sign phase: psbt buffers %d B (arena %d B)\n", mallinfo().uordblks, mallinfo().arena);

    t = time_us_64();
    if (!core_init(CORE_MAINNET) || !parser_host_init(image, sizeof(parser_wasm), pool, sizeof(pool)))
        return printf("init failed\n"), 1;
    printf("init %llu us (image %u B)\n", (unsigned long long)(time_us_64() - t), (unsigned)sizeof(parser_wasm));

    t = time_us_64();
    pbkdf2_hmac_sha512((const uint8_t *)TEST_MNEMONIC, sizeof(TEST_MNEMONIC) - 1, (const uint8_t *)"mnemonic", 8, 2048,
                       seed);
    core_load_seed(seed);
    memset(seed, 0, sizeof(seed));
    printf("seed %llu us (native)\n", (unsigned long long)(time_us_64() - t));

    t = time_us_64();
    if (!parser_host_parse(test_psbt, sizeof(test_psbt), core_fingerprint(), &rc, &plan, prev, prevtx_arena,
                           PARSER_PSBT_MAX) ||
        rc)
        return printf("parse failed rc=%u\n", (unsigned)rc), 1;
    printf("parse %llu us (wasm)\n", (unsigned long long)(time_us_64() - t));

    t = time_us_64();
    if ((err = core_review(&plan, prev, &review)) != CORE_OK ||
        (err = core_display(&plan, &review, &display)) != CORE_OK)
        return printf("review err=%d\n", err), 1;
    printf("review %llu us (native)\n", (unsigned long long)(time_us_64() - t));

    t = time_us_64();
    if ((err = core_sign(&plan, zero_rng, sigs, &n_sigs)) != CORE_OK) return printf("sign err=%d\n", err), 1;
    printf("sign %llu us (native, %u inputs)\n", (unsigned long long)(time_us_64() - t), n_sigs);

    t = time_us_64();
    if (!parser_host_finalize(sigs, n_sigs, signed_psbt, SIGNED_PSBT_MAX, &out_len))
        return printf("finalize failed\n"), 1;
    core_unload();
    printf("finalize %llu us (wasm)\n", (unsigned long long)(time_us_64() - t));

    /* Encoding and QR building, once per pure part. The worst single part sets the display interval */
    ur_total = time_us_64();
    long parts = parser_host_ur_encode_start(out_len, UI_UR_FRAGMENT);
    if (parts <= 0) return printf("ur encode failed\n"), 1;
    for (long i = 0; i < parts; i++) {
        static char text[1024];
        uint16_t line[UI_W];
        t = time_us_64();
        if (!parser_host_ur_encode_next(text, sizeof(text)) || !ui_qr_set(text)) return printf("qr failed\n"), 1;
        for (int y = 0; y < UI_H; y++) ui_qr_render_line(y, line);
        t = time_us_64() - t;
        if (t > worst) worst = t;
    }
    printf("ur+qr %llu us total for %ld parts, worst part %llu us\n", (unsigned long long)(time_us_64() - ur_total),
           parts, (unsigned long long)worst);

    printf("signed %u bytes, first 32: ", (unsigned)out_len);
    for (int i = 0; i < 32; i++) printf("%02x", signed_psbt[i]);
    printf("\npool_highmark %u\nheap arena %d B (%d B if not shared)\ndone\n", (unsigned)parser_host_pool_highmark(),
           mallinfo().arena, 91648 + PARSER_PSBT_MAX + SIGNED_PSBT_MAX);
    while (1) tight_loop_contents();
}
