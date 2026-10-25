// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — verify softdouble.c against IEEE-754 hardware.
// Copyright (C) 2026 James H. All rights reserved.
//
// Run on a host whose FPU is correctly rounded (x86-64 SSE2, AArch64): every
// sb_f64_* result must equal the hardware result bit for bit (NaN results
// compared as "is NaN": payload propagation is not specified by IEEE 754).
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

uint64_t sb_f64_add(uint64_t a, uint64_t b);
uint64_t sb_f64_sub(uint64_t a, uint64_t b);
uint64_t sb_f64_from_i64(int64_t v);
uint64_t sb_f64_from_u64(uint64_t v);
uint64_t sb_f64_from_f32(uint32_t f);

static uint64_t s = 0x5D0B1EULL;
static uint64_t rnd(void)
{
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    return s ^ (s >> 29);
}
static double d(uint64_t u) { double x; memcpy(&x, &u, 8); return x; }
static uint64_t u(double x) { uint64_t v; memcpy(&v, &x, 8); return v; }
static int isnan_u(uint64_t v)
{
    return ((v & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL) &&
           ((v & 0x000FFFFFFFFFFFFFULL) != 0U);
}

static unsigned long long n_checked = 0, n_bad = 0;
static void check(uint64_t a, uint64_t b)
{
    volatile double x = d(a), y = d(b);
    const uint64_t hs = u(x + y), hd = u(x - y);
    const uint64_t ss = sb_f64_add(a, b), sd = sb_f64_sub(a, b);
    n_checked += 2;
    if (!((hs == ss) || (isnan_u(hs) && isnan_u(ss)))) {
        if (n_bad < 10) {
            printf("  add %016llX + %016llX: hw %016llX sb %016llX\n",
                   (unsigned long long)a, (unsigned long long)b,
                   (unsigned long long)hs, (unsigned long long)ss);
        }
        ++n_bad;
    }
    if (!((hd == sd) || (isnan_u(hd) && isnan_u(sd)))) {
        if (n_bad < 10) {
            printf("  sub %016llX - %016llX: hw %016llX sb %016llX\n",
                   (unsigned long long)a, (unsigned long long)b,
                   (unsigned long long)hd, (unsigned long long)sd);
        }
        ++n_bad;
    }
}

int main(int argc, char **argv)
{
    unsigned long long n_rand = (argc > 1) ? strtoull(argv[1], 0, 10) : 20000000ULL;
    static const uint64_t special[] = {
        0x0000000000000000ULL, 0x8000000000000000ULL, 0x0000000000000001ULL,
        0x8000000000000001ULL, 0x000FFFFFFFFFFFFFULL, 0x0010000000000000ULL,
        0x7FEFFFFFFFFFFFFFULL, 0x7FF0000000000000ULL, 0xFFF0000000000000ULL,
        0x7FF8000000000000ULL, 0x7FF0000000000001ULL, 0x3FF0000000000000ULL,
        0xBFF0000000000000ULL, 0x3FEFFFFFFFFFFFFFULL, 0x3FF0000000000001ULL,
        0x4340000000000000ULL, 0x3CA0000000000000ULL, 0x3C90000000000000ULL};
    const size_t ns = sizeof(special) / sizeof(special[0]);
    size_t i, j;
    int gap;
    unsigned long long k;

    for (i = 0; i < ns; ++i) {
        for (j = 0; j < ns; ++j) { check(special[i], special[j]); }
    }
    /* The failure pattern: power-of-two vs a value `gap` binades below, both
     * signs, every gap 0..66, many mantissas — plus near-ties. */
    for (gap = 0; gap <= 66; ++gap) {
        for (k = 0; k < 20000; ++k) {
            const uint64_t ea = 600 + (rnd() % 800);
            const uint64_t a = (ea << 52) | ((k & 1) ? 0 : (rnd() >> 12));
            const uint64_t eb = (ea > (uint64_t)gap) ? ea - (uint64_t)gap : 1;
            uint64_t b = (eb << 52) | (rnd() >> 12);
            if (k % 7 == 0) { b = (b & ~0x3FFULL) | 0x200ULL; }  /* tie-ish */
            b |= (rnd() & 1) ? 0x8000000000000000ULL : 0;
            check(a, b);
            check(a | 0x8000000000000000ULL, b);
        }
    }
    /* Subnormal arithmetic. */
    for (k = 0; k < 1000000; ++k) {
        check(rnd() >> 12, (rnd() >> 12) | ((rnd() & 1) ? 0x8000000000000000ULL : 0));
        check(0x0010000000000000ULL | (rnd() >> 12), (rnd() >> 12) | 0x8000000000000000ULL);
    }
    /* Uniform random bit patterns and random magnitudes near each other. */
    for (k = 0; k < n_rand; ++k) {
        const uint64_t a = rnd();
        uint64_t b = rnd();
        if (k & 1) {
            b = (a & 0xFFF0000000000000ULL) + ((rnd() % 128) << 52) - (64ULL << 52);
            b |= rnd() >> 12;
        }
        check(a, b);
    }
    {
        unsigned long long bad_conv = 0;
        for (k = 0; k < 5000000; ++k) {
            const int64_t v = (int64_t)rnd() >> (rnd() % 64);
            const uint64_t w = rnd() >> (rnd() % 64);
            volatile float f;
            uint32_t fb = (uint32_t)rnd();
            if (u((double)v) != sb_f64_from_i64(v)) { ++bad_conv; }
            if (u((double)w) != sb_f64_from_u64(w)) { ++bad_conv; }
            memcpy((void *)&f, &fb, 4);
            if (!((u((double)f) == sb_f64_from_f32(fb)) ||
                  (isnan_u(u((double)f)) && isnan_u(sb_f64_from_f32(fb))))) {
                ++bad_conv;
            }
        }
        printf("conversions: 15000000 checked, %llu mismatches\n", bad_conv);
        n_bad += bad_conv;
    }
    printf("add/sub: %llu checked, %llu mismatches vs IEEE-754 hardware\n",
           n_checked, n_bad);
    printf("%s\n", (n_bad == 0) ? "ALL PASS" : "FAILED");
    return (n_bad == 0) ? 0 : 1;
}
