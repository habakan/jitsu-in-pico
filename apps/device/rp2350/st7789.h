#ifndef ST7789_H
#define ST7789_H

#include <stdint.h>

void st7789_init(void);
/* 1 行 240 画素（RGB565、ホストのバイト順）を y 行目に送る。st7789_begin_frame の後に 0〜239 行を順に送る */
void st7789_begin_frame(void);
void st7789_write_line(const uint16_t *line);

#endif
