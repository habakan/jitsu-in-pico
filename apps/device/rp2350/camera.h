#ifndef RP2350_CAMERA_H
#define RP2350_CAMERA_H

/* One QVGA greyscale frame from a DVP camera (OV7670, OV7675 or OV2640), taking the Y of YUV422.
 * PIO picks out the Y and DMA writes it to the buffer. The sensor is configured over SCCB (I2C1) */

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
    bool needs_xclk;              /* a board without a crystal is fed XCLK from GP21 */
    const camera_reg_t *regs;     /* terminated by 0xff, 0xff; {0xfe, ms} waits ms milliseconds */
} camera_model_t;

extern const camera_model_t camera_ov7670;

/* Brings up only XCLK and SCCB (I2C1), which is as far as checking the wiring needs to go */
void camera_bus_init(const camera_model_t *model);
/* Writes the registers and sets up PIO and DMA. False if an SCCB write failed */
bool camera_init(const camera_model_t *model);
/* Captures the next frame into buf (CAMERA_W * CAMERA_H bytes, 4-byte aligned), giving up after
 * timeout_ms */
bool camera_capture(uint8_t *buf, uint32_t timeout_ms);
/* Reads one register over SCCB, for checking the wiring */
bool camera_read_reg(uint8_t reg, uint8_t *val);

#endif
