#include "camera.h"

/* OV7670 / OV7675（どちらも SCCB 0x21、レジスタ互換）: YUV422 を 1/2 に縮小して QVGA で出す。OmniVision の実装ガイドにある QVGA YUV の定番設定。
 * 露出・ゲイン・ホワイトバランスはリセット時の自動のまま。実機で QR の読み取りを見て詰める */
static const camera_reg_t regs[] = {
    {0x12, 0x80}, /* COM7: リセット */
    {0xfe, 100},
    {0x11, 0x01}, /* CLKRC: 内部クロック = XCLK / 2 */
    {0x12, 0x00}, /* COM7: YUV */
    {0x0c, 0x04}, /* COM3: 縮小を有効にする */
    {0x3e, 0x19}, /* COM14: 縮小に合わせて PCLK を 1/2 */
    {0x70, 0x3a}, /* SCALING_XSC */
    {0x71, 0x35}, /* SCALING_YSC */
    {0x72, 0x11}, /* SCALING_DCWCTR: 縦横 1/2 */
    {0x73, 0xf1}, /* SCALING_PCLK_DIV: 1/2 */
    {0xa2, 0x02}, /* SCALING_PCLK_DELAY */
    {0x3a, 0x04}, /* TSLB: COM13[0] と合わせて Y U Y V の順 */
    {0x3d, 0x88}, /* COM13: ガンマ有効、UV 自動、順序ビット 0 */
    {0x40, 0xc0}, /* COM15: 出力範囲 00〜FF */
    {0xff, 0xff},
};

const camera_model_t camera_ov7670 = {"OV7670/OV7675", 0x21, true, regs};
