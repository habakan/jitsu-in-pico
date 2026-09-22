#ifndef HOST_ABI_PLAN_H
#define HOST_ABI_PLAN_H

/* parser.wasm がネイティブへ渡す取引の構造化表現（docs/architecture-b.md §9）。
 * wasm32 / rv32 / 64bit ホストで配置が一致するよう、ポインタと long を持たせない */

#include <stddef.h>
#include <stdint.h>

#define PLAN_MAGIC       0x4e4c5042u /* "BPLN" */
#define PLAN_VERSION     1
#define PLAN_MAX_INPUTS  16
#define PLAN_MAX_OUTPUTS 16
#define PLAN_MAX_SPK     83 /* OP_RETURN の標準上限。P2TR / P2WSH は 34 */
#define PLAN_MAX_DEPTH   8

typedef struct {
    uint8_t len;
    uint8_t bytes[PLAN_MAX_SPK];
} plan_script_t;

typedef struct {
    uint8_t depth;
    uint32_t fingerprint;
    uint32_t path[PLAN_MAX_DEPTH];
} plan_keypath_t;

typedef struct {
    uint8_t prev_txid[32]; /* 内部バイト順（直列化される順） */
    uint32_t prev_vout;
    uint32_t sequence;
    uint64_t amount;    /* witness_utxo */
    plan_script_t spk;  /* witness_utxo */
    plan_keypath_t key; /* 署名しない入力は depth = 0 */
    uint8_t sighash_type;
} plan_input_t;

typedef struct {
    uint64_t amount;
    plan_script_t spk;
    plan_keypath_t key; /* お釣り候補。ネイティブが再導出で確かめる */
} plan_output_t;

typedef struct {
    uint32_t magic, version;
    int32_t tx_version;
    uint32_t locktime;
    uint8_t n_inputs, n_outputs;
    plan_input_t inputs[PLAN_MAX_INPUTS];
    plan_output_t outputs[PLAN_MAX_OUTPUTS];
} plan_t;

_Static_assert(sizeof(plan_input_t) == 176, "plan_input_t layout");
_Static_assert(sizeof(plan_output_t) == 136, "plan_output_t layout");
_Static_assert(offsetof(plan_t, inputs) == 24, "plan_t layout");
_Static_assert(sizeof(plan_t) == 5016, "plan_t layout");

#endif
