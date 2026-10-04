#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/* The GPIO assignment from docs/hardware.md; change both together */

#define PIN_UART_TX 0 /* to the Debug Probe's UART; RX is unused */

#define PIN_CAM_D0 2 /* D0 to D7 are read as a run by PIO, so they sit on GP2 to GP9 */
#define PIN_CAM_PCLK 10
#define PIN_CAM_HREF 11
#define PIN_CAM_VSYNC 12
#define PIN_CAM_SIOD 14 /* I2C1 SDA */
#define PIN_CAM_SIOC 15 /* I2C1 SCL */
#define PIN_CAM_XCLK 21 /* CLOCK GPOUT0; unused on an OV2640 board that has its own crystal */

#define PIN_LCD_DC 16
#define PIN_LCD_SCK 18 /* SPI0 SCK */
#define PIN_LCD_MOSI 19 /* SPI0 TX; CS is tied to ground, RES and BLK to 3V3 */

#define PIN_JOY_UP 13
#define PIN_JOY_DOWN 17
#define PIN_JOY_LEFT 20
#define PIN_JOY_RIGHT 22
#define PIN_JOY_PUSH 26
#define PIN_BTN_A 27
#define PIN_BTN_B 28
#define PIN_BTN_C 1

#endif
