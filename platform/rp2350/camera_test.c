/* カメラの初期確認: 取り込んだ QVGA を LCD に縮小表示し、quirc（自前フォーク、固定小数点）で QR を読む。
 * 取り込み・デコードの時間と読めた文字列を UART に出す */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "board_pins.h"
#include "camera.h"
#include "hardware/gpio.h"
#include "quirc.h"
#include "st7789.h"

/* どの線が来ていないかを見る。D0〜D7・PCLK・HREF・VSYNC は GP2〜GP12 に連番で並んでいる */
static void probe_signals(void) {
    uint32_t prev, changed = 0, high = 0;
    unsigned edges[3] = {0};
    uint64_t t0;

    /* VSYNC とその隣を内蔵プルアップで引き上げる。線が来ていなければ high、
     * センサーが駆動していれば toggling、GND に落ちていれば low になる */
    for (unsigned gp = PIN_CAM_VSYNC; gp <= 17; gp++) gpio_pull_up(gp);
    sleep_ms(1);
    prev = gpio_get_all();
    t0 = time_us_64();

    while (time_us_64() - t0 < 100000) {
        uint32_t v = gpio_get_all();
        changed |= v ^ prev;
        high |= v;
        for (unsigned i = 0; i < 3; i++)
            if ((v ^ prev) >> (PIN_CAM_PCLK + i) & 1) edges[i]++;
        prev = v;
    }
    /* 挿し間違いを見つけられるよう、隣の GP13〜GP17 も出す */
    for (unsigned gp = PIN_CAM_D0; gp <= 17; gp++) {
        static const char *const name[] = {"D0",   "D1",   "D2",  "D3",      "D4",      "D5",     "D6",  "D7",
                                           "PCLK", "HREF", "VSYNC", "(joy UP)", "(cam SDA)", "(cam SCL)", "(lcd DC)",
                                           "(btn next)"};
        printf("%-10s GP%-2u %s\n", name[gp - PIN_CAM_D0], gp,
               changed >> gp & 1 ? "toggling" : (high >> gp & 1 ? "stuck high" : "stuck low"));
    }
    /* エッジの数で、どの線が実際に来ているかを当てる。PCLK は MHz 単位、HREF は行の数、VSYNC はフレームの数 */
    for (unsigned i = 0; i < 3; i++) {
        static const char *const want[] = {"PCLK", "HREF", "VSYNC"};
        unsigned n = edges[i];
        const char *got = n == 0 ? "未接続" : n > 50000 ? "PCLK" : n > 100 ? "HREF" : "VSYNC";
        printf("GP%u（%s を繋ぐ所）: %u edges/100ms -> %s\n", PIN_CAM_PCLK + i, want[i], n, got);
    }
    printf("カメラ側: 左列の上から 3 番目が VS、4 番目が PCLK。右列の 3 番目が HS\n");

    /* 1 フレームの行数を数える。QVGA なら 240、VGA のままなら 480 */
    {
        unsigned lines = 0;
        int prev_vs = gpio_get(PIN_CAM_VSYNC), prev_href = gpio_get(PIN_CAM_HREF), started = 0;
        uint64_t end = time_us_64() + 500000;
        while (time_us_64() < end) {
            int vs = gpio_get(PIN_CAM_VSYNC), href = gpio_get(PIN_CAM_HREF);
            if (!prev_vs && vs) {           /* VSYNC の立ち上がりでフレームの区切り */
                if (started) break;
                started = 1, lines = 0;
            }
            if (started && !prev_href && href) lines++;
            prev_vs = vs, prev_href = href;
        }
        printf("1 フレームの行数: %u（QVGA なら 240、VGA のままなら 480）\n", lines);
    }
}

int main(void) {
    struct quirc *q;
    uint16_t line[240];
    uint8_t val;

    stdio_init_all();
    st7789_init();
    printf("\ncamera test (%s)\n", camera_ov7670.name);
    /* まず電源・XCLK・SCCB だけで ID を読む。ここが通れば電源と I2C とクロックは正しい */
    camera_bus_init(&camera_ov7670);
    if (!camera_read_reg(0x0a, &val)) return printf("SCCB read failed: check 3V3/GND/SIOD/SIOC/XCLK\n"), 1;
    printf("PID 0x%02x\n", val);
    if (camera_read_reg(0x0b, &val)) printf("VER 0x%02x\n", val);
    if (!camera_init(&camera_ov7670)) return printf("register setup failed\n"), 1;
    probe_signals();
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
        printf("capture %llu us (%llu fps), decode %llu us, codes %d\n", (unsigned long long)(t1 - t0),
               (unsigned long long)(1000000 / (t1 - t0)), (unsigned long long)(t2 - t1), quirc_count(q));
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
