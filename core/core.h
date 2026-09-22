#ifndef CORE_CORE_H
#define CORE_CORE_H

/* 鍵を持つ署名中核（docs/architecture-b.md §5, §6）。plan_t は parser.wasm から来る untrusted な入力で、
 * 呼び出し側はネイティブ側にコピーしたものを渡す。core_review() で確認した plan と同じものしか core_sign() は署名しない */

#include <stddef.h>
#include <stdint.h>
#include "plan.h"

enum {
    CORE_OK = 0,
    CORE_ERR_FORMAT,       /* 上限超過、未使用フィールドが非ゼロ、額の範囲外 */
    CORE_ERR_NO_SEED,
    CORE_ERR_NOT_OURS,     /* 自分の fingerprint を名乗る入力の鍵とスクリプトが一致しない */
    CORE_ERR_NOTHING_TO_SIGN,
    CORE_ERR_SIGHASH,      /* 許可しない sighash type */
    CORE_ERR_SCRIPT,       /* 署名対象の入力が P2WPKH / P2TR 以外 */
    CORE_ERR_PREVTX_MISSING, /* SegWit v0 を含む 2 入力以上で non_witness_utxo が無い */
    CORE_ERR_PREVTX_MISMATCH,
    CORE_ERR_FEE,
    CORE_ERR_NOT_REVIEWED,
    CORE_ERR_CRYPTO,
};

typedef enum { CORE_MAINNET = 0, CORE_TESTNET = 1 } core_network_t;

typedef struct {
    const uint8_t *raw; /* non_witness_utxo。無ければ NULL */
    size_t len;
} core_prevtx_t;

typedef struct {
    uint64_t total_in, total_out, fee;
    uint8_t is_change[PLAN_MAX_OUTPUTS];
    uint8_t will_sign[PLAN_MAX_INPUTS];
    uint8_t n_sign;
} core_review_t;

typedef struct {
    uint8_t input;
    uint8_t pubkey[33]; /* P2TR は先頭 1 byte を除いた x-only 鍵 */
    uint8_t sig_len;
    uint8_t sig[73];    /* ECDSA は DER + sighash byte、Schnorr は 64 か 65 byte */
} core_sig_t;

typedef int (*core_rng_t)(uint8_t *buf, size_t len);

int core_init(core_network_t net);
int core_load_seed(const uint8_t seed[64]);
void core_unload(void);
uint32_t core_fingerprint(void);
int core_review(const plan_t *p, const core_prevtx_t prev[PLAN_MAX_INPUTS], core_review_t *r);
int core_sign(const plan_t *p, core_rng_t rng, core_sig_t sigs[PLAN_MAX_INPUTS], unsigned *n_sigs);

#endif
