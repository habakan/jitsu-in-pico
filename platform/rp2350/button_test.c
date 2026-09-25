/* 配線の確認用。GP2〜GP28 を内蔵プルアップ付きの入力にして、GND に落ちたピンを UART に出す。
 * どのピンが実際につながっているかが分かるので、ブレッドボードの向きの取り違えを切り分けられる */
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

int main(void) {
    uint32_t prev;

    stdio_init_all();
    for (unsigned gp = 2; gp <= 28; gp++) {
        gpio_init(gp);
        gpio_set_dir(gp, GPIO_IN);
        gpio_pull_up(gp);
    }
    sleep_ms(50);
    printf("\nbutton_test: GP2-GP28 pulled up. Short a pin to GND.\n");
    prev = gpio_get_all();
    for (;;) {
        uint32_t now = gpio_get_all(), diff = (now ^ prev) & 0x1ffffffcu;
        for (unsigned gp = 2; gp <= 28 && diff; gp++) {
            if (!(diff >> gp & 1)) continue;
            printf("GP%u %s\n", gp, (now >> gp & 1) ? "released" : "PRESSED");
            diff &= ~(1u << gp);
        }
        prev = now;
        sleep_ms(5);
    }
}
