/* ブラウザ用。実機と同じ quirc（自前フォークの固定小数点版）で QR を読む */
#include <stddef.h>
#include <stdint.h>
#include "quirc.h"

/* quirc は malloc を使うが、wasi-libc を引き込むと import が増える。
 * 伸ばすだけの割り当てにして、qr_init のたびに先頭へ戻す（解放は何もしない） */
static uint8_t arena[1u << 21];
static size_t used;

void *malloc(size_t n) {
    size_t p = (used + 7) & ~(size_t)7;
    if (p + n > sizeof(arena)) return NULL;
    used = p + n;
    return arena + p;
}
void *calloc(size_t n, size_t m) {
    uint8_t *p = malloc(n * m);
    if (p)
        for (size_t i = 0; i < n * m; i++) p[i] = 0;
    return p;
}
void free(void *p) { (void)p; }

static struct quirc *q;
static struct quirc_data data;

__attribute__((export_name("qr_init"))) int qr_init(int w, int h) {
    used = 0; /* 割り当てを先頭へ戻す。q は無効になるので作り直す */
    q = quirc_new();
    return q && quirc_resize(q, w, h) >= 0;
}
/* ここへ 8bit グレースケールを w*h byte 書いてから qr_decode を呼ぶ */
__attribute__((export_name("qr_image"))) uint8_t *qr_image(void) { return quirc_begin(q, NULL, NULL); }
__attribute__((export_name("qr_payload"))) uint8_t *qr_payload(void) { return data.payload; }

/* 読めた最初の QR の長さを返す。見つからなければ 0、見つかったがデコードに失敗したら負 */
__attribute__((export_name("qr_decode"))) int qr_decode(void) {
    quirc_end(q);
    int n = quirc_count(q);
    for (int i = 0; i < n; i++) {
        struct quirc_code code;
        quirc_extract(q, i, &code);
        if (!quirc_decode(&code, &data)) return data.payload_len;
    }
    return n ? -1 : 0;
}
