/* 実機アプリ。メニューから PSBT 署名の一巡を回す。確認画面を全て見てから承認すると署名し、
 * 署名済み PSBT をアニメーション QR（UR）で返して、メニューに戻る。
 * シードは電源が入っている間だけ RAM に置き、Lock で消す。Flash には何も書かない。
 * TEST_SEED=1 のビルドでは BIP39 のテストベクタを使える（警告画面を出す）。資金を扱ってはならない */
#include <stdio.h>
#include <stdlib.h>
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
/* 読み取り中の quirc（画像 77KB を含む 90KB）と同時には要らないので、署名の段だけヒープから取る */
#define SIGNED_PSBT_MAX (PARSER_PSBT_MAX + 2048)
static uint8_t *prevtx_arena, *signed_psbt;
static plan_t plan;
static ui_review_t review_ui;
static ui_menu_t menu;

/* NO_LCD=1 では液晶の代わりに UART へ画面の文字を出す（はんだ付け前の確認用） */
static void show(const ui_screen_t *s) {
    uint16_t line[UI_W];
#if NO_LCD
    printf("+--------------------------------+\n");
    for (int r = 0; r < UI_ROWS; r++)
        if (s->text[r][0]) printf("| %-30s |\n", s->text[r]);
    printf("+--------------------------------+\n");
    for (int y = 0; y < UI_H; y++) ui_render_line(s, y, line);
#else
    st7789_begin_frame();
    for (int y = 0; y < UI_H; y++) {
        ui_render_line(s, y, line);
        st7789_write_line(line);
    }
#endif
}

static int wait_key(void) {
    for (;;) {
        int key = buttons_poll();
        if (key >= 0) return key;
        sleep_ms(10);
    }
}

static void message(const char *title, const char *body, int warn) {
    static ui_screen_t s;
    ui_message(&s, title, body, warn);
    show(&s);
    printf("%s%s%s\n", title, body ? ": " : "", body ? body : "");
    wait_key();
}

/* 署名の段で使うバッファ。メニューに戻るときに返す（読み取りの段で quirc が同じ領域を使う） */
static int sign_buffers_take(void) {
    prevtx_arena = malloc(PARSER_PSBT_MAX);
    signed_psbt = malloc(SIGNED_PSBT_MAX);
    return prevtx_arena && signed_psbt;
}

static void sign_buffers_give(void) {
    free(prevtx_arena);
    free(signed_psbt);
    prevtx_arena = signed_psbt = NULL;
}

static int trng(uint8_t *buf, size_t len) {
    /* pico_rand は TRNG を種にするが暗号用 PRNG ではない。初期確認で Schnorr の aux にだけ使う */
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)get_rand_32();
    return 1;
}

/* 署名済み PSBT をアニメーション QR で返す。純粋なパートだけを周回させるので、混ぜたパートを
 * 使えない受信側でも完成でき、取りこぼしても次の周回で拾える。どのキーでも終える */
static void export_qr(uint32_t len) {
    long seq_len = parser_host_ur_encode_start(len, UI_UR_FRAGMENT);
    if (seq_len <= 0) return message("UR encode failed", NULL, 1);
    unsigned long part = 0;
    for (uint64_t next = 0; ;) {
        static char text[1024];
        if (buttons_poll() >= 0) return;
        if (time_us_64() >= next) {
            uint16_t line[UI_W];
            if (part && part % (unsigned long)seq_len == 0) parser_host_ur_encode_start(len, UI_UR_FRAGMENT);
            if (!parser_host_ur_encode_next(text, sizeof(text)) || !ui_qr_set(text))
                return message("QR failed", NULL, 1);
            if (part < (unsigned long)seq_len)
                printf("part %lu/%ld: %u chars, %d modules\n", part + 1, seq_len, (unsigned)strlen(text),
                       ui_qr_modules());
            part++;
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
            /* 250ms だとスマホがピントを合わせる前に切り替わり、パートを取りこぼす */
            next = time_us_64() + 500 * 1000;
        }
        sleep_ms(10);
    }
}

/* PSBT を 1 つ署名してメニューに戻る。カメラが付いたら、組み込みのテスト PSBT を UR の読み取りに差し替える */
static void sign_flow(void) {
    core_prevtx_t prev[PLAN_MAX_INPUTS];
    core_review_t review;
    core_display_t display;
    core_sig_t sigs[PLAN_MAX_INPUTS];
    uint32_t rc = 0, out_len = 0;
    unsigned n_sigs = 0;
    uint64_t t;
    int err, decision = UI_PENDING;

    if (!sign_buffers_take()) return sign_buffers_give(), message("Out of memory", NULL, 1);

    t = time_us_64();
    if (!parser_host_parse(test_psbt, sizeof(test_psbt), core_fingerprint(), &rc, &plan, prev, prevtx_arena,
                           PARSER_PSBT_MAX)
        || rc) {
        printf("parse failed rc=%u\n", (unsigned)rc);
        return sign_buffers_give(), message("Invalid PSBT", NULL, 1);
    }
    printf("parse %llu us, pool highmark %u\n", (unsigned long long)(time_us_64() - t),
           (unsigned)parser_host_pool_highmark());

    t = time_us_64();
    if ((err = core_review(&plan, prev, &review)) != CORE_OK
        || (err = core_display(&plan, &review, &display)) != CORE_OK) {
        printf("review err=%d\n", err);
        return sign_buffers_give(), message("Rejected by review", NULL, 1);
    }
    printf("review %llu us\n", (unsigned long long)(time_us_64() - t));

    ui_review_init(&review_ui, &display);
    show(&review_ui.screens[0]);
    while (decision == UI_PENDING) {
        int key = buttons_poll();
        if (key >= 0) {
            unsigned before = review_ui.cur;
            decision = ui_review_key(&review_ui, key);
            if (review_ui.cur != before) show(&review_ui.screens[review_ui.cur]);
        }
        sleep_ms(10);
    }
    if (decision == UI_REJECTED) return sign_buffers_give(), message("Canceled", NULL, 0);

    t = time_us_64();
    if ((err = core_sign(&plan, trng, sigs, &n_sigs)) != CORE_OK) {
        printf("sign err=%d\n", err);
        return sign_buffers_give(), message("Sign failed", NULL, 1);
    }
    printf("sign %llu us (%u inputs)\n", (unsigned long long)(time_us_64() - t), n_sigs);
    if (!parser_host_finalize(sigs, n_sigs, signed_psbt, SIGNED_PSBT_MAX, &out_len))
        return sign_buffers_give(), message("Finalize failed", NULL, 1);

    printf("signed psbt (%u bytes):\n", (unsigned)out_len);
    for (uint32_t i = 0; i < out_len; i++) printf("%02x%s", signed_psbt[i], (i % 32 == 31) ? "\n" : "");
    printf("\n");
    export_qr(out_len);
    sign_buffers_give();
}

#if TEST_SEED
static int load_test_seed(void) {
    uint8_t seed[64];
    uint64_t t = time_us_64();
    int ok;

    message("TEST SEED", "Public BIP39 vector. Never send funds to this wallet.", 1);
    pbkdf2_hmac_sha512((const uint8_t *)TEST_MNEMONIC, sizeof(TEST_MNEMONIC) - 1, (const uint8_t *)"mnemonic", 8,
                       2048, seed);
    ok = core_load_seed(seed);
    memset(seed, 0, sizeof(seed));
    printf("seed %llu us, fingerprint %08x\n", (unsigned long long)(time_us_64() - t), (unsigned)core_fingerprint());
    return ok;
}
#endif

static void seed_menu(void) {
    static const char *const items[] = {"Scan SeedQR", "Use test seed"};
    int sel;

    ui_menu_init(&menu, "No seed loaded", items, TEST_SEED ? 2 : 1);
    show(&menu.screen);
    for (;;) {
        sel = ui_menu_key(&menu, wait_key());
        if (sel == UI_MENU_PENDING) {
            show(&menu.screen);
            continue;
        }
        if (sel == 0) {
            message("Not implemented", "SeedQR needs the camera.", 0);
            show(&menu.screen);
            continue;
        }
#if TEST_SEED
        if (sel == 1 && load_test_seed()) return;
#endif
        show(&menu.screen);
    }
}

static void main_menu(void) {
    static const char *const items[] = {"Sign PSBT", "Lock (wipe seed)"};
    static char title[UI_COLS + 1];

    snprintf(title, sizeof(title), "Signer  fp %08x", (unsigned)core_fingerprint());
    ui_menu_init(&menu, title, items, 2);
    show(&menu.screen);
    for (;;) {
        int sel = ui_menu_key(&menu, wait_key());
        if (sel == 0) sign_flow();
        if (sel == 1) {
            core_unload();
            printf("locked (seed wiped)\n");
            return;
        }
        show(&menu.screen);
    }
}

int main(void) {
    stdio_init_all();
#if !NO_LCD
    st7789_init();
#endif
    buttons_init();
    printf("\nbaremetal-wasm-signer\n");

    memcpy(parser_wasm_rw, parser_wasm, sizeof(parser_wasm_rw));
    if (!core_init(CORE_MAINNET) || !parser_host_init(parser_wasm_rw, sizeof(parser_wasm_rw), pool, sizeof(pool))) {
        message("Init failed", NULL, 1);
        return 1;
    }
    for (;;) {
        seed_menu();
        main_menu();
    }
}
