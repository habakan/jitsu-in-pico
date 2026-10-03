#ifndef BUTTONS_H
#define BUTTONS_H

void buttons_init(void);
/* 押された瞬間のキー（UI_KEY_*）を返す。無ければ -1。10ms 程度の間隔で呼ぶ */
int buttons_poll(void);
/* UI_KEY_* の順に、離していれば 1 のビット（配線の確認用） */
unsigned buttons_raw(void);

#endif
