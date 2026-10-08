/* The device application. The menu runs one signing round: read a PSBT, show every review screen,
 * and on approval sign it and hand the result back as an animated QR, then return to the menu.
 * The seed lives in RAM only while the power is on, and Lock clears it. Nothing is ever written to
 * flash. A TEST_SEED=1 build can load BIP39's test vector, and shows a warning screen for it; such a
 * build must never hold funds */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/rand.h"
#include "core.h"
#include "parser_host.h"
#include "sha256.h"
#include "sha512.h"
#include "wipe.h"
#include "ui.h"
#include "st7789.h"
#include "buttons.h"
#include "camera.h"
#include "quirc.h"
#include "seedqr.h"
#include "parser_wasm.h"
#include "test_psbt.h"

#define TEST_MNEMONIC "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"

/* parser.wasm's linear memory comes out of this pool too, about 132KB with UR reassembly and
 * encoding. Measured at 149KB peak under QEMU */
static char pool[160 * 1024];
static uint8_t parser_wasm_rw[sizeof(parser_wasm)];
/* Not needed at the same time as quirc while reading (90KB, 77KB of it the image), so this is taken
 * from the heap only for the signing step */
#define SIGNED_PSBT_MAX (PARSER_PSBT_MAX + 2048)
static uint8_t *prevtx_arena, *signed_psbt, *psbt;
static int camera_ready;
static plan_t plan;
static ui_review_t review_ui;
static ui_menu_t menu;

/* With NO_LCD=1 the screens' text goes to the UART instead of the panel, which is how this was
 * checked before anything was soldered */
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

/* The buffers the signing step uses, released on the way back to the menu because quirc takes the
 * same memory while reading */
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

/* Shows the captured frame, keeping a band at the top and bottom, with the status on the first line,
 * so the camera can actually be aimed */
static void show_preview(const uint8_t *img, const char *status) {
#if NO_LCD
    (void)img;
    printf("%s\n", status);
#else
    static ui_screen_t s;
    uint16_t line[UI_W];

    memset(&s, 0, sizeof(s));
    strncpy(s.text[0], status, UI_COLS);
    s.color[0] = 0xfd40;
    st7789_begin_frame();
    for (int y = 0; y < UI_H; y++) {
        if (y < 16) {
            ui_render_line(&s, y, line);
        } else if (y >= 30 && y < 210) { /* 320x240 decimated to three quarters, shown as 240x180 */
            int sy = (y - 30) * 4 / 3;
            for (int x = 0; x < UI_W; x++) {
                uint8_t v = img[sy * CAMERA_W + x * 4 / 3];
                line[x] = (uint16_t)((v >> 3) << 11 | (v >> 2) << 5 | (v >> 3));
            }
        } else {
            memset(line, 0, sizeof(line));
        }
        st7789_write_line(line);
    }
#endif
}

/* Reads an animated QR and reassembles the PSBT. Returns its length, or 0 if cancelled or failed */
static uint32_t scan_psbt(void) {
    struct quirc *q;
    unsigned parts = 0;
    uint64_t seen = 0; /* which parts have arrived, so a repeat is not counted twice */
    uint32_t len = 0;
    char status[UI_COLS + 1];

    if (!camera_ready) return message("No camera", "Wire it up or use the test PSBT.", 1), 0;
    if (!(psbt = malloc(PARSER_PSBT_MAX))) return message("Out of memory", NULL, 1), 0;
    if (!(q = quirc_new()) || quirc_resize(q, CAMERA_W, CAMERA_H) < 0) {
        free(psbt), psbt = NULL;
        return message("Out of memory", NULL, 1), 0;
    }
    parser_host_ur_reset();
    snprintf(status, sizeof(status), "Scan PSBT: aim at the QR");

    while (!len) {
        uint8_t *img = quirc_begin(q, NULL, NULL); /* captured straight into quirc's image buffer */
        int found = 0;
        if (buttons_poll() >= 0) break; /* any key cancels */
        if (!camera_capture(img, 500)) continue;
        show_preview(img, status);
        quirc_end(q);
        for (int i = 0; i < quirc_count(q) && !len; i++) {
            static struct quirc_code code;
            static struct quirc_data data;
            int32_t rc = 0;
            unsigned seq = 0, seq_len = 0;
            quirc_extract(q, i, &code);
            found = 1;
            if (quirc_decode(&code, &data)) {
                snprintf(status, sizeof(status), "QR found, cannot read");
                continue;
            }
            /* n and m out of "UR:CRYPTO-PSBT/<n>-<m>/", to show how many of how many */
            for (const char *c = (const char *)data.payload; *c; c++)
                if (*c == '/') {
                    while (*++c >= '0' && *c <= '9') seq = seq * 10 + (unsigned)(*c - '0');
                    if (*c == '-')
                        while (*++c >= '0' && *c <= '9') seq_len = seq_len * 10 + (unsigned)(*c - '0');
                    break;
                }
            if (!parser_host_ur_receive((const char *)data.payload, (uint32_t)data.payload_len, &rc, psbt,
                                        PARSER_PSBT_MAX)) {
                snprintf(status, sizeof(status), "QR read, not a PSBT");
                continue;
            }
            if (rc > 0) {
                len = (uint32_t)rc;
            } else if (rc == 0) {
                if (seq && seq <= 64 && !(seen >> (seq - 1) & 1)) seen |= (uint64_t)1 << (seq - 1), parts++;
                if (seq_len) snprintf(status, sizeof(status), "Scan PSBT  %u/%u parts", parts, seq_len);
                else snprintf(status, sizeof(status), "Scan PSBT  %u parts", parts);
            } else {
                snprintf(status, sizeof(status), "UR error %d", (int)rc);
            }
        }
        if (!found && parts == 0) snprintf(status, sizeof(status), "Scan PSBT: no QR in view");
    }
    quirc_destroy(q);
    if (!len) free(psbt), psbt = NULL;
    printf("scan: %u parts, psbt %u bytes\n", parts, (unsigned)len);
    return len;
}

static int trng(uint8_t *buf, size_t len) {
    /* pico_rand is seeded from the TRNG but is not a cryptographic PRNG. It is used only as Schnorr
     * aux data during the initial bring-up */
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)get_rand_32();
    return 1;
}

static void draw_qr(void) {
    uint16_t line[UI_W];
#if NO_LCD
    for (int y = 0; y < UI_H; y++) ui_qr_render_line(y, line);
#else
    st7789_begin_frame();
    for (int y = 0; y < UI_H; y++) {
        ui_qr_render_line(y, line);
        st7789_write_line(line);
    }
#endif
}

/* Hands the signed PSBT back as an animated QR. Only pure parts are cycled, so a receiver that
 * cannot use mixed parts still completes, and a missed part comes round again. Any key ends it */
static void export_qr(uint32_t len) {
    long seq_len = parser_host_ur_encode_start(len, UI_UR_FRAGMENT);
    if (seq_len <= 0) return message("UR encode failed", NULL, 1);
    unsigned long part = 0;
    for (uint64_t next = 0;;) {
        static char text[1024];
        int key = buttons_poll();
        if (key == UI_KEY_UP || key == UI_KEY_DOWN) {
            ui_qr_level(key == UI_KEY_UP ? 1 : -1);
            printf("qr level %d\n", ui_qr_level_get());
            draw_qr();
        } else if (key >= 0) {
            return;
        }
        if (time_us_64() >= next) {
            if (part && part % (unsigned long)seq_len == 0) parser_host_ur_encode_start(len, UI_UR_FRAGMENT);
            if (!parser_host_ur_encode_next(text, sizeof(text)) || !ui_qr_set(text))
                return message("QR failed", NULL, 1);
            part++;
            /* also on the UART, for when the camera cannot read the screen */
            printf("%s\n", text);
            draw_qr();
            /* at 250ms a phone changes part before it has focused, and parts get missed */
            next = time_us_64() + 500 * 1000;
        }
        sleep_ms(10);
    }
}

/* Signs one PSBT and returns to the menu. psbt and psbt_len are what was read */
static void sign_flow(const uint8_t *in, uint32_t in_len) {
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
    if (!parser_host_parse(in, in_len, core_fingerprint(), &rc, &plan, prev, prevtx_arena, PARSER_PSBT_MAX) || rc) {
        printf("parse failed rc=%u\n", (unsigned)rc);
        return sign_buffers_give(), message("Invalid PSBT", NULL, 1);
    }
    printf("parse %llu us, pool highmark %u\n", (unsigned long long)(time_us_64() - t),
           (unsigned)parser_host_pool_highmark());

    t = time_us_64();
    if ((err = core_review(&plan, prev, &review)) != CORE_OK ||
        (err = core_display(&plan, &review, &display)) != CORE_OK) {
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

static void scan_and_sign(void) {
    uint32_t len = scan_psbt();
    if (!len) return;
    sign_flow(psbt, len);
    free(psbt), psbt = NULL;
}

/* Reads a SeedQR and loads the seed. It is the secret itself, so it is handled here and never goes
 * through the parser */
static int scan_seed(void) {
    struct quirc *q;
    char status[UI_COLS + 1], mnemonic[256];
    uint8_t seed[64];
    /* The payload is the seed. Too large for the stack, so it is static and wiped at the end */
    static struct quirc_code code;
    static struct quirc_data data;
    int ok = 0;

    if (!camera_ready) return message("No camera", "Wire it up or use the test seed.", 1), 0;
    if (!(q = quirc_new()) || quirc_resize(q, CAMERA_W, CAMERA_H) < 0) return message("Out of memory", NULL, 1), 0;
    snprintf(status, sizeof(status), "Scan SeedQR");

    while (!ok) {
        uint8_t *img = quirc_begin(q, NULL, NULL);
        if (buttons_poll() >= 0) break;
        if (!camera_capture(img, 500)) continue;
        show_preview(img, status);
        quirc_end(q);
        for (int i = 0; i < quirc_count(q) && !ok; i++) {
            quirc_extract(q, i, &code);
            if (quirc_decode(&code, &data)) {
                snprintf(status, sizeof(status), "QR found, cannot read");
                continue;
            }
            if (!seedqr_decode(data.payload, (size_t)data.payload_len, mnemonic, sizeof(mnemonic))) {
                snprintf(status, sizeof(status), "Not a valid SeedQR");
                continue;
            }
            pbkdf2_hmac_sha512((const uint8_t *)mnemonic, strlen(mnemonic), (const uint8_t *)"mnemonic", 8, 2048, seed);
            ok = core_load_seed(seed);
        }
    }
    /* The captured frame still holds the SeedQR, readable over SWD, so it is wiped before release */
    wipe(quirc_begin(q, NULL, NULL), (size_t)CAMERA_W * CAMERA_H);
    quirc_destroy(q);
    wipe(&code, sizeof(code));
    wipe(&data, sizeof(data));
    wipe(mnemonic, sizeof(mnemonic));
    wipe(seed, sizeof(seed));
    if (ok) printf("seed from SeedQR, fingerprint %08x\n", (unsigned)core_fingerprint());
    return ok;
}

#if TEST_SEED
static int load_test_seed(void) {
    uint8_t seed[64];
    uint64_t t;
    int ok;

    message("TEST SEED", "Public BIP39 vector. Never send funds to this wallet.", 1);
    t = time_us_64();
    pbkdf2_hmac_sha512((const uint8_t *)TEST_MNEMONIC, sizeof(TEST_MNEMONIC) - 1, (const uint8_t *)"mnemonic", 8, 2048,
                       seed);
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
            if (scan_seed()) return;
            show(&menu.screen);
            continue;
        }
#if TEST_SEED
        if (sel == 1 && load_test_seed()) return;
#endif
        show(&menu.screen);
    }
}

/* Shows the account xpub and descriptor. That is all the PC needs to watch the wallet, with no key */
static void show_xpub(void) {
    static ui_screen_t s;
    static char xpub[CORE_XPUB_MAX], desc[CORE_DESC_MAX];

    if (core_account_xpub(84, 0, xpub, desc) != CORE_OK) return message("xpub failed", NULL, 1);
    ui_xpub(&s, xpub, core_fingerprint(), TESTNET);
    show(&s);
    printf("%s\n%s\n", xpub, desc);
    wait_key();

    /* The descriptor runs to about 145 characters, which fits QR v8, so one still image carries it */
    if (!ui_qr_set(desc)) return message("QR failed", NULL, 1);
    printf("descriptor QR: %d modules\n", ui_qr_modules());
    for (;;) {
        int key;
        draw_qr();
        key = wait_key();
        if (key != UI_KEY_UP && key != UI_KEY_DOWN) return;
        ui_qr_level(key == UI_KEY_UP ? 1 : -1);
        printf("qr level %d\n", ui_qr_level_get());
    }
}

/* The hash of the parser.wasm actually loaded. Matching it against the jitsu-in viewer is
 * how you confirm the device is running the same module you are */
static void show_parser_hash(void) {
    static ui_screen_t s;
    uint8_t h[32];

    sha256(parser_wasm, sizeof(parser_wasm), h);
    ui_hash(&s, "parser.wasm", (unsigned)sizeof(parser_wasm), h);
    show(&s);
    printf("parser.wasm %u B sha256 ", (unsigned)sizeof(parser_wasm));
    for (int i = 0; i < 32; i++) printf("%02x", h[i]);
    printf("\n");
    wait_key();
}

static void main_menu(void) {
    static const char *const items[] = {"Scan PSBT", "Show xpub", "Parser hash", "Sign test PSBT", "Lock (wipe seed)"};
    static const char *const items_notest[] = {"Scan PSBT", "Show xpub", "Parser hash", "Lock (wipe seed)"};
    static char title[UI_COLS + 1];

    snprintf(title, sizeof(title), "%s fp %08x", TESTNET ? "Signet" : "Signer", (unsigned)core_fingerprint());
    ui_menu_init(&menu, title, TEST_SEED ? items : items_notest, TEST_SEED ? 5 : 4);
    show(&menu.screen);
    for (;;) {
        int sel = ui_menu_key(&menu, wait_key());
        if (sel == 0) scan_and_sign();
        if (sel == 1) show_xpub();
        if (sel == 2) show_parser_hash();
#if TEST_SEED
        if (sel == 3) sign_flow(test_psbt, sizeof(test_psbt));
#endif
        if (sel == (TEST_SEED ? 4 : 3)) {
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
    printf("\njitsu-in-pico (%s)\n", TESTNET ? "testnet/signet" : "mainnet");

    memcpy(parser_wasm_rw, parser_wasm, sizeof(parser_wasm_rw));
    /* A TESTNET=1 build targets signet and testnet: tb1 addresses, derived under m/84'/1'/... */
    if (!core_init(TESTNET ? CORE_TESTNET : CORE_MAINNET) ||
        !parser_host_init(parser_wasm_rw, sizeof(parser_wasm_rw), pool, sizeof(pool))) {
        message("Init failed", NULL, 1);
        return 1;
    }
    camera_bus_init(&camera_ov7670);
    camera_ready = camera_init(&camera_ov7670);
    printf("camera: %s\n", camera_ready ? "ready" : "not connected");
    for (;;) {
        seed_menu();
        main_menu();
    }
}
