/* For checking the wiring: GP2 to GP28 as inputs with the internal pull-ups, reporting on the UART
 * whichever pin goes to ground. Shows which pin is actually connected, which is how a breadboard put
 * in the wrong way round gets found */
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
