#ifndef CORE_TX_H
#define CORE_TX_H

#include <stddef.h>
#include <stdint.h>

/* 直列化された取引を 1 回走査し、入出力ごとにコールバックを呼ぶ。スクリプトは解釈しない。
 * txid は witness を除いた部分の SHA256d（内部バイト順）。 */
typedef struct {
    int (*on_input)(void *ctx, uint32_t index, const uint8_t prevout[36], size_t script_sig_len, uint32_t sequence);
    int (*on_output)(void *ctx, uint32_t index, uint64_t amount, const uint8_t *spk, size_t spk_len);
    void *ctx;
} tx_visitor_t;

typedef struct {
    int32_t version;
    uint32_t locktime, n_inputs, n_outputs;
    uint8_t segwit; /* marker / flag 付きで直列化されていた */
    uint8_t txid[32];
} tx_info_t;

int tx_parse(const uint8_t *raw, size_t len, const tx_visitor_t *v, tx_info_t *info);

#endif
