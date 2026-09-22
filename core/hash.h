#ifndef CORE_HASH_H
#define CORE_HASH_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t s[8];
    uint64_t len;
    uint8_t buf[64];
} sha256_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const uint8_t *p, size_t n);
void sha256_final(sha256_ctx *c, uint8_t out[32]);
void sha256(const uint8_t *p, size_t n, uint8_t out[32]);
void sha256d_final(sha256_ctx *c, uint8_t out[32]);
void ripemd160(const uint8_t *p, size_t n, uint8_t out[20]);
void hash160(const uint8_t *p, size_t n, uint8_t out[20]);

#endif
