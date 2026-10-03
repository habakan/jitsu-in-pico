#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "vectors.h"

unsigned char *signer_io(void), *signer_in(void);
int signer_init(void), signer_sign_ecdsa(void), signer_sign_schnorr(void);
int signer_seed_from_mnemonic(unsigned mn_len, unsigned pass_len), signer_bip32_derive(unsigned depth);

#ifdef QEMU_BUILD
static unsigned long long now(void) { unsigned lo, hi; __asm__ volatile("csrr %0, minstret; csrr %1, minstreth" : "=r"(lo), "=r"(hi)); return ((unsigned long long)hi << 32) | lo; }
#define MEASURE(label, expr) do { unsigned long long t0 = now(); if (!(expr)) return 1; printf("instret %s %llu\n", label, now() - t0); } while (0)
#else
#define MEASURE(label, expr) do { if (!(expr)) return 1; } while (0)
#endif

static void hex(const char *label, const unsigned char *p, int n) {
    printf("%s ", label);
    for (int i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
}

int main(void) {
    unsigned char *io = signer_io();
    MEASURE("signer_init", signer_init());
    memcpy(io, TV_IN, 96);
    MEASURE("signer_sign_ecdsa", signer_sign_ecdsa());
    hex("ecdsa", io + 96, 64);
    MEASURE("signer_sign_schnorr", signer_sign_schnorr());
    hex("schnorr", io + 96, 64);

    unsigned char *in = signer_in();
    memcpy(in, TV_MN12 "TREZOR", sizeof(TV_MN12) + 5);
    MEASURE("signer_seed_from_mnemonic", signer_seed_from_mnemonic(sizeof(TV_MN12) - 1, 6));
    hex("seed12", io + 96, 64);
    memcpy(in, TV_MN24 "TREZOR", sizeof(TV_MN24) + 5);
    MEASURE("signer_seed_from_mnemonic", signer_seed_from_mnemonic(sizeof(TV_MN24) - 1, 6));
    hex("seed24", io + 96, 64);
    memcpy(in, TV_MN12, sizeof(TV_MN12) - 1);
    MEASURE("signer_seed_from_mnemonic", signer_seed_from_mnemonic(sizeof(TV_MN12) - 1, 0));
    memcpy(in, TV_PATH, sizeof(TV_PATH));
    MEASURE("signer_bip32_derive/4", signer_bip32_derive(4));
    MEASURE("signer_bip32_derive/5", signer_bip32_derive(5));
    hex("bip84_pub", io + 96, 33);
    return 0;
}
