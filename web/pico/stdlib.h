#ifndef WEB_PICO_STDLIB_H
#define WEB_PICO_STDLIB_H
/* The part of pico-sdk that main.c uses, backed by the browser (hal.c) */
#include <stdint.h>
void stdio_init_all(void);
void sleep_ms(uint32_t ms);
uint64_t time_us_64(void);
#endif
