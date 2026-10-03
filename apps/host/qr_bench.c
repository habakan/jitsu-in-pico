#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "quirc.h"
#include "qrcodegen.h"
#include "qr_frames.h"

#ifdef QEMU_BUILD
#include <malloc.h>
static uint64_t now(void) { uint32_t lo, hi; __asm__ volatile("csrr %0, minstret; csrr %1, minstreth" : "=r"(lo), "=r"(hi)); return ((uint64_t)hi << 32) | lo; }
#define UNIT "instret"
extern uint32_t qemu_stack[], qemu_stack_top[];
static void __attribute__((noinline)) paint_stack(void) {
    uint32_t *sp, *p = qemu_stack;
    __asm__ volatile("mv %0, sp" : "=r"(sp));
    while (p < sp - 64) *p++ = 0xdeadbeef;
}
#elif defined(PICO_BUILD)
#include <malloc.h>
#include "pico/stdlib.h"
#define now() time_us_64()
#define UNIT "us"
#else
#include <time.h>
static uint64_t now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000; }
#define UNIT "us"
#endif

#define LCD_W 240
static volatile uint16_t lcd_sink;

/* 240x240 LCD へ 1 行ずつ RGB565 で流す想定。フルフレームバッファは持たない */
static void render_qr(const uint8_t *qr) {
    int size = qrcodegen_getSize(qr), scale = LCD_W / (size + 4), off = (LCD_W - size * scale) / 2;
    uint16_t line[LCD_W];
    for (int y = 0; y < LCD_W; y++) {
        int my = (y - off) / scale;
        for (int x = 0; x < LCD_W; x++) {
            int mx = (x - off) / scale;
            int dark = y >= off && x >= off && my < size && mx < size && qrcodegen_getModule(qr, mx, my);
            line[x] = dark ? 0x0000 : 0xffff;
        }
        lcd_sink = line[y];  /* SPI 送信の代わり。行バッファ生成を最適化で消させない */
    }
}

int main(void) {
#ifdef QEMU_BUILD
    paint_stack();
#elif defined(PICO_BUILD)
    stdio_init_all();
    printf("\nqr_bench: %dx%d\n", FRAME_W, FRAME_H);
#endif
    struct quirc *q = quirc_new();
    if (!q || quirc_resize(q, FRAME_W, FRAME_H) < 0) return 1;
#if defined(QEMU_BUILD) || defined(PICO_BUILD)
    printf("quirc_heap %d\n", mallinfo().uordblks);
#endif

    for (unsigned i = 0; i < sizeof(frames) / sizeof(frames[0]); i++) {
        const struct frame *f = &frames[i];
        uint8_t *buf = quirc_begin(q, NULL, NULL);
        memcpy(buf, f->pix, FRAME_W * FRAME_H);  /* 実機ではカメラの DMA がここへ直接書く */

        uint64_t t0 = now(), t_ext = 0, t_dec = 0;
        quirc_end(q);
        uint64_t t_end = now() - t0;
        int ok = 0, n = quirc_count(q);
        quirc_decode_error_t err = QUIRC_SUCCESS;
        for (int j = 0; j < n; j++) {
            struct quirc_code code;
            struct quirc_data data;
            uint64_t a = now();
            quirc_extract(q, j, &code);
            uint64_t b = now();
            err = quirc_decode(&code, &data);
            t_ext += b - a;
            t_dec += now() - b;
            if (!err && !strcmp((char *)data.payload, f->text)) ok = 1;
        }
        uint64_t t1 = now();

        static uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
        if (!qrcodegen_encodeText(f->text, tmp, qr, qrcodegen_Ecc_LOW, 1, 40, qrcodegen_Mask_AUTO, false)) return 1;
        uint64_t t2 = now();
        render_qr(qr);
        uint64_t t3 = now();

        printf("%-10s v%-2d codes=%d decode=%s(%s) %s decode=%llu (end=%llu extract=%llu ecc=%llu) encode=%llu render=%llu\n",
               f->name, f->version, n, ok ? "ok" : "NG", quirc_strerror(err), UNIT, (unsigned long long)(t1 - t0),
               (unsigned long long)t_end, (unsigned long long)t_ext, (unsigned long long)t_dec, (unsigned long long)(t2 - t1),
               (unsigned long long)(t3 - t2));
    }
#ifdef QEMU_BUILD
    printf("heap_arena %d\n", mallinfo().arena);
    uint32_t *p = qemu_stack;
    while (p < qemu_stack_top && *p == 0xdeadbeef) p++;
    printf("native_stack_used %u\n", (unsigned)((char *)qemu_stack_top - (char *)p));
#endif
#ifdef PICO_BUILD
    printf("done\n");
    while (1) tight_loop_contents();
#endif
    return 0;
}
