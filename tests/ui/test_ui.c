#include <stdio.h>
#include <string.h>
#include "ui.h"

static int failures, checks;
#define CHECK(cond, msg)                                                                                               \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(cond)) {                                                                                                 \
            failures++;                                                                                                \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg);                                                        \
        }                                                                                                              \
    } while (0)

static void display3(core_display_t *d) {
    memset(d, 0, sizeof(*d));
    d->n_outputs = 3, d->fee = 1000, d->spend = 100000;
    strcpy(d->outputs[0].text, "bc1q5knnnmmfe7xqdr55g43jyec388dtsah27rxamf");
    d->outputs[1].owner = CORE_OUT_CHANGE;
    strcpy(d->outputs[1].text, "bc1q8c6fshw2dlwun7ekn9qwf37cu2rn755upcp6el");
    d->outputs[2].text_kind = CORE_TEXT_SCRIPT;
    memset(d->outputs[2].text, 'a', 2 * PLAN_MAX_SPK); /* the longest script in hex, 166 characters */
}

int main(void) {
    static ui_review_t r;
    core_display_t d;

    display3(&d);
    ui_review_init(&r, &d);
    CHECK(r.n == 6, "summary + 3 outputs + confirm + cancel");
    CHECK(ui_review_key(&r, UI_KEY_PUSH) == UI_PENDING, "push on summary does nothing");
    for (int i = 0; i < 4; i++) ui_review_key(&r, UI_KEY_RIGHT);
    CHECK(r.cur == 4 && ui_review_key(&r, UI_KEY_PUSH) == UI_APPROVED, "approve after seeing all");

    /* The cancel screen is last: pressing in rejects, moving on wraps to the first */
    ui_review_init(&r, &d);
    for (int i = 0; i < 5; i++) ui_review_key(&r, UI_KEY_DOWN);
    CHECK(r.cur == 5, "cancel screen is last");
    CHECK(ui_review_key(&r, UI_KEY_PUSH) == UI_REJECTED, "push on cancel screen rejects");
    ui_review_init(&r, &d);
    for (int i = 0; i < 6; i++) ui_review_key(&r, UI_KEY_DOWN);
    CHECK(r.cur == 0, "next wraps to the start");

    /* Skipping a screen cannot reach the confirmation anyway, but a gap in seen is checked directly too */
    ui_review_init(&r, &d);
    r.cur = 4;
    r.seen |= 1u << 4;
    CHECK(ui_review_key(&r, UI_KEY_PUSH) == UI_PENDING, "unseen outputs block approval");
    CHECK(ui_review_key(&r, UI_KEY_A) == UI_REJECTED, "cancel anywhere");

    /* Even the longest script is shown in full, not truncated: 30 characters over 6 rows */
    ui_review_init(&r, &d);
    {
        size_t shown = 0;
        /* Row 1 is the application name and row 2 the heading, so the hex starts at row 7 */
        for (int row = 6; row < UI_ROWS - 1; row++) shown += strlen(r.screens[3].text[row]);
        CHECK(shown == 2 * PLAN_MAX_SPK, "longest script fully shown");
    }
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures != 0;
}
