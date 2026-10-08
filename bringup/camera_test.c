/* Camera bring-up: shows the captured QVGA scaled down on the LCD and reads QRs with our quirc fork,
 * the fixed-point one. Capture and decode times, and whatever was read, go to the UART */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "board_pins.h"
#include "camera.h"
#include "hardware/gpio.h"
#include "quirc.h"
#include "st7789.h"

/* Finds which line is missing. D0-D7, PCLK, HREF and VSYNC run consecutively from GP2 to GP12 */
static void probe_signals(void) {
    uint32_t prev, changed = 0, high = 0;
    unsigned edges[3] = {0};
    uint64_t t0;

    /* VSYNC and its neighbours pulled up internally: high means nothing is connected, toggling means
     * the sensor is driving it, and low means it is shorted to ground */
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
    /* GP13 to GP17 are reported too, so a lead in the wrong hole shows up */
    for (unsigned gp = PIN_CAM_D0; gp <= 17; gp++) {
        static const char *const name[] = {"D0",        "D1",        "D2",       "D3",        "D4",    "D5",
                                           "D6",        "D7",        "PCLK",     "HREF",      "VSYNC", "(joy UP)",
                                           "(cam SDA)", "(cam SCL)", "(lcd DC)", "(btn next)"};
        printf("%-10s GP%-2u %s\n", name[gp - PIN_CAM_D0], gp,
               changed >> gp & 1 ? "toggling" : (high >> gp & 1 ? "stuck high" : "stuck low"));
    }
    /* The edge count says which line this really is: PCLK runs at MHz, HREF at rows and VSYNC at frames */
    for (unsigned i = 0; i < 3; i++) {
        static const char *const want[] = {"PCLK", "HREF", "VSYNC"};
        unsigned n = edges[i];
        const char *got = n == 0 ? "nothing" : n > 50000 ? "PCLK" : n > 100 ? "HREF" : "VSYNC";
        printf("GP%u (where %s goes): %u edges/100ms -> %s\n", PIN_CAM_PCLK + i, want[i], n, got);
    }
    printf("on the camera: left column, third down is VS and fourth is PCLK; right column, third is HS\n");

    /* Counts the rows in a frame: 240 for QVGA, or 480 if it is still sending VGA */
    {
        unsigned lines = 0;
        int prev_vs = gpio_get(PIN_CAM_VSYNC), prev_href = gpio_get(PIN_CAM_HREF), started = 0;
        uint64_t end = time_us_64() + 500000;
        while (time_us_64() < end) {
            int vs = gpio_get(PIN_CAM_VSYNC), href = gpio_get(PIN_CAM_HREF);
            if (!prev_vs && vs) { /* VSYNC's rising edge separates frames */
                if (started) break;
                started = 1, lines = 0;
            }
            if (started && !prev_href && href) lines++;
            prev_vs = vs, prev_href = href;
        }
        printf("rows per frame: %u (240 for QVGA, 480 if still VGA)\n", lines);
    }
}

int main(void) {
    struct quirc *q;
    uint16_t line[240];
    uint8_t val;

    stdio_init_all();
    st7789_init();
    printf("\ncamera test (%s)\n", camera_ov7670.name);
    /* The ID is read first with nothing but power, XCLK and SCCB; getting it means those three are right */
    camera_bus_init(&camera_ov7670);
    if (!camera_read_reg(0x0a, &val)) return printf("SCCB read failed: check 3V3/GND/SIOD/SIOC/XCLK\n"), 1;
    printf("PID 0x%02x\n", val);
    if (camera_read_reg(0x0b, &val)) printf("VER 0x%02x\n", val);
    if (!camera_init(&camera_ov7670)) return printf("register setup failed\n"), 1;
    probe_signals();
    if (!(q = quirc_new()) || quirc_resize(q, CAMERA_W, CAMERA_H) < 0) return printf("quirc alloc failed\n"), 1;

    for (;;) {
        /* Captured straight into quirc's image buffer, which malloc leaves 4-byte aligned */
        uint8_t *img = quirc_begin(q, NULL, NULL);
        uint64_t t0 = time_us_64(), t1, t2;
        if (!camera_capture(img, 1000)) {
            printf("capture timeout\n");
            continue;
        }
        t1 = time_us_64();

        /* 320x240 decimated to three quarters and shown as 240x180, with 30 black rows above and below */
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
            if (err) printf("  %s\n", quirc_strerror(err));
            else printf("  v%d: %.*s\n", data.version, data.payload_len, (const char *)data.payload);
        }
    }
}
