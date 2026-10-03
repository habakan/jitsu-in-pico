#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/* docs/hardware.md の GPIO 割り当て。変えるときは両方を直す */

#define PIN_UART_TX 0 /* Debug Probe の UART へ。RX は使わない */

#define PIN_CAM_D0 2 /* D0〜D7 は PIO で連続して読むので GP2〜GP9 に並べる */
#define PIN_CAM_PCLK 10
#define PIN_CAM_HREF 11
#define PIN_CAM_VSYNC 12
#define PIN_CAM_SIOD 14 /* I2C1 SDA */
#define PIN_CAM_SIOC 15 /* I2C1 SCL */
#define PIN_CAM_XCLK 21 /* CLOCK GPOUT0。水晶を載せた OV2640 基板では使わない */

#define PIN_LCD_DC 16
#define PIN_LCD_SCK 18 /* SPI0 SCK */
#define PIN_LCD_MOSI 19 /* SPI0 TX。CS は GND、RES と BLK は 3V3 に固定 */

#define PIN_JOY_UP 13
#define PIN_JOY_DOWN 17
#define PIN_JOY_LEFT 20
#define PIN_JOY_RIGHT 22
#define PIN_JOY_PUSH 26
#define PIN_BTN_A 27
#define PIN_BTN_B 28
#define PIN_BTN_C 1

#endif
