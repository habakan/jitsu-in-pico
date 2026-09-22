#include <stdint.h>
#include <string.h>
#include "secp256k1.h"
#include "secp256k1_preallocated.h"
#include "secp256k1_extrakeys.h"
#include "secp256k1_schnorrsig.h"

#ifdef __wasm__
#define EXPORT(name) __attribute__((export_name(#name))) name
#else
#define EXPORT(name) name
#endif

/* USE_EXTERNAL_DEFAULT_CALLBACKS: stdio/abort を引き込まないため */
void secp256k1_default_illegal_callback_fn(const char *msg, void *data) { (void)msg; (void)data; __builtin_trap(); }
void secp256k1_default_error_callback_fn(const char *msg, void *data) { (void)msg; (void)data; __builtin_trap(); }

static unsigned char ctx_mem[256] __attribute__((aligned(16)));
static secp256k1_context *ctx;
/* ホストとの受け渡し領域。seckey[32] msg[32] aux[32] out[64] */
static unsigned char io[160];

unsigned char *EXPORT(signer_io)(void) { return io; }

int EXPORT(signer_init)(void) {
    if (secp256k1_context_preallocated_size(SECP256K1_CONTEXT_NONE) > sizeof(ctx_mem)) return 0;
    ctx = secp256k1_context_preallocated_create(ctx_mem, SECP256K1_CONTEXT_NONE);
    return ctx != NULL;
}

int EXPORT(signer_sign_ecdsa)(void) {
    secp256k1_ecdsa_signature sig;
    int ok = secp256k1_ecdsa_sign(ctx, &sig, io + 32, io, NULL, NULL)
          && secp256k1_ecdsa_signature_serialize_compact(ctx, io + 96, &sig);
    memset(&sig, 0, sizeof(sig));
    return ok;
}

int EXPORT(signer_sign_schnorr)(void) {
    secp256k1_keypair kp;
    int ok = secp256k1_keypair_create(ctx, &kp, io)
          && secp256k1_schnorrsig_sign32(ctx, io + 96, io + 32, &kp, io + 64);
    memset(&kp, 0, sizeof(kp));
    return ok;
}

void EXPORT(signer_zeroize)(void) {
    volatile unsigned char *p = io;
    for (size_t i = 0; i < sizeof(io); i++) p[i] = 0;
}
