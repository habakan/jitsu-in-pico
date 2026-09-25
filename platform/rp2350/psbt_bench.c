/* 実機で PSBT 一巡の時間を測る。表示もボタンも使わず、組み込んだテスト用 PSBT を
 * parser.wasm → core → parser.wasm と通して UART に時間を出す。インタプリタと AOT の比較用。
 * seed は BIP39 のテストベクタ（abandon ... about）で、資金を扱ってはならない */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "core.h"
#include "parser_host.h"
#include "sha512.h"
#include "ui.h"
#include "parser_wasm.h"
#include "test_psbt.h"

#define TEST_MNEMONIC "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"

#ifndef POOL_KB
#define POOL_KB 256
#endif
static char pool[POOL_KB * 1024];
#if WASM_ENABLE_INTERP != 0
#define MODE "classic interp"
/* classic interp はロード時にバイトコードを書き換えるので Flash のままでは渡せない */
static uint8_t parser_wasm_rw[sizeof(parser_wasm)];
#else
#define MODE "AOT"
#endif
static uint8_t prevtx_arena[PARSER_PSBT_MAX];
static uint8_t signed_psbt[PARSER_PSBT_MAX + 2048];
static plan_t plan;

/* 署名を実機とホストで比べられるよう、Schnorr の aux は 0 固定にする */
static int zero_rng(uint8_t *buf, size_t len) { return memset(buf, 0, len), 1; }

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
    t = time_us_64();
    if (!core_init(CORE_MAINNET) || !parser_host_init(image, sizeof(parser_wasm), pool, sizeof(pool)))
        return printf("init failed\n"), 1;
    printf("init %llu us (image %u B)\n", (unsigned long long)(time_us_64() - t), (unsigned)sizeof(parser_wasm));

    t = time_us_64();
    pbkdf2_hmac_sha512((const uint8_t *)TEST_MNEMONIC, sizeof(TEST_MNEMONIC) - 1, (const uint8_t *)"mnemonic", 8,
                       2048, seed);
    core_load_seed(seed);
    memset(seed, 0, sizeof(seed));
    printf("seed %llu us (native)\n", (unsigned long long)(time_us_64() - t));

    t = time_us_64();
    if (!parser_host_parse(test_psbt, sizeof(test_psbt), core_fingerprint(), &rc, &plan, prev, prevtx_arena,
                           sizeof(prevtx_arena)) || rc)
        return printf("parse failed rc=%u\n", (unsigned)rc), 1;
    printf("parse %llu us (wasm)\n", (unsigned long long)(time_us_64() - t));

    t = time_us_64();
    if ((err = core_review(&plan, prev, &review)) != CORE_OK || (err = core_display(&plan, &review, &display)) != CORE_OK)
        return printf("review err=%d\n", err), 1;
    printf("review %llu us (native)\n", (unsigned long long)(time_us_64() - t));

    t = time_us_64();
    if ((err = core_sign(&plan, zero_rng, sigs, &n_sigs)) != CORE_OK) return printf("sign err=%d\n", err), 1;
    printf("sign %llu us (native, %u inputs)\n", (unsigned long long)(time_us_64() - t), n_sigs);

    t = time_us_64();
    if (!parser_host_finalize(sigs, n_sigs, signed_psbt, sizeof(signed_psbt), &out_len))
        return printf("finalize failed\n"), 1;
    core_unload();
    printf("finalize %llu us (wasm)\n", (unsigned long long)(time_us_64() - t));

    /* 符号化と QR の組み立てを純粋なパートの分だけ回す。1 パートの最悪値が表示の間隔を決める */
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
    printf("ur+qr %llu us total for %ld parts, worst part %llu us\n",
           (unsigned long long)(time_us_64() - ur_total), parts, (unsigned long long)worst);

    printf("signed %u bytes, first 32: ", (unsigned)out_len);
    for (int i = 0; i < 32; i++) printf("%02x", signed_psbt[i]);
    printf("\npool_highmark %u\ndone\n", (unsigned)parser_host_pool_highmark());
    while (1) tight_loop_contents();
}
