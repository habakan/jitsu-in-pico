#ifndef HOST_ABI_PARSER_HOST_H
#define HOST_ABI_PARSER_HOST_H

/* The host side of parser.wasm. Every address and length it returns is checked against the bounds of
 * the linear memory before anything is copied out */

#include <stddef.h>
#include <stdint.h>
#include "core.h"

#define PARSER_PSBT_MAX 32768

int parser_host_init(const uint8_t *wasm, uint32_t wasm_len, void *pool, uint32_t pool_size);
/* Returns 1 on success and puts parser_parse's result in *rc, where 0 means accepted. plan and prev
 * are only filled in when it was accepted, and prev[i].raw points into the arena */
int parser_host_parse(const uint8_t *psbt, uint32_t len, uint32_t fingerprint, uint32_t *rc, plan_t *plan,
                      core_prevtx_t prev[PLAN_MAX_INPUTS], uint8_t *arena, size_t arena_cap);
/* Anti-exfil: the coordinator's n per input as parser.wasm read it (1 then n, 16 x 33 bytes), and the Q of
 * each signature for parser_host_finalize to add (zero for none). 1 on success */
int parser_host_nonces(uint8_t nonces[PLAN_MAX_INPUTS][33]);
int parser_host_set_points(const uint8_t points[PLAN_MAX_INPUTS][33]);
/* Hands the signatures to parser.wasm and takes the signed PSBT in out. 1 on success */
int parser_host_finalize(const core_sig_t *sigs, unsigned n, uint8_t *out, size_t cap, uint32_t *out_len);

/* Feeds one part of an animated QR, as the string the QR held, and puts parser_ur_receive's result in
 * *rc. An *rc above 0 means the PSBT is complete, and that many bytes are copied to psbt */
int parser_host_ur_reset(void);
int parser_host_ur_receive(const char *part, uint32_t len, int32_t *rc, uint8_t *psbt, size_t cap);
/* Called after parser_host_finalize. Turns the signed PSBT into a crypto-psbt UR and returns the
 * number of pure parts, or a negative value on failure */
int32_t parser_host_ur_encode_start(uint32_t len, uint32_t max_fragment_len);
/* Any byte string as a UR, for testing the host: writes to the parser's output buffer, then encodes */
int32_t parser_host_ur_encode_bytes(const uint8_t *data, uint32_t len, uint32_t max_fragment_len);
/* Writes the next part's string to text, NUL-terminated. 1 on success */
int parser_host_ur_encode_next(char *text, size_t cap);
/* The high-water mark of the WAMR pool, measured to decide how big the pool needs to be */
uint32_t parser_host_pool_highmark(void);

#endif
