#ifndef RP2350_CAMERA_H
#define RP2350_CAMERA_H

/* DVP カメラ（OV7670 / OV7675 / OV2640）から QVGA のグレースケール（YUV422 の Y）を 1 フレーム取り込む。
 * PIO で Y を拾って DMA でバッファに書く。センサーの設定は SCCB（I2C1）で行う */

#include <stdbool.h>
#include <stdint.h>

#define CAMERA_W 320
#define CAMERA_H 240

typedef struct {
    uint8_t reg, val;
} camera_reg_t;

typedef struct {
    const char *name;
    uint8_t sccb_addr;            /* 7bit */
    bool needs_xclk;              /* 水晶の無い基板は GP21 から XCLK を与える */
    const camera_reg_t *regs;     /* 0xff, 0xff で終わる。{0xfe, ms} は ms ミリ秒待つ */
} camera_model_t;

extern const camera_model_t camera_ov7670;

/* XCLK と SCCB（I2C1）だけ用意する。配線の確認はここまでで足りる */
void camera_bus_init(const camera_model_t *model);
/* レジスタを設定し、PIO と DMA を用意する。SCCB の書き込みに失敗したら false */
bool camera_init(const camera_model_t *model);
/* 次のフレームを buf（CAMERA_W * CAMERA_H byte、4 byte 境界）に取り込む。timeout_ms で諦める */
bool camera_capture(uint8_t *buf, uint32_t timeout_ms);
/* SCCB で 1 レジスタ読む（配線の確認用） */
bool camera_read_reg(uint8_t reg, uint8_t *val);

#endif
