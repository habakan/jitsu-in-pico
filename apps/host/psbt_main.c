/* A host that runs the full round: parser.wasm, core, parser.wasm. Two modes, one that only parses
 * and one that goes through to a signature */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"
#include "parser_host.h"
#include "ui.h"
#include "sha512.h"
#include "parser_wasm.h"
#ifdef QEMU_BUILD
static uint64_t now(void) {
    uint32_t lo, hi;
    __asm__ volatile("csrr %0, minstret; csrr %1, minstreth" : "=r"(lo), "=r"(hi));
    return ((uint64_t)hi << 32) | lo;
}
#define UNIT "instret"
#else
#include <time.h>
static uint64_t now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
#define UNIT "us"
#endif

#define MNEMONIC "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"

static char pool[256 * 1024];
/* The classic interpreter rewrites on load, so it is handed a writable copy */
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

/* Writes the review screens as 240x240 PPMs, so the layout can be checked without the hardware */
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

static int zero_rng(uint8_t *b, size_t n) {
    memset(b, 0, n);
    return 1;
} /* for tests; the device uses its TRNG */

/* Feeds a UR, one part per line, reassembles the PSBT into file_buf and returns its length, or -1 */
static long assemble_ur(const char *path) {
    static char line[PARSER_PSBT_MAX];
    FILE *f = fopen(path, "r");
    int32_t rc = 0;
    unsigned parts = 0;
    uint64_t t0 = now(), max_part = 0;

    if (!f || !parser_host_ur_reset()) return -1;
    while (rc <= 0 && fgets(line, sizeof(line), f)) {
        size_t n = strcspn(line, "\r\n");
        uint64_t t = now();
        if (!n) continue;
        if (!parser_host_ur_receive(line, (uint32_t)n, &rc, file_buf, sizeof(file_buf))) break;
        t = now() - t;
        max_part = t > max_part ? t : max_part;
        parts++;
    }
    fclose(f);
    printf("  ur: %u parts, %s total=%llu max_per_part=%llu\n", parts, UNIT, (unsigned long long)(now() - t0),
           (unsigned long long)max_part);
    return rc > 0 ? rc : -1;
}

/* Turns the signed PSBT into a UR and writes three times the pure part count, mixed parts included,
 * one per line, to <out>.ur. With preview it also writes the first two parts' QR screens as PPMs */
/* Once encoding has started, write three passes over the pure parts */
static int write_ur_parts(const char *out_path) {
    static char text[4096];
    char path[256];
    FILE *f;

    snprintf(path, sizeof(path), "%s.ur", out_path);
    if (!(f = fopen(path, "w"))) return 1;
    for (int i = 0; i < 24; i++) {
        if (!parser_host_ur_encode_next(text, sizeof(text))) return fclose(f), 1;
        fprintf(f, "%s\n", text);
    }
    fclose(f);
    return 0;
}

static int write_ur(const char *out_path, uint32_t len, const char *preview) {
    static char text[4096];
    char path[256];
    uint64_t t0 = now(), worst_qr = 0;
    int32_t seq_len = parser_host_ur_encode_start(len, UI_UR_FRAGMENT);
    FILE *f;

    snprintf(path, sizeof(path), "%s.ur", out_path);
    if (seq_len <= 0 || !(f = fopen(path, "w"))) return -1;
    for (int32_t i = 0; i < 3 * seq_len; i++) {
        uint64_t t;
        if (!parser_host_ur_encode_next(text, sizeof(text))) return fclose(f), -1;
        fprintf(f, "%s\n", text);
        t = now();
        if (!ui_qr_set(text)) return fclose(f), -1;
        t = now() - t;
        worst_qr = t > worst_qr ? t : worst_qr;
        if (preview && i < 2) {
            uint16_t line[UI_W];
            char ppm[256];
            FILE *p;
            snprintf(ppm, sizeof(ppm), "%s_qr_%02d.ppm", preview, (int)i);
            if (!(p = fopen(ppm, "wb"))) continue;
            fprintf(p, "P6 %d %d 255\n", UI_W, UI_H);
            for (int y = 0; y < UI_H; y++) {
                ui_qr_render_line(y, line);
                for (int x = 0; x < UI_W; x++) {
                    uint8_t v = line[x] ? 255 : 0, rgb[3] = {v, v, v};
                    fwrite(rgb, 1, 3, p);
                }
            }
            fclose(p);
        }
    }
    fclose(f);
    printf("  ur out: %d pure parts, %s encode+qr total=%llu worst_qr=%llu\n", (int)seq_len, UNIT,
           (unsigned long long)(now() - t0), (unsigned long long)worst_qr);
    return 0;
}

/* The plan as JSON, so tools/check_against_core.py can diff it against Bitcoin Core's decodepsbt.
 * Only what Core also reports is printed; comparing anything else would prove nothing */
static void put_hex(const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) printf("%02x", b[i]);
}

static int emit_plan(const char *in_path) {
    static plan_t plan;
    core_prevtx_t prev[PLAN_MAX_INPUTS];
    core_review_t r;
    uint32_t rc = 0;
    long len = read_file(in_path);

    if (len < 0 || !parser_host_parse(file_buf, (uint32_t)len, core_fingerprint(), &rc, &plan, prev, prevtx_arena,
                                      sizeof(prevtx_arena)))
        return printf("{\"error\":\"host\"}\n"), 1;
    if (rc) return printf("{\"error\":\"parse\",\"rc\":%u}\n", (unsigned)rc), 0;

    printf("{\"tx_version\":%d,\"locktime\":%u,\"inputs\":[", plan.tx_version, plan.locktime);
    for (unsigned i = 0; i < plan.n_inputs; i++) {
        const plan_input_t *in = &plan.inputs[i];
        printf("%s{\"txid\":\"", i ? "," : "");
        /* Core reports a txid in reversed byte order; the plan holds it as it appears in the transaction */
        for (int j = 31; j >= 0; j--) printf("%02x", in->prev_txid[j]);
        printf("\",\"vout\":%u,\"sequence\":%u,\"amount\":%llu,\"spk\":\"", in->prev_vout, in->sequence,
               (unsigned long long)in->amount);
        put_hex(in->spk.bytes, in->spk.len);
        printf("\",\"sighash\":%u,\"path\":[", in->sighash_type);
        for (unsigned j = 0; j < in->key.depth; j++) printf("%s%u", j ? "," : "", in->key.path[j]);
        printf("],\"fingerprint\":\"%08x\"}", in->key.depth ? in->key.fingerprint : 0);
    }
    printf("],\"outputs\":[");
    for (unsigned i = 0; i < plan.n_outputs; i++) {
        const plan_output_t *out = &plan.outputs[i];
        printf("%s{\"amount\":%llu,\"spk\":\"", i ? "," : "", (unsigned long long)out->amount);
        put_hex(out->spk.bytes, out->spk.len);
        printf("\",\"path\":[");
        for (unsigned j = 0; j < out->key.depth; j++) printf("%s%u", j ? "," : "", out->key.path[j]);
        printf("]}");
    }
    printf("]");
    if (core_review(&plan, prev, &r) == CORE_OK)
        printf(",\"fee\":%llu,\"will_sign\":%u", (unsigned long long)r.fee, r.n_sign);
    printf("}\n");
    return 0;
}

static int sign(const char *in_path, const char *out_path, const char *preview) {
    static plan_t plan;
    core_prevtx_t prev[PLAN_MAX_INPUTS];
    core_review_t r;
    core_display_t d;
    core_sig_t sigs[PLAN_MAX_INPUTS];
    unsigned n_sigs;
    uint32_t rc = 0, out_len;
    uint64_t t0 = now(), t1, t2, t3, t4;
    size_t ext = strlen(in_path);
    long len = ext > 3 && !strcmp(in_path + ext - 3, ".ur") ? assemble_ur(in_path) : read_file(in_path);
    char btc[21];
    int err;
    FILE *f;

    if (len < 0 ||
        !parser_host_parse(file_buf, (uint32_t)len, core_fingerprint(), &rc, &plan, prev, prevtx_arena,
                           sizeof(prevtx_arena)) ||
        rc)
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
    if (write_ur(out_path, out_len, preview) != 0) return printf("ur encode failed\n"), 1;
    printf("  signed %u input(s); %s parse=%llu review=%llu sign=%llu finalize=%llu; pool_highmark=%u\n", n_sigs, UNIT,
           (unsigned long long)(t1 - t0), (unsigned long long)(t2 - t1), (unsigned long long)(t3 - t2),
           (unsigned long long)(t4 - t3), (unsigned)parser_host_pool_highmark());
    return 0;
}

int main(int argc, char **argv) {
    uint8_t seed[64];

#ifdef QEMU_BUILD
    /* libgloss's crt0 does not pass the semihosting command line through, so it is fixed here */
    static char *qemu_argv[] = {"psbt_host", "sign", "build/psbt/own_mixed_nwu.ur", "build/psbt/own_mixed_nwu.qemu"};
    argc = 4, argv = qemu_argv;
#endif
    if (argc < 3)
        return fprintf(stderr,
                       "usage: %s parse FILE... | plan FILE | sign IN(.psbt|.ur) OUT [PREVIEW_PREFIX] | bin2ur IN.psbt "
                       "OUT | ur2bin IN.ur OUT\n",
                       argv[0]),
               2;
    memcpy(parser_wasm_rw, parser_wasm, sizeof(parser_wasm_rw));
    if (!core_init(CORE_MAINNET) || !parser_host_init(parser_wasm_rw, sizeof(parser_wasm_rw), pool, sizeof(pool)))
        return 1;
    pbkdf2_hmac_sha512((const uint8_t *)MNEMONIC, sizeof(MNEMONIC) - 1, (const uint8_t *)"mnemonic", 8, 2048, seed);
    if (!core_load_seed(seed)) return 1;

    if (!strcmp(argv[1], "plan") && argc == 3) return emit_plan(argv[2]);
    if (!strcmp(argv[1], "sign")) return argc >= 4 ? sign(argv[2], argv[3], argc > 4 ? argv[4] : NULL) : 2;
    if (!strcmp(argv[1], "bin2ur") &&
        argc == 4) { /* a PSBT as a UR, one part per line, to test reading on the device */
        long len = read_file(argv[2]);
        if (len <= 0 || parser_host_ur_encode_bytes(file_buf, (uint32_t)len, UI_UR_FRAGMENT) <= 0) return 1;
        return write_ur_parts(argv[3]);
    }
    if (!strcmp(argv[1], "ur2bin") && argc == 4) { /* reassemble a UR, one part per line, and write the PSBT */
        long len = assemble_ur(argv[2]);
        FILE *f = len > 0 ? fopen(argv[3], "wb") : NULL;
        if (!f) return 1;
        fwrite(file_buf, 1, (size_t)len, f);
        fclose(f);
        return 0;
    }
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
