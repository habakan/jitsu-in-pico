#include "camera.h"
#include "board_pins.h"
#include "camera.pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "pico/time.h"

#define SCCB i2c1
#define SCCB_BAUD (100 * 1000)
/* 150MHz / 6 = 25MHz; the OV7670 takes an XCLK of 10 to 48MHz */
#define XCLK_DIV 6

static PIO pio = pio0;
static uint sm, offset;
static int dma_ch = -1;
static const camera_model_t *model;

static bool sccb_write(uint8_t reg, uint8_t val) {
    uint8_t b[2] = {reg, val};
    return i2c_write_blocking(SCCB, model->sccb_addr, b, 2, false) == 2;
}

/* SCCB does not expect a repeated start, so the write is stopped before the read */
bool camera_read_reg(uint8_t reg, uint8_t *val) {
    return i2c_write_blocking(SCCB, model->sccb_addr, &reg, 1, false) == 1 &&
           i2c_read_blocking(SCCB, model->sccb_addr, val, 1, false) == 1;
}

/* Clock and SCCB first, with no data bus yet, so the wiring can be checked a step at a time */
void camera_bus_init(const camera_model_t *m) {
    model = m;
    if (m->needs_xclk) clock_gpio_init(PIN_CAM_XCLK, CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS, XCLK_DIV);
    sleep_ms(10);

    i2c_init(SCCB, SCCB_BAUD);
    gpio_set_function(PIN_CAM_SIOD, GPIO_FUNC_I2C);
    gpio_set_function(PIN_CAM_SIOC, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_CAM_SIOD); /* in case the board has no 4.7k; the internal pull-up is weak, so an
                                 * external one is better */
    gpio_pull_up(PIN_CAM_SIOC);
}

bool camera_init(const camera_model_t *m) {
    for (const camera_reg_t *r = m->regs; !(r->reg == 0xff && r->val == 0xff); r++) {
        if (r->reg == 0xfe) {
            sleep_ms(r->val);
            continue;
        }
        if (!sccb_write(r->reg, r->val)) return false;
    }

    offset = pio_add_program(pio, &dvp_y_program);
    sm = (uint)pio_claim_unused_sm(pio, true);
    dvp_y_program_init(pio, sm, offset, PIN_CAM_D0);
    dma_ch = dma_claim_unused_channel(true);
    return true;
}

bool camera_capture(uint8_t *buf, uint32_t timeout_ms) {
    dma_channel_config c = dma_channel_get_default_config((uint)dma_ch);
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

    /* The state machine is restarted from the top, waiting on VSYNC, so capture never begins partway
     * through a frame */
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_exec(pio, sm, pio_encode_jmp(offset));

    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));
    dma_channel_configure((uint)dma_ch, &c, buf, &pio->rxf[sm], CAMERA_W * CAMERA_H / 4, true);

    pio_sm_put_blocking(pio, sm, CAMERA_W - 1);
    pio_sm_set_enabled(pio, sm, true);
    while (dma_channel_is_busy((uint)dma_ch)) {
        if (time_reached(deadline)) {
            dma_channel_abort((uint)dma_ch);
            pio_sm_set_enabled(pio, sm, false);
            return false;
        }
    }
    pio_sm_set_enabled(pio, sm, false);
    return true;
}
