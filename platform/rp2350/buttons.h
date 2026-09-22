#ifndef BUTTONS_H
#define BUTTONS_H

void buttons_init(void);
/* 押された瞬間のキー（UI_KEY_*）を返す。無ければ -1。10ms 程度の間隔で呼ぶ */
int buttons_poll(void);

#endif
