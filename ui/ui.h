#ifndef UI_UI_H
#define UI_UI_H

/* 240x240 の確認画面。文字列はすべて core_display_t（ネイティブが plan のバイト列から作ったもの）から組み立てる。
 * 画面はフルフレームバッファを持たず、1 行ずつ RGB565 で生成して LCD に流す */

#include <stdint.h>
#include "core.h"

#define UI_W 240
#define UI_H 240
#define UI_COLS 30 /* 8x16 フォント */
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
    uint32_t seen; /* 表示した画面のビット集合。全て見るまで署名を受け付けない */
} ui_review_t;

/* 概要、出力ごとの詳細、署名確認の順に画面を組む */
void ui_review_init(ui_review_t *r, const core_display_t *d);
/* キー入力で画面を移し、署名確認画面で PUSH されたら UI_APPROVED、A なら UI_REJECTED を返す */
int ui_review_key(ui_review_t *r, int key);
void ui_render_line(const ui_screen_t *s, int y, uint16_t line[UI_W]);

/* 署名済み PSBT を返すアニメーション QR。1 パート 120 byte だと文字列が約 300 文字で QR は v8 前後、
 * 240 px に 1 モジュール 4 px で収まる */
/* 1 パートを QR v8（49 モジュール）に収める大きさ。240px の LCD で 1 モジュール 4px になり、
 * 1.54 インチでは 0.47mm。これより大きい版にすると倍率が 3px に落ちてカメラが読めない */
#define UI_UR_FRAGMENT 100
#define UI_QR_MAX_VERSION 12
/* text を QR にする。収まらなければ 0 */
int ui_qr_set(const char *text);
/* 周囲に 4 モジュールの余白を付けて、画面いっぱいに整数倍で拡大する */
void ui_qr_render_line(int y, uint16_t line[UI_W]);
/* 直前に ui_qr_set した QR の一辺のモジュール数（表示の確認用） */
int ui_qr_modules(void);

#endif
