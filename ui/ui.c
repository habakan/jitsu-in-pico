#include "ui.h"
#include <string.h>
#include <stdio.h>
#include "font8x16.h"
#include "qrcodegen.h"

#define RGB565(r, g, b) (uint16_t)(((r) >> 3) << 11 | ((g) >> 2) << 5 | (b) >> 3)
#define C_TEXT RGB565(230, 230, 230)
#define C_TITLE RGB565(255, 170, 0)
#define C_SEND RGB565(255, 110, 80)
#define C_OURS RGB565(90, 220, 120)
#define C_HINT RGB565(140, 140, 140)
#define C_BG RGB565(0, 0, 0)

typedef struct {
    ui_screen_t *s;
    int row;
} writer_t;

/* 右寄せでアプリ名を出す。各画面の 1 行目 */
static void put_header(writer_t *w);

static void put(writer_t *w, uint16_t color, const char *text) {
    if (w->row >= UI_ROWS) return;
    strncpy(w->s->text[w->row], text, UI_COLS);
    w->s->text[w->row][UI_COLS] = 0;
    w->s->color[w->row++] = color;
}

static void put_header(writer_t *w) {
    char line[UI_COLS + 1];
    size_t n = strlen(UI_APP_NAME);
    memset(line, ' ', UI_COLS);
    line[UI_COLS] = 0;
    if (n < UI_COLS) memcpy(line + (UI_COLS - 1 - n), UI_APP_NAME, n); /* 右端に 1 文字の余白を残す */
    put(w, C_HINT, line);
}

/* 長い文字列（アドレス、16 進のスクリプト）は途中で切らずに全部折り返す */
static void put_wrapped(writer_t *w, uint16_t color, const char *text) {
    size_t n = strlen(text);
    char chunk[UI_COLS + 1];
    for (size_t i = 0; i < n; i += UI_COLS) {
        size_t k = n - i < UI_COLS ? n - i : UI_COLS;
        memcpy(chunk, text + i, k);
        chunk[k] = 0;
        put(w, color, chunk);
    }
}

static void put_amount(writer_t *w, uint16_t color, const char *label, uint64_t sats) {
    char btc[21], line[UI_COLS + 1];
    size_t n = strlen(label);
    core_format_btc(sats, btc);
    memcpy(line, label, n);
    memcpy(line + n, btc, strlen(btc) + 1);
    strncat(line, " BTC", UI_COLS - strlen(line));
    put(w, color, line);
}

static void put_number(writer_t *w, uint16_t color, const char *label, unsigned v) {
    char line[UI_COLS + 1];
    size_t n = strlen(label);
    memcpy(line, label, n);
    if (v >= 10) line[n++] = (char)('0' + v / 10);
    line[n++] = (char)('0' + v % 10);
    line[n] = 0;
    put(w, color, line);
}

static void put_counter(writer_t *w, uint16_t color, const char *label, unsigned a, unsigned b) {
    char line[UI_COLS + 1];
    size_t n = strlen(label), o = n;
    memcpy(line, label, n);
    if (a >= 10) line[o++] = (char)('0' + a / 10);
    line[o++] = (char)('0' + a % 10);
    line[o++] = '/';
    if (b >= 10) line[o++] = (char)('0' + b / 10);
    line[o++] = (char)('0' + b % 10);
    line[o] = 0;
    put(w, color, line);
}

void ui_review_init(ui_review_t *r, const core_display_t *d) {
    static const char *owner[] = {"Send to", "Change", "Self-transfer"};
    unsigned n_send = 0;
    writer_t w;

    memset(r, 0, sizeof(*r));
    for (unsigned i = 0; i < d->n_outputs; i++) n_send += d->outputs[i].owner == CORE_OUT_EXTERNAL;

    w = (writer_t){&r->screens[r->n++], 0};
    put_header(&w);
    put(&w, C_TITLE, "Review transaction");
    put(&w, C_TEXT, "");
    put_amount(&w, C_SEND, "Spend ", d->spend);
    put_amount(&w, C_TEXT, "Fee   ", d->fee);
    put(&w, C_TEXT, "");
    put_number(&w, C_TEXT, "Outputs:  ", d->n_outputs);
    put_number(&w, C_TEXT, "External: ", n_send);
    w.row = UI_ROWS - 1;
    put(&w, C_HINT, "RIGHT: next   A: cancel");

    for (unsigned i = 0; i < d->n_outputs; i++) {
        const core_display_output_t *o = &d->outputs[i];
        uint16_t color = o->owner == CORE_OUT_EXTERNAL ? C_SEND : C_OURS;
        w = (writer_t){&r->screens[r->n++], 0};
        put_header(&w);
        put_counter(&w, C_TITLE, "Output ", i + 1, d->n_outputs);
        put(&w, color, owner[o->owner]);
        put_amount(&w, color, "", o->amount);
        put(&w, C_TEXT, "");
        put(&w, C_HINT, o->text_kind == CORE_TEXT_ADDRESS ? "Address:" : o->text_kind == CORE_TEXT_OP_RETURN
                                                                           ? "OP_RETURN data (hex):" : "Script (hex):");
        put_wrapped(&w, C_TEXT, o->text);
        w.row = UI_ROWS - 1;
        put(&w, C_HINT, "LEFT/RIGHT: prev/next");
    }

    w = (writer_t){&r->screens[r->n++], 0};
    put_header(&w);
    put(&w, C_TITLE, "Sign this transaction?");
    put(&w, C_TEXT, "");
    put_amount(&w, C_SEND, "Spend ", d->spend);
    put_amount(&w, C_TEXT, "Fee   ", d->fee);
    w.row = UI_ROWS - 2;
    put(&w, C_HINT, "PUSH: sign");
    put(&w, C_HINT, "NEXT: cancel screen");

    /* 最後は取り消し画面。ボタン 2 個（進む・押込）だけでも取り消せて、ここから先頭へ回り込む */
    w = (writer_t){&r->screens[r->n++], 0};
    put_header(&w);
    put(&w, C_TITLE, "Cancel transaction?");
    put(&w, C_TEXT, "");
    put(&w, C_TEXT, "Nothing is signed.");
    w.row = UI_ROWS - 2;
    put(&w, C_HINT, "PUSH: cancel");
    put(&w, C_HINT, "NEXT: back to start");
    r->seen = 1;
}

int ui_review_key(ui_review_t *r, int key) {
    /* 署名確認は最後から 2 枚目、取り消しは最後。全画面を見ていないと署名できない（取り消し画面は除く） */
    unsigned sign = r->n - 2, must_see = (1u << (r->n - 1)) - 1;
    if (key == UI_KEY_A) return UI_REJECTED;
    if (key == UI_KEY_RIGHT || key == UI_KEY_DOWN) r->cur = (r->cur + 1) % r->n;
    if (key == UI_KEY_LEFT || key == UI_KEY_UP) r->cur = (r->cur + r->n - 1) % r->n;
    r->seen |= 1u << r->cur;
    if (key == UI_KEY_PUSH && r->cur == r->n - 1) return UI_REJECTED;
    if (key == UI_KEY_PUSH && r->cur == sign && (r->seen & must_see) == must_see) return UI_APPROVED;
    return UI_PENDING;
}

static void menu_draw(ui_menu_t *m) {
    writer_t w = {&m->screen, 0};
    memset(&m->screen, 0, sizeof(m->screen));
    put_header(&w);
    put(&w, C_TITLE, m->title);
    put(&w, C_TEXT, "");
    for (unsigned i = 0; i < m->n; i++) {
        char line[UI_COLS + 1];
        line[0] = i == m->cur ? '>' : ' ';
        line[1] = ' ';
        strncpy(line + 2, m->items[i], UI_COLS - 2);
        line[UI_COLS] = 0;
        put(&w, i == m->cur ? C_TITLE : C_TEXT, line);
    }
    w.row = UI_ROWS - 1;
    put(&w, C_HINT, "DOWN: move   PUSH: select");
}

void ui_menu_init(ui_menu_t *m, const char *title, const char *const *items, unsigned n) {
    m->title = title;
    m->items = items;
    m->n = n > UI_MENU_MAX ? UI_MENU_MAX : n;
    m->cur = 0;
    menu_draw(m);
}

int ui_menu_key(ui_menu_t *m, int key) {
    if (key == UI_KEY_PUSH || key == UI_KEY_RIGHT) return (int)m->cur;
    if (key == UI_KEY_A || key == UI_KEY_LEFT) return UI_MENU_BACK;
    if (key == UI_KEY_DOWN) m->cur = (m->cur + 1) % m->n;
    if (key == UI_KEY_UP) m->cur = (m->cur + m->n - 1) % m->n;
    menu_draw(m);
    return UI_MENU_PENDING;
}

void ui_message(ui_screen_t *s, const char *title, const char *body, int warn) {
    writer_t w = {s, 0};
    memset(s, 0, sizeof(*s));
    put_header(&w);
    put(&w, warn ? C_SEND : C_TITLE, title);
    put(&w, C_TEXT, "");
    if (body) put_wrapped(&w, C_TEXT, body);
    w.row = UI_ROWS - 1;
    put(&w, C_HINT, "PUSH: ok");
}

void ui_render_line(const ui_screen_t *s, int y, uint16_t line[UI_W]) {
    int row = y / FONT_H, gy = y % FONT_H;
    const char *t = s->text[row];
    for (int x = 0; x < UI_W; x++) line[x] = C_BG;
    for (int c = 0; c < UI_COLS && t[c]; c++) {
        unsigned ch = (unsigned char)t[c];
        uint8_t bits = font8x16[(ch >= 0x20 && ch <= 0x7e ? ch : '?') - 0x20][gy];
        for (int b = 0; b < FONT_W; b++)
            if (bits & (0x80 >> b)) line[c * FONT_W + b] = s->color[row];
    }
}

static uint8_t qr[qrcodegen_BUFFER_LEN_FOR_VERSION(UI_QR_MAX_VERSION)];

int ui_qr_set(const char *text) {
    static uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(UI_QR_MAX_VERSION)];
    return qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_LOW, 1, UI_QR_MAX_VERSION, qrcodegen_Mask_AUTO, true);
}

int ui_qr_modules(void) { return qrcodegen_getSize(qr); }

void ui_qr_render_line(int y, uint16_t line[UI_W]) {
    int size = qrcodegen_getSize(qr), scale = UI_W / (size + 8), off = (UI_W - size * scale) / 2;
    int my = (y - off) / scale;
    for (int x = 0; x < UI_W; x++) {
        int mx = (x - off) / scale;
        int dark = y >= off && x >= off && my < size && mx < size && qrcodegen_getModule(qr, mx, my);
        line[x] = dark ? 0x0000 : 0xffff;
    }
}

/* 口座の拡張公開鍵。PC 側と目で突き合わせられるよう、頭と尻を切らずに全部出す */
void ui_xpub(ui_screen_t *s, const char *xpub, uint32_t fp, int testnet) {
    writer_t w = {s, 0};
    char line[UI_COLS + 1];
    size_t n = strlen(xpub);

    memset(s, 0, sizeof(*s));
    put_header(&w);
    put(&w, C_TITLE, "Account xpub (watch-only)");
    snprintf(line, sizeof(line), "m/84h/%dh/0h  fp %08lx", testnet ? 1 : 0, (unsigned long)fp);
    put(&w, C_HINT, line);
    put(&w, C_TEXT, "");
    for (size_t i = 0; i < n; i += UI_COLS) {
        snprintf(line, sizeof(line), "%.*s", (int)UI_COLS, xpub + i);
        put(&w, C_OURS, line);
    }
    w.row = UI_ROWS - 1;
    put(&w, C_HINT, "PUSH: show QR");
}

/* 積んでいる wasm のハッシュ。ブラウザ側が出す値と目で突き合わせるので、8 文字ずつ区切る */
void ui_hash(ui_screen_t *s, const char *name, unsigned len, const uint8_t h[32]) {
    writer_t w = {s, 0};
    char line[UI_COLS + 1];

    memset(s, 0, sizeof(*s));
    put_header(&w);
    put(&w, C_TITLE, "Parser hash");
    snprintf(line, sizeof(line), "%s  %u B", name, len);
    put(&w, C_HINT, line);
    put(&w, C_TEXT, "");
    for (int r = 0; r < 4; r++) {
        const uint8_t *p = h + r * 8;
        snprintf(line, sizeof(line), "%02x%02x%02x%02x %02x%02x%02x%02x",
                 p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
        put(&w, C_OURS, line);
    }
    put(&w, C_TEXT, "");
    put_wrapped(&w, C_HINT, "Must match the viewer page");
    w.row = UI_ROWS - 1;
    put(&w, C_HINT, "PUSH: ok");
}
