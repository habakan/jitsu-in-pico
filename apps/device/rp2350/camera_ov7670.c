#include "camera.h"

/* OV7670 and OV7675, both at SCCB 0x21 with compatible registers: left to emit VGA YUV422 as is.
 * The decimation to QVGA happens in PIO, because the OV7675's vertical scaler had no effect and kept
 * sending 480 rows. Exposure, gain and white balance stay on the automatic defaults; they get tuned
 * by watching what actually reads a QR on the hardware */
static const camera_reg_t regs[] = {
    {0x12, 0x80}, /* COM7: reset */
    {0xfe, 100},
    {0x11, 0x00}, /* CLKRC: internal clock = XCLK, no division */
    {0x12, 0x00}, /* COM7: YUV、VGA */
    {0x3a, 0x04}, /* TSLB: with COM13[0], gives the order Y U Y V */
    {0x3d, 0x88}, /* COM13: gamma on, UV automatic, order bit 0 */
    {0x40, 0xc0}, /* COM15: output range 00 to FF */
    {0xff, 0xff},
};

const camera_model_t camera_ov7670 = {"OV7670/OV7675", 0x21, true, regs};
