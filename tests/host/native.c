#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "vectors.h"

unsigned char *prim_io(void), *prim_input(void);
int prim_init(void), prim_sign_ecdsa(void), prim_sign_schnorr(void);
int prim_seed_from_mnemonic(unsigned mn_len, unsigned pass_len), prim_bip32_derive(unsigned depth);

#ifdef QEMU_BUILD
static unsigned long long now(void) {
    unsigned lo, hi;
    __asm__ volatile("csrr %0, minstret; csrr %1, minstreth" : "=r"(lo), "=r"(hi));
    return ((unsigned long long)hi << 32) | lo;
}
#define MEASURE(label, expr)                                                                                           \
    do {                                                                                                               \
        unsigned long long t0 = now();                                                                                 \
        if (!(expr)) return 1;                                                                                         \
        printf("instret %s %llu\n", label, now() - t0);                                                                \
    } while (0)
#else
#define MEASURE(label, expr)                                                                                           \
    do {                                                                                                               \
        if (!(expr)) return 1;                                                                                         \
    } while (0)
#endif

static void hex(const char *label, const unsigned char *p, int n) {
    printf("%s ", label);
    for (int i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
}

int main(void) {
    unsigned char *io = prim_io();
    MEASURE("prim_init", prim_init());
    memcpy(io, TV_IN, 96);
    MEASURE("prim_sign_ecdsa", prim_sign_ecdsa());
    hex("ecdsa", io + 96, 64);
    MEASURE("prim_sign_schnorr", prim_sign_schnorr());
    hex("schnorr", io + 96, 64);

    unsigned char *in = prim_input();
    memcpy(in, TV_MN12 "TREZOR", sizeof(TV_MN12) + 5);
    MEASURE("prim_seed_from_mnemonic", prim_seed_from_mnemonic(sizeof(TV_MN12) - 1, 6));
    hex("seed12", io + 96, 64);
    memcpy(in, TV_MN24 "TREZOR", sizeof(TV_MN24) + 5);
    MEASURE("prim_seed_from_mnemonic", prim_seed_from_mnemonic(sizeof(TV_MN24) - 1, 6));
    hex("seed24", io + 96, 64);
    memcpy(in, TV_MN12, sizeof(TV_MN12) - 1);
    MEASURE("prim_seed_from_mnemonic", prim_seed_from_mnemonic(sizeof(TV_MN12) - 1, 0));
    memcpy(in, TV_PATH, sizeof(TV_PATH));
    MEASURE("prim_bip32_derive/4", prim_bip32_derive(4));
    MEASURE("prim_bip32_derive/5", prim_bip32_derive(5));
    hex("bip84_pub", io + 96, 33);
    return 0;
}
