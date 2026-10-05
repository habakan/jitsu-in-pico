/* Checks camera.pio's capture on real silicon with no camera attached: another state machine in the
 * same PIO generates the DVP waveform and the capture side reads those same pins, which needs no
 * wiring because PIO inputs read the pads. The same thing tools/sim_dvp_pio.py checks in simulation.
 *
 * **Do not run this with the camera connected.** It drives the same pins, so the two outputs fight
 * and what gets read is neither waveform. Unplug all eleven of D0-D7, PCLK, HREF and VSYNC first */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "board_pins.h"
#include "camera.pio.h"
#include "dvp_gen.pio.h"
#include "hardware/dma.h"
#include "hardware/pio.h"

#define W 64 /* pixels captured; kept small because the waveform lives in RAM */
#define H 8
#define SW (2 * W) /* pixels the sensor emits; PIO halves both directions */
#define SH (2 * H)
#define HBLANK 16    /* pixels between rows */
#define VSYNC_PX 128 /* the VSYNC pulse, in pixels */
#define FRAMES 3
/* A pixel is two bytes, Y and U; a byte is two samples, PCLK low then high */
#define LINE_S (4 * (SW + HBLANK))
#define FRAME_S (4 * VSYNC_PX + SH * LINE_S)

static uint32_t wave[FRAMES * FRAME_S / 2];
static uint8_t expect[FRAMES][W * H];
static uint8_t captured[W * H] __attribute__((aligned(4)));
static unsigned n_samples;
static uint8_t sensor_line0[8]; /* the first 8 bytes of the first row of the first frame (Y U Y U ...) */

static uint32_t rnd(void) {
    static uint32_t s = 12345;
    s = s * 1103515245u + 12345u;
    return s >> 16;
}

/* One sample: bits 0-7 are D0-D7, bit 8 PCLK, bit 9 HREF, bit 10 VSYNC; OUT is based at D0 */
static void put(unsigned i, uint8_t d, int pclk, int href, int vsync) {
    uint32_t s = (uint32_t)d | (uint32_t)pclk << 8 | (uint32_t)href << 9 | (uint32_t)vsync << 10;
    wave[i / 2] = i % 2 ? (wave[i / 2] & 0xffff) | s << 16 : s;
}

static void build_wave(void) {
    unsigned i = 0;
    for (int f = 0; f < FRAMES; f++) {
        for (int k = 0; k < 4 * VSYNC_PX; k++) put(i++, 0, k % 2, 0, 1);
        for (int y = 0; y < SH; y++) {
            for (int x = 0; x < SW; x++) {
                /* The values say where in the phase we are: Y is twice the pixel number, U and V are 0xaa */
                uint8_t lum = (uint8_t)(2 * x + f), chroma = 0xaa;
                /* Only even pixels of even rows get captured */
                if (y % 2 == 0 && x % 2 == 0) expect[f][(y / 2) * W + x / 2] = lum;
                if (f == 0 && y == 0 && x < 4) sensor_line0[2 * x] = lum, sensor_line0[2 * x + 1] = chroma;
                /* Data changes while PCLK is low and is settled when it goes high */
                put(i++, lum, 0, 1, 0);
                put(i++, lum, 1, 1, 0);
                put(i++, chroma, 0, 1, 0);
                put(i++, chroma, 1, 1, 0);
            }
            for (int k = 0; k < 4 * HBLANK; k++) put(i++, (uint8_t)rnd(), k % 2, 0, 0);
        }
    }
    n_samples = i;
}

static PIO pio = pio0;
static uint sm_gen, sm_cap, off_gen, off_cap, dma_gen, dma_cap;

static void gen_start(float clkdiv) {
    dma_channel_config c = dma_channel_get_default_config(dma_gen);
    pio_sm_set_enabled(pio, sm_gen, false);
    pio_sm_clear_fifos(pio, sm_gen);
    pio_sm_restart(pio, sm_gen);
    pio_sm_exec(pio, sm_gen, pio_encode_jmp(off_gen));
    pio_sm_set_clkdiv(pio, sm_gen, clkdiv);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm_gen, true));
    dma_channel_configure(dma_gen, &c, &pio->txf[sm_gen], wave, n_samples / 2, true);
    pio_sm_set_enabled(pio, sm_gen, true);
}

/* Arming and waiting are separate: arm after the generator starts and the first frame is already gone */
static void cap_arm(void) {
    dma_channel_config c = dma_channel_get_default_config(dma_cap);

    memset(captured, 0, sizeof(captured));
    pio_sm_set_enabled(pio, sm_cap, false);
    pio_sm_clear_fifos(pio, sm_cap);
    pio_sm_restart(pio, sm_cap);
    pio_sm_exec(pio, sm_cap, pio_encode_jmp(off_cap));
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm_cap, false));
    dma_channel_configure(dma_cap, &c, captured, &pio->rxf[sm_cap], W * H / 4, true);
    pio_sm_put_blocking(pio, sm_cap, W - 1);
    pio_sm_set_enabled(pio, sm_cap, true);
}

static bool cap_wait(uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (dma_channel_is_busy(dma_cap) || dma_channel_hw_addr(dma_cap)->transfer_count)
        if (time_reached(deadline)) {
            dma_channel_abort(dma_cap);
            pio_sm_set_enabled(pio, sm_cap, false);
            return false;
        }
    pio_sm_set_enabled(pio, sm_cap, false);
    return true;
}

/* Sets up the capture side. The generator drives the pins, so pindirs and the gpio assignment are
 * left alone */
static void cap_init(void) {
    pio_sm_config c = dvp_y_program_get_default_config(off_cap);
    sm_config_set_in_pins(&c, PIN_CAM_D0);
    sm_config_set_in_shift(&c, true, true, 32);
    pio_sm_init(pio, sm_cap, off_cap, &c);
}

static int run(const char *name, float clkdiv, int mid_frame) {
    uint64_t t;
    int ok, frame = mid_frame ? 1 : 0;
    /* Three cycles per sample, so this is how long a frame takes at 150MHz */
    unsigned frame_us = (unsigned)(FRAME_S * 3 * clkdiv / 150);

    if (mid_frame) {
        gen_start(clkdiv);
        sleep_us(frame_us / 2); /* arming mid-frame should still capture the next one whole */
        t = time_us_64();
        cap_arm();
    } else {
        t = time_us_64();
        cap_arm();
        gen_start(clkdiv);
    }
    ok = cap_wait(500);
    t = time_us_64() - t;
    /* Armed mid-frame, which frame arrives next depends on the start-up delay. What is being checked is
     * that some whole frame comes out, never a picture torn across two */
    if (ok && mid_frame)
        for (frame = 0; frame < FRAMES && memcmp(captured, expect[frame], sizeof(captured)); frame++);
    if (ok) ok = frame < FRAMES && !memcmp(captured, expect[frame], sizeof(captured));
    printf("%-22s %s (%llu us, frame %dx%d = %u us%s", name, ok ? "ok" : "NG", (unsigned long long)t, W, H, frame_us,
           ok && mid_frame ? ", got frame " : "");
    if (ok && mid_frame) printf("%d", frame);
    printf(")\n");
    if (!ok) {
        printf("  got :");
        for (int i = 0; i < 8; i++) printf(" %02x", captured[i]);
        printf("\n  want:");
        for (int i = 0; i < 8; i++) printf(" %02x", expect[frame][i]);
        printf("\n  sensor row 1:");
        for (int i = 0; i < 8; i++) printf(" %02x", sensor_line0[i]);
        printf("\n  wave[0..3]: %08x %08x %08x %08x  samples %u\n", wave[0], wave[1], wave[2], wave[3], n_samples);
        printf("  gen dma remaining %u / %u\n", (unsigned)dma_channel_hw_addr(dma_gen)->transfer_count, n_samples / 2);
        /* Runs only the generator, to look at what is actually on the pins */
        gen_start(16.0f);
        printf("  pins:");
        for (int k = 0; k < 10; k++) {
            uint32_t v = gpio_get_all() >> PIN_CAM_D0;
            printf(" %02x/%c%c%c", v & 0xff, v >> 8 & 1 ? 'P' : '-', v >> 9 & 1 ? 'H' : '-', v >> 10 & 1 ? 'V' : '-');
            busy_wait_us(7);
        }
        printf("\n");
        dma_channel_abort(dma_gen);
    }
    dma_channel_abort(dma_gen);
    return !ok;
}

int main(void) {
    int failures = 0;

    stdio_init_all();
    printf("\npio_loopback_test: sensor %dx%d decimated to %dx%d, PCLK %u kHz at clkdiv 1\n", SW, SH, W, H,
           (unsigned)(clock_get_hz(clk_sys) / 6000));
    printf("unplug the camera's D0-D7, PCLK, HREF and VSYNC first, or the outputs fight\n");
    build_wave();
    sleep_ms(1); /* the first time_us_64() returns 0, so burn one before timing anything */

    off_gen = pio_add_program(pio, &dvp_gen_program);
    off_cap = pio_add_program(pio, &dvp_y_program);
    sm_gen = (uint)pio_claim_unused_sm(pio, true);
    sm_cap = (uint)pio_claim_unused_sm(pio, true);
    dma_gen = (uint)dma_claim_unused_channel(true);
    dma_cap = (uint)dma_claim_unused_channel(true);
    dvp_gen_program_init(pio, sm_gen, off_gen, PIN_CAM_D0, 1.0f);
    cap_init();

    failures += run("PCLK 1.5MHz", 16.0f, 0);
    failures += run("PCLK 6.25MHz", 4.0f, 0);
    failures += run("PCLK 6.25MHz mid-frame", 4.0f, 1);
    failures += run("PCLK 25MHz", 1.0f, 0);
    failures += run("PCLK 25MHz mid-frame", 1.0f, 1);

    printf("%s\ndone\n", failures ? "FAILED" : "all ok");
    while (1) tight_loop_contents();
}
