/* カメラの初期確認: 取り込んだ QVGA を LCD に縮小表示し、quirc（自前フォーク、固定小数点）で QR を読む。
 * 取り込み・デコードの時間と読めた文字列を UART に出す */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "camera.h"
#include "quirc.h"
#include "st7789.h"

int main(void) {
    struct quirc *q;
    uint16_t line[240];
    uint8_t val;

    stdio_init_all();
    st7789_init();
    printf("\ncamera test (%s)\n", camera_ov7670.name);
    if (!camera_init(&camera_ov7670)) return printf("camera init failed (SCCB)\n"), 1;
    /* PID/VER で実物を確かめる。OV7670 は 0x76/0x73、OV7675 は 0x76/0x73 以外のこともあるので値をそのまま出す */
    if (camera_read_reg(0x0a, &val)) printf("PID 0x%02x\n", val);
    if (camera_read_reg(0x0b, &val)) printf("VER 0x%02x\n", val);
    if (!(q = quirc_new()) || quirc_resize(q, CAMERA_W, CAMERA_H) < 0) return printf("quirc alloc failed\n"), 1;

    for (;;) {
        /* quirc の画像バッファへ直接取り込む（malloc なので 4 byte 境界） */
        uint8_t *img = quirc_begin(q, NULL, NULL);
        uint64_t t0 = time_us_64(), t1, t2;
        if (!camera_capture(img, 1000)) {
            printf("capture timeout\n");
            continue;
        }
        t1 = time_us_64();

        /* 320x240 を 3/4 に間引いて 240x180 で表示する（上下 30 行は黒） */
        st7789_begin_frame();
        for (int y = 0; y < 240; y++) {
            int sy = (y - 30) * 4 / 3;
            for (int x = 0; x < 240; x++) {
                uint8_t v = (y >= 30 && y < 210) ? img[sy * CAMERA_W + x * 4 / 3] : 0;
                line[x] = (uint16_t)((v >> 3) << 11 | (v >> 2) << 5 | (v >> 3));
            }
            st7789_write_line(line);
        }

        quirc_end(q);
        t2 = time_us_64();
        printf("capture %llu us, decode %llu us, codes %d\n", (unsigned long long)(t1 - t0),
               (unsigned long long)(t2 - t1), quirc_count(q));
        for (int i = 0; i < quirc_count(q); i++) {
            static struct quirc_code code;
            static struct quirc_data data;
            quirc_extract(q, i, &code);
            quirc_decode_error_t err = quirc_decode(&code, &data);
            if (err)
                printf("  %s\n", quirc_strerror(err));
            else
                printf("  v%d: %.*s\n", data.version, data.payload_len, (const char *)data.payload);
        }
    }
}
