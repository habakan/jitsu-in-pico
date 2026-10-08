#include "st7789.h"
#include "board_pins.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/time.h"

#define LCD_SPI spi0
#define LCD_W 240
#define LCD_H 240
/* Kept modest because this is wired with jumper leads; the ST7789 itself takes writes up to 62.5MHz */
#define LCD_BAUD (40 * 1000 * 1000)

static void cmd(uint8_t c, const uint8_t *data, unsigned n) {
    gpio_put(PIN_LCD_DC, 0);
    spi_write_blocking(LCD_SPI, &c, 1);
    gpio_put(PIN_LCD_DC, 1);
    if (n) spi_write_blocking(LCD_SPI, data, n);
}

void st7789_init(void) {
    static const uint8_t colmod = 0x55, madctl = 0x00;
    static const uint8_t caset[4] = {0, 0, 0, LCD_W - 1}, raset[4] = {0, 0, 0, LCD_H - 1};

    spi_init(LCD_SPI, LCD_BAUD);
    /* CS is tied to ground, so there is no falling edge to synchronise on; mode 3 is used instead */
    spi_set_format(LCD_SPI, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    gpio_set_function(PIN_LCD_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_LCD_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_LCD_DC);
    gpio_set_dir(PIN_LCD_DC, GPIO_OUT);

    cmd(0x01, NULL, 0); /* SWRESET; RES is tied to 3V3, so the software reset is all there is */
    sleep_ms(150);
    cmd(0x11, NULL, 0); /* SLPOUT */
    sleep_ms(120);
    cmd(0x3a, &colmod, 1); /* 16bit/pixel */
    cmd(0x36, &madctl, 1);
    cmd(0x2a, caset, 4);
    cmd(0x2b, raset, 4);
    cmd(0x21, NULL, 0); /* INVON; without it an IPS panel shows inverted colours */
    cmd(0x13, NULL, 0); /* NORON */
    cmd(0x29, NULL, 0); /* DISPON */
}

void st7789_begin_frame(void) {
    cmd(0x2c, NULL, 0); /* RAMWR; writes from here fill the window from its top left */
}

void st7789_write_line(const uint16_t *line) {
    uint8_t buf[LCD_W * 2];
    for (int x = 0; x < LCD_W; x++) buf[2 * x] = (uint8_t)(line[x] >> 8), buf[2 * x + 1] = (uint8_t)line[x];
    spi_write_blocking(LCD_SPI, buf, sizeof(buf));
}
