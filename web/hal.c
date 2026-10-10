/* The board drivers main.c calls, backed by the browser: the LCD is a canvas, the buttons are a key
 * queue filled by the page, and the camera is getUserMedia. sleep_ms yields through Asyncify */
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <emscripten.h>
#include "pico/stdlib.h"
#include "pico/rand.h"
#include "st7789.h"
#include "buttons.h"
#include "camera.h"
#include "ui.h"

static uint8_t fb[UI_W * UI_H * 4];
static int fb_y;

EM_JS(void, js_present, (const uint8_t *rgba), { Module.present(HEAPU8.subarray(rgba, rgba + 240 * 240 * 4)); });
EM_JS(int, js_key, (void), { return Module.keys.length ? Module.keys.shift() : -1; });
EM_JS(int, js_capture, (uint8_t * buf), { return Module.capture(HEAPU8.subarray(buf, buf + 320 * 240)); });

void stdio_init_all(void) {
}

void sleep_ms(uint32_t ms) {
    emscripten_sleep(ms);
}

uint64_t time_us_64(void) {
    return (uint64_t)(emscripten_get_now() * 1000.0);
}

uint32_t get_rand_32(void) {
    uint32_t v;
    getentropy(&v, sizeof(v));
    return v;
}

void st7789_init(void) {
}

void st7789_begin_frame(void) {
    fb_y = 0;
}

void st7789_write_line(const uint16_t *line) {
    uint8_t *p = fb + fb_y * UI_W * 4;
    for (int x = 0; x < UI_W; x++, p += 4) {
        uint16_t c = line[x];
        p[0] = (uint8_t)((c >> 11) * 255 / 31);
        p[1] = (uint8_t)((c >> 5 & 63) * 255 / 63);
        p[2] = (uint8_t)((c & 31) * 255 / 31);
        p[3] = 255;
    }
    if (++fb_y == UI_H) js_present(fb), fb_y = 0;
}

void buttons_init(void) {
}

int buttons_poll(void) {
    return js_key();
}

const camera_model_t camera_ov7670 = {"browser", 0, false, 0};

void camera_bus_init(const camera_model_t *model) {
    (void)model;
}

bool camera_init(const camera_model_t *model) {
    (void)model;
    return EM_ASM_INT({ return !!(navigator.mediaDevices && navigator.mediaDevices.getUserMedia); });
}

/* The scan loops call this back to back, so it waits a frame's time before taking one */
bool camera_capture(uint8_t *buf, uint32_t timeout_ms) {
    (void)timeout_ms;
    emscripten_sleep(66);
    return js_capture(buf);
}
