/* ブラウザ用。core/address.c をそのまま使い、scriptPubKey をアドレス文字列にする */
#include "address.h"

static uint8_t spk[128];
static char out[ADDRESS_MAX];

__attribute__((export_name("addr_spk"))) uint8_t *addr_spk(void) { return spk; }
__attribute__((export_name("addr_out"))) char *addr_out(void) { return out; }
/* 成功なら 1。testnet は 0 / 1 */
__attribute__((export_name("addr_encode"))) int addr_encode(unsigned len, int testnet) {
    return len <= sizeof(spk) && address_encode(spk, len, testnet, out);
}
