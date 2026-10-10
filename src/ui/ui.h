#ifndef UI_UI_H
#define UI_UI_H

/* The 240x240 review screens. Every string comes from core_display_t, which the native side built
 * from the plan's bytes. There is no full framebuffer: each row is generated as RGB565 and streamed
 * to the LCD */

#include <stdint.h>
#include "core.h"

#define UI_W 240
#define UI_H 240
/* Shown at the top right of every screen; must fit within UI_COLS */
#define UI_APP_NAME "Baremetal wasm DIY Signer"
#define UI_COLS 30 /* with the 8x16 font */
#define UI_ROWS 15
#define UI_MAX_SCREENS (PLAN_MAX_OUTPUTS + 2)

typedef struct {
    char text[UI_ROWS][UI_COLS + 1];
    uint16_t color[UI_ROWS];
} ui_screen_t;

enum { UI_KEY_UP, UI_KEY_DOWN, UI_KEY_LEFT, UI_KEY_RIGHT, UI_KEY_PUSH, UI_KEY_A, UI_KEY_B, UI_KEY_C };
enum { UI_PENDING = 0, UI_APPROVED, UI_REJECTED };

typedef struct {
    ui_screen_t screens[UI_MAX_SCREENS];
    uint8_t n, cur;
    uint32_t seen; /* which screens have been shown; signing is refused until every one has been */
} ui_review_t;

/* The screens, in order: the summary, one per output, then the confirmation */
void ui_review_init(ui_review_t *r, const core_display_t *d);
/* Moves between screens on a key press. On the confirmation screen, PUSH gives UI_APPROVED and A
 * gives UI_REJECTED */
int ui_review_key(ui_review_t *r, int key);
void ui_render_line(const ui_screen_t *s, int y, uint16_t line[UI_W]);

/* A list to pick from. Two buttons are enough: one moves the cursor, the other selects */
#define UI_MENU_MAX 6
enum { UI_MENU_PENDING = -1, UI_MENU_BACK = -2 };

typedef struct {
    ui_screen_t screen;
    const char *title;
    const char *const *items;
    unsigned n, cur;
} ui_menu_t;

void ui_menu_init(ui_menu_t *m, const char *title, const char *const *items, unsigned n);
/* Returns the chosen item's index, or UI_MENU_PENDING or UI_MENU_BACK */
int ui_menu_key(ui_menu_t *m, int key);
/* A text-only screen, for startup, errors and warnings */
void ui_message(ui_screen_t *s, const char *title, const char *body, int warn);

/* The animated QR that hands back the signed PSBT. At 120 bytes per part the string runs to about
 * 300 characters, which lands around QR v8 and fits 240 px at 4 px per module */
/* Sized so one part fits QR v8 (49 modules). That is 4 px per module on a 240 px LCD, or 0.47 mm on
 * a 1.54 inch panel. A larger version drops the scale to 3 px, which a camera cannot read */
#define UI_UR_FRAGMENT 100
#define UI_QR_MAX_VERSION 12
/* Encodes text as a QR, or returns 0 if it does not fit */
/* The account xpub screen; the string is wrapped so all of it is shown. purpose 48 is m/48h/coin/0h/2h */
void ui_xpub(ui_screen_t *s, const char *xpub, uint32_t fp, int testnet, unsigned purpose);

/* Shows the hash of the wasm actually loaded */
void ui_hash(ui_screen_t *s, const char *name, unsigned len, const uint8_t h[32]);

int ui_qr_set(const char *text);
/* Adds the four-module quiet zone and scales by a whole number to fill the screen */
void ui_qr_render_line(int y, uint16_t line[UI_W]);

/* Steps the brightness of the QR's white. Too bright saturates the camera and cannot be read */
void ui_qr_level(int delta);
int ui_qr_level_get(void);
/* How many modules a side the last ui_qr_set produced, for checking what is on screen */
int ui_qr_modules(void);

#endif
