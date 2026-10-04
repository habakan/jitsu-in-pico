#include "buttons.h"
#include "board_pins.h"
#include "hardware/gpio.h"
#include "ui.h"

/* In UI_KEY_* order. The joystick has 10k pull-ups on its board; the tactile switches use the
 * internal pull-ups and pull to ground */
static const unsigned pins[] = {PIN_JOY_UP, PIN_JOY_DOWN, PIN_JOY_LEFT, PIN_JOY_RIGHT, PIN_JOY_PUSH,
                                PIN_BTN_A, PIN_BTN_B, PIN_BTN_C};
#define N_KEYS (sizeof(pins) / sizeof(pins[0]))
static unsigned char history[N_KEYS];

void buttons_init(void) {
    for (unsigned i = 0; i < N_KEYS; i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);
        history[i] = 0xff;
    }
}

unsigned buttons_raw(void) {
    unsigned v = 0;
    for (unsigned i = 0; i < N_KEYS; i++) v |= (unsigned)gpio_get(pins[i]) << i;
    return v;
}

int buttons_poll(void) {
    int pressed = -1;
    for (unsigned i = 0; i < N_KEYS; i++) {
        /* Over the last four samples: one release followed by three presses counts as a single press */
        history[i] = (unsigned char)(history[i] << 1 | gpio_get(pins[i]));
        if ((history[i] & 0x0f) == 0x08 && pressed < 0) pressed = (int)i;
    }
    return pressed;
}
