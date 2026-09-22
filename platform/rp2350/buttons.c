#include "buttons.h"
#include "board_pins.h"
#include "hardware/gpio.h"
#include "ui.h"

/* UI_KEY_* の順。ジョイスティックは基板上で 10kΩ プルアップ済み、タクトスイッチは内蔵プルアップで GND に落とす */
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

int buttons_poll(void) {
    int pressed = -1;
    for (unsigned i = 0; i < N_KEYS; i++) {
        /* 直近 4 回の読み値で、離した状態が 1 回あった後に押下が 3 回続いたら 1 回だけ押したとみなす */
        history[i] = (unsigned char)(history[i] << 1 | gpio_get(pins[i]));
        if ((history[i] & 0x0f) == 0x08 && pressed < 0) pressed = (int)i;
    }
    return pressed;
}
