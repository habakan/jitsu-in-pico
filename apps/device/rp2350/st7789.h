#ifndef ST7789_H
#define ST7789_H

#include <stdint.h>

void st7789_init(void);
/* Sends one row of 240 pixels (RGB565, host byte order) as row y. After st7789_begin_frame, rows 0
 * to 239 go in order */
void st7789_begin_frame(void);
void st7789_write_line(const uint16_t *line);

#endif
