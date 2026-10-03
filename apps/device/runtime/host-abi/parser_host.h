#ifndef HOST_ABI_PARSER_HOST_H
#define HOST_ABI_PARSER_HOST_H

/* parser.wasm を呼ぶホスト側。parser.wasm が返すアドレスと長さは、線形メモリ内か確かめてからコピーする */

#include <stddef.h>
#include <stdint.h>
#include "core.h"

#define PARSER_PSBT_MAX 32768

int parser_host_init(const uint8_t *wasm, uint32_t wasm_len, void *pool, uint32_t pool_size);
/* 成功なら 1 を返し、*rc に parser_parse の戻り値（0 が受理）を入れる。受理した場合だけ plan と prev を埋める。
 * prev[i].raw は arena 内を指す */
int parser_host_parse(const uint8_t *psbt, uint32_t len, uint32_t fingerprint, uint32_t *rc, plan_t *plan,
                      core_prevtx_t prev[PLAN_MAX_INPUTS], uint8_t *arena, size_t arena_cap);
/* 署名を parser.wasm に渡して署名済み PSBT を out に受け取る。成功なら 1 */
int parser_host_finalize(const core_sig_t *sigs, unsigned n, uint8_t *out, size_t cap, uint32_t *out_len);

/* アニメーション QR の 1 パート（QR の文字列）を渡す。parser_ur_receive の戻り値を *rc に入れる。
 * *rc > 0 なら PSBT が揃っていて、psbt に *rc バイトを写す */
int parser_host_ur_reset(void);
int parser_host_ur_receive(const char *part, uint32_t len, int32_t *rc, uint8_t *psbt, size_t cap);
/* parser_host_finalize の後に呼ぶ。署名済み PSBT を crypto-psbt の UR にし、純粋なパートの数を返す（失敗は負） */
int32_t parser_host_ur_encode_start(uint32_t len, uint32_t max_fragment_len);
/* 任意のバイト列を UR にする（ホストの試験用。解析器の出力バッファへ書いてから符号化する） */
int32_t parser_host_ur_encode_bytes(const uint8_t *data, uint32_t len, uint32_t max_fragment_len);
/* 次のパートの文字列を text に書き、NUL で終える。成功なら 1 */
int parser_host_ur_encode_next(char *text, size_t cap);
/* WAMR プールの最大使用量。プールの大きさを決めるための計測用 */
uint32_t parser_host_pool_highmark(void);

#endif
