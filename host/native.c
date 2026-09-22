#include <stdio.h>
#include <string.h>
#include "vectors.h"

unsigned char *signer_io(void);
int signer_init(void), signer_sign_ecdsa(void), signer_sign_schnorr(void);

#ifdef QEMU_BUILD
static unsigned long long now(void) { unsigned lo, hi; __asm__ volatile("csrr %0, minstret; csrr %1, minstreth" : "=r"(lo), "=r"(hi)); return ((unsigned long long)hi << 32) | lo; }
#define MEASURE(label, expr) do { unsigned long long t0 = now(); if (!(expr)) return 1; printf("instret %s %llu\n", label, now() - t0); } while (0)
#else
#define MEASURE(label, expr) do { if (!(expr)) return 1; } while (0)
#endif

static void hex(const char *label, const unsigned char *p) {
    printf("%s ", label);
    for (int i = 0; i < 64; i++) printf("%02x", p[i]);
    printf("\n");
}

int main(void) {
    unsigned char *io = signer_io();
    MEASURE("signer_init", signer_init());
    memcpy(io, TV_IN, 96);
    MEASURE("signer_sign_ecdsa", signer_sign_ecdsa());
    hex("ecdsa", io + 96);
    MEASURE("signer_sign_schnorr", signer_sign_schnorr());
    hex("schnorr", io + 96);
    return 0;
}
