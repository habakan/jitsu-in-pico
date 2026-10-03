#include "camera.h"

/* OV7670 / OV7675（どちらも SCCB 0x21、レジスタ互換）: VGA の YUV422 をそのまま出させる。
 * QVGA への間引きは PIO 側でやる（OV7675 ではスケーラの縦が効かず、1 フレーム 480 行のままだった）。
 * 露出・ゲイン・ホワイトバランスはリセット時の自動のまま。実機で QR の読み取りを見て詰める */
static const camera_reg_t regs[] = {
    {0x12, 0x80}, /* COM7: リセット */
    {0xfe, 100},
    {0x11, 0x00}, /* CLKRC: 内部クロック = XCLK（分周しない） */
    {0x12, 0x00}, /* COM7: YUV、VGA */
    {0x3a, 0x04}, /* TSLB: COM13[0] と合わせて Y U Y V の順 */
    {0x3d, 0x88}, /* COM13: ガンマ有効、UV 自動、順序ビット 0 */
    {0x40, 0xc0}, /* COM15: 出力範囲 00〜FF */
    {0xff, 0xff},
};

const camera_model_t camera_ov7670 = {"OV7670/OV7675", 0x21, true, regs};
