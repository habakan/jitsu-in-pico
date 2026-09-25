/* 実機の初期確認用アプリ。組み込んだテスト用 PSBT を parser.wasm と core に通し、LCD の確認画面を
 * ボタンで全て見てから承認すると署名し、署名済み PSBT をアニメーション QR で返す（UART にも 16 進で出す）。
 * seed は BIP39 のテストベクタ（abandon ... about）で、資金を扱ってはならない */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/rand.h"
#include "core.h"
#include "parser_host.h"
#include "sha512.h"
#include "ui.h"
#include "st7789.h"
#include "buttons.h"
#include "parser_wasm.h"
#include "test_psbt.h"

#define TEST_MNEMONIC "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"

/* parser.wasm の線形メモリ（UR の復元と符号化込みで約 132KB）もこのプールから取られる。QEMU の実測で最大 149KB */
static char pool[160 * 1024];
static uint8_t parser_wasm_rw[sizeof(parser_wasm)];
static uint8_t prevtx_arena[PARSER_PSBT_MAX];
static uint8_t signed_psbt[PARSER_PSBT_MAX + 2048];
static plan_t plan;
static ui_review_t ui;

/* NO_LCD=1 では液晶の代わりに UART へ画面の文字を出す（はんだ付け前の確認用） */
static void show(const ui_screen_t *s) {
    uint16_t line[UI_W];
#if NO_LCD
    printf("+------------------------------+\n");
    for (int r = 0; r < UI_ROWS; r++)
        if (s->text[r][0]) printf("| %-28s |\n", s->text[r]);
    printf("+------------------------------+\n");
    for (int y = 0; y < UI_H; y++) ui_render_line(s, y, line);
#else
    st7789_begin_frame();
    for (int y = 0; y < UI_H; y++) {
        ui_render_line(s, y, line);
        st7789_write_line(line);
    }
#endif
}

static void show_message(const char *title, const char *body) {
    static ui_screen_t s;
    memset(&s, 0, sizeof(s));
    strncpy(s.text[0], title, UI_COLS);
    strncpy(s.text[2], body, UI_COLS);
    s.color[0] = 0xfd40, s.color[2] = 0xe71c;
    show(&s);
}

static int trng(uint8_t *buf, size_t len) {
    /* pico_rand は TRNG を種にするが暗号用 PRNG ではない。初期確認で Schnorr の aux にだけ使う */
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)get_rand_32();
    return 1;
}

int main(void) {
    core_prevtx_t prev[PLAN_MAX_INPUTS];
    core_review_t review;
    core_display_t display;
    core_sig_t sigs[PLAN_MAX_INPUTS];
    uint8_t seed[64];
    uint32_t rc = 0, out_len = 0;
    unsigned n_sigs = 0;
    uint64_t t;
    int err, decision = UI_PENDING;

    stdio_init_all();
#if !NO_LCD
    st7789_init();
#endif
    buttons_init();
    show_message("baremetal-wasm-signer", "starting...");
    printf("\nbaremetal-wasm-signer: TEST SEED ONLY\n");

    memcpy(parser_wasm_rw, parser_wasm, sizeof(parser_wasm_rw));
    if (!core_init(CORE_MAINNET) || !parser_host_init(parser_wasm_rw, sizeof(parser_wasm_rw), pool, sizeof(pool)))
        return show_message("init failed", ""), 1;

    t = time_us_64();
    pbkdf2_hmac_sha512((const uint8_t *)TEST_MNEMONIC, sizeof(TEST_MNEMONIC) - 1, (const uint8_t *)"mnemonic", 8,
                       2048, seed);
    core_load_seed(seed);
    memset(seed, 0, sizeof(seed));
    printf("seed (pbkdf2 + master): %llu us\n", (unsigned long long)(time_us_64() - t));

    t = time_us_64();
    if (!parser_host_parse(test_psbt, sizeof(test_psbt), core_fingerprint(), &rc, &plan, prev, prevtx_arena,
                           sizeof(prevtx_arena)) || rc)
        return printf("parse failed rc=%u\n", (unsigned)rc), show_message("parse failed", ""), 1;
    printf("parse (parser.wasm): %llu us, pool highmark %u\n", (unsigned long long)(time_us_64() - t),
           (unsigned)parser_host_pool_highmark());

    t = time_us_64();
    if ((err = core_review(&plan, prev, &review)) != CORE_OK || (err = core_display(&plan, &review, &display)) != CORE_OK)
        return printf("review err=%d\n", err), show_message("review failed", ""), 1;
    printf("review: %llu us\n", (unsigned long long)(time_us_64() - t));

    ui_review_init(&ui, &display);
    show(&ui.screens[0]);
    while (decision == UI_PENDING) {
        int key = buttons_poll();
        if (key >= 0) {
            unsigned before = ui.cur;
            decision = ui_review_key(&ui, key);
            if (ui.cur != before) show(&ui.screens[ui.cur]);
        }
        sleep_ms(10);
    }
    if (decision == UI_REJECTED) {
        core_unload();
        show_message("Canceled", "seed wiped");
        printf("canceled\n");
        return 0;
    }

    t = time_us_64();
    if ((err = core_sign(&plan, trng, sigs, &n_sigs)) != CORE_OK)
        return printf("sign err=%d\n", err), show_message("sign failed", ""), 1;
    printf("sign: %llu us (%u inputs)\n", (unsigned long long)(time_us_64() - t), n_sigs);
    if (!parser_host_finalize(sigs, n_sigs, signed_psbt, sizeof(signed_psbt), &out_len))
        return show_message("finalize failed", ""), 1;
    core_unload();
    printf("signed psbt (%u bytes):\n", (unsigned)out_len);
    for (uint32_t i = 0; i < out_len; i++) printf("%02x%s", signed_psbt[i], (i % 32 == 31) ? "\n" : "");
    printf("\n");

    /* 署名済み PSBT をアニメーション QR（UR）で返す。純粋なパートの後は混ぜたパートが続くので、
     * ウォレットがいくつか取りこぼしても復元できる。A で終える */
    if (parser_host_ur_encode_start(out_len, UI_UR_FRAGMENT) <= 0) return show_message("ur encode failed", ""), 1;
    for (uint64_t next = 0;;) {
        static char text[1024];
        if (buttons_poll() == UI_KEY_A) break;
        if (time_us_64() >= next) {
            uint16_t line[UI_W];
            if (!parser_host_ur_encode_next(text, sizeof(text)) || !ui_qr_set(text)) return show_message("qr failed", ""), 1;
#if NO_LCD
            printf("%s\n", text);
            for (int y = 0; y < UI_H; y++) ui_qr_render_line(y, line);
#else
            st7789_begin_frame();
            for (int y = 0; y < UI_H; y++) {
                ui_qr_render_line(y, line);
                st7789_write_line(line);
            }
#endif
            next = time_us_64() + 250 * 1000;
        }
        sleep_ms(10);
    }
    show_message("Done", "");
    return 0;
}
