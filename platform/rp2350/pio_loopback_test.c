/* カメラ無しで camera.pio の取り込みを実機で確かめる。同じ PIO の別ステートマシンに DVP の波形を
 * 出させ、取り込み側に同じピンを読ませる（PIO の入力はパッドを見るので配線は要らない）。
 * tools/sim_dvp_pio.py と同じ観点を実シリコンで確かめるためのもの */
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "board_pins.h"
#include "camera.pio.h"
#include "dvp_gen.pio.h"
#include "hardware/dma.h"
#include "hardware/pio.h"

#define W 64          /* 1 行の画素数。波形を RAM に置くので小さくする */
#define H 8
#define HBLANK 16     /* 行間の画素数 */
#define VSYNC_PX 128  /* VSYNC パルスの長さ（画素換算） */
#define FRAMES 3
/* 1 画素は Y と U で 2 byte、1 byte は PCLK の Low と High で 2 サンプル */
#define LINE_S (4 * (W + HBLANK))
#define FRAME_S (4 * VSYNC_PX + H * LINE_S)

static uint32_t wave[FRAMES * FRAME_S / 2];
static uint8_t expect[FRAMES][W * H];
static uint8_t captured[W * H] __attribute__((aligned(4)));
static unsigned n_samples;

static uint32_t rnd(void) {
    static uint32_t s = 12345;
    s = s * 1103515245u + 12345u;
    return s >> 16;
}

/* 1 サンプル: bit0-7 が D0〜D7、bit8 が PCLK、bit9 が HREF、bit10 が VSYNC（OUT のベースは D0） */
static void put(unsigned i, uint8_t d, int pclk, int href, int vsync) {
    uint32_t s = (uint32_t)d | (uint32_t)pclk << 8 | (uint32_t)href << 9 | (uint32_t)vsync << 10;
    wave[i / 2] = i % 2 ? (wave[i / 2] & 0xffff) | s << 16 : s;
}

static void build_wave(void) {
    unsigned i = 0;
    for (int f = 0; f < FRAMES; f++) {
        for (int k = 0; k < 4 * VSYNC_PX; k++) put(i++, 0, k % 2, 0, 1);
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                uint8_t lum = (uint8_t)rnd(), chroma = (uint8_t)rnd();
                expect[f][y * W + x] = lum;
                /* データは PCLK が Low の間に変えて、High で確定させる */
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

/* 取り込みを仕掛けるところと待つところを分ける。生成より先に仕掛けないと、先頭のフレームを取り逃がす */
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

/* 取り込み側の初期化。ピンは生成側が駆動しているので、pindirs と gpio の割り当てには触らない */
static void cap_init(void) {
    pio_sm_config c = dvp_y_program_get_default_config(off_cap);
    sm_config_set_in_pins(&c, PIN_CAM_D0);
    sm_config_set_in_shift(&c, true, true, 32);
    pio_sm_init(pio, sm_cap, off_cap, &c);
}

static int run(const char *name, float clkdiv, int mid_frame) {
    uint64_t t;
    int ok, frame = mid_frame ? 1 : 0;
    /* 1 サンプル 3 サイクル。150MHz での 1 フレームの長さ */
    unsigned frame_us = (unsigned)(FRAME_S * 3 * clkdiv / 150);

    if (mid_frame) {
        gen_start(clkdiv);
        sleep_us(frame_us / 2); /* フレームの途中から仕掛けると、次のフレームが取れるはず */
        t = time_us_64();
        cap_arm();
    } else {
        t = time_us_64();
        cap_arm();
        gen_start(clkdiv);
    }
    ok = cap_wait(500);
    t = time_us_64() - t;
    /* 途中から仕掛けた場合、次にどのフレームが来るかは起動の遅れで変わる。
     * 確かめたいのは「途中で切れた絵にならず、どれか 1 フレームがそろって取れる」こと */
    if (ok && mid_frame)
        for (frame = 0; frame < FRAMES && memcmp(captured, expect[frame], sizeof(captured)); frame++)
            ;
    if (ok) ok = frame < FRAMES && !memcmp(captured, expect[frame], sizeof(captured));
    printf("%-22s %s (%llu us, frame %dx%d = %u us%s", name, ok ? "ok" : "NG", (unsigned long long)t, W, H,
           frame_us, ok && mid_frame ? ", got frame " : "");
    if (ok && mid_frame) printf("%d", frame);
    if (!ok)
        for (unsigned i = 0; i < sizeof(captured); i++)
            if (captured[i] != expect[frame][i]) {
                printf(", first diff at %u: got %02x want %02x", i, captured[i], expect[frame][i]);
                break;
            }
    printf(")\n");
    dma_channel_abort(dma_gen);
    return !ok;
}

int main(void) {
    int failures = 0;

    stdio_init_all();
    printf("\npio_loopback_test: %dx%d, PCLK %u kHz at clkdiv 1\n", W, H, (unsigned)(clock_get_hz(clk_sys) / 6000));
    build_wave();
    sleep_ms(1); /* 最初の time_us_64() が 0 を返すので、計時の前に一度動かす */

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
