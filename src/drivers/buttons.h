#ifndef BUTTONS_H
#define BUTTONS_H

void buttons_init(void);
/* The key (UI_KEY_*) at the moment it goes down, or -1. Meant to be called every 10ms or so */
int buttons_poll(void);
/* One bit per key in UI_KEY_* order, set when it is released; for checking the wiring */
unsigned buttons_raw(void);

#endif
