// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// softdouble.c — correctly-rounded binary64 addition/subtraction (and the
// conversions that share libgcc's object) for FPU-less ARM targets.
//
// WHY THIS EXISTS. Starboard's cross-architecture campaign
// (tools/crossarch.sh, tools/baremetal.sh) found that ARM soft-float double
// addition, libgcc's __aeabi_dadd (config/arm/ieee754-df.S, as built by GCC
// 13), misrounds an effective subtraction whose exponent gap is exactly 33
// when the result renormalizes left by one bit (e.g. 1.0 + -0x1.72e0936a3d520p-33
// gives 0x3FEFFFFFFFE8D1F6; the correctly rounded value is ...F7). In the
// >= 32-bit alignment path only one guard bit of the high word survives and
// the low word is squashed into a sticky flag, so the round bit is lost when
// the 1-bit renormalization after cancellation needs it. Every failure in a
// 200,000-case fuzz was at gap 33 and rounded DOWN (adjudicated with exact
// rational arithmetic). Observed under QEMU on ARMv5/ARMv7 Linux and on
// bare-metal Cortex-M3/M4; confirmation on silicon is open.
//
// The pattern `1.0 + small` is everywhere in Starboard's pinned kernels
// (canon_sincos's cosine kernel hits it at x = 7.854), so on such targets
// two satellites would silently disagree. The boot self-test detects this
// (SB_BIT_ARITH_ROUND, SB_BIT_TRANSCEND) and refuses to fly; linking this
// file instead makes those MCUs conformant. Verified against x86-64 SSE2
// hardware on structured corners + random cases (tests/test_softdouble.c).
//
// The sb_f64_* functions are always compiled (host-testable). The libgcc /
// AEABI symbols are exported only when the target lacks hardware doubles,
// replacing libgcc's member _arm_addsubdf3.o as a whole (all 15 symbols it
// defines, so the archive member is never pulled in).

#include <stdint.h>
#include <string.h>

uint64_t sb_f64_add(uint64_t a, uint64_t b);
uint64_t sb_f64_sub(uint64_t a, uint64_t b);
uint64_t sb_f64_from_i64(int64_t v);
uint64_t sb_f64_from_u64(uint64_t v);
uint64_t sb_f64_from_f32(uint32_t f);

#define SIGN64     (0x8000000000000000ULL)
#define EXP_MASK   (0x7FF0000000000000ULL)
#define FRAC_MASK  (0x000FFFFFFFFFFFFFULL)
#define QUIET_BIT  (0x0008000000000000ULL)
#define DEFAULT_NAN (0x7FF8000000000000ULL)

/* Shift right with sticky ("jamming"): any bit shifted out ORs into bit 0. */
static uint64_t shift_right_jam(uint64_t v, uint32_t d)
{
    if (d == 0U)
    {
        return v;
    }
    if (d >= 64U)
    {
        return (v != 0U) ? 1U : 0U;
    }
    return (v >> d) | (((v & ((1ULL << d) - 1U)) != 0U) ? 1U : 0U);
}

/* Round-to-nearest-even and pack. `sig` carries 3 extra low bits (guard,
 * round, sticky) below the binary64 LSB; the implicit bit sits at bit 55
 * for a normal number. `e` is the biased exponent (>= 1). */
static uint64_t round_pack(uint64_t sign, int32_t e, uint64_t sig)
{
    const uint64_t rem = sig & 7U;
    uint64_t mant = sig >> 3;
    if ((rem > 4U) || ((rem == 4U) && ((mant & 1U) != 0U)))
    {
        ++mant;
    }
    if (mant >= (1ULL << 53))
    {
        mant >>= 1;
        ++e;
    }
    if (e >= 0x7FF)
    {
        return sign | EXP_MASK;                       /* overflow -> inf */
    }
    if (mant < (1ULL << 52))
    {
        return sign | mant;                           /* subnormal (e == 1) */
    }
    return sign | ((uint64_t)e << 52) | (mant & FRAC_MASK);
}

uint64_t sb_f64_add(uint64_t a, uint64_t b)
{
    uint64_t sa = a & SIGN64;
    uint64_t sb = b & SIGN64;
    int32_t ea = (int32_t)((a & EXP_MASK) >> 52);
    int32_t eb = (int32_t)((b & EXP_MASK) >> 52);
    uint64_t ma = a & FRAC_MASK;
    uint64_t mb = b & FRAC_MASK;
    uint64_t sig;
    int32_t e;

    /* NaN, infinity. */
    if ((ea == 0x7FF) && (ma != 0U))
    {
        return a | QUIET_BIT;
    }
    if ((eb == 0x7FF) && (mb != 0U))
    {
        return b | QUIET_BIT;
    }
    if (ea == 0x7FF)
    {
        return ((eb == 0x7FF) && (sa != sb)) ? DEFAULT_NAN : a;
    }
    if (eb == 0x7FF)
    {
        return b;
    }
    /* Zeros: x + 0 = x; (+0) + (-0) = +0 under round-to-nearest. */
    if ((ea == 0) && (ma == 0U))
    {
        if ((eb == 0) && (mb == 0U))
        {
            return (sa == sb) ? a : 0U;
        }
        return b;
    }
    if ((eb == 0) && (mb == 0U))
    {
        return a;
    }
    /* Unpack with the implicit bit, 3 guard bits. Subnormals use e = 1. */
    if (ea == 0) { ea = 1; } else { ma |= (1ULL << 52); }
    if (eb == 0) { eb = 1; } else { mb |= (1ULL << 52); }
    ma <<= 3;
    mb <<= 3;
    /* Order by magnitude: |a| >= |b|. */
    if ((eb > ea) || ((eb == ea) && (mb > ma)))
    {
        const uint64_t ts = sa; const int32_t te = ea; const uint64_t tm = ma;
        sa = sb; ea = eb; ma = mb;
        sb = ts; eb = te; mb = tm;
    }
    mb = shift_right_jam(mb, (uint32_t)(ea - eb));
    e = ea;
    if (sa == sb)
    {
        sig = ma + mb;
        if (sig >= (1ULL << 56))
        {
            sig = shift_right_jam(sig, 1U);
            ++e;
        }
    }
    else
    {
        sig = ma - mb;
        if (sig == 0U)
        {
            return 0U;                                /* exact zero: +0 */
        }
        /* Renormalize. With a gap >= 2 at most one shift happens and the
         * jammed sticky bit stays below the guard bit; with a gap <= 1 the
         * operands were aligned exactly, so every shift is exact. */
        while ((sig < (1ULL << 55)) && (e > 1))
        {
            sig <<= 1;
            --e;
        }
    }
    return round_pack(sa, e, sig);
}

uint64_t sb_f64_sub(uint64_t a, uint64_t b)
{
    return sb_f64_add(a, b ^ SIGN64);
}

/* Normalize an unsigned magnitude (< 2^64) into round_pack's format. */
static uint64_t from_magnitude(uint64_t sign, uint64_t m)
{
    int32_t e = 1023 + 55;          /* value = m * 2^0 with implicit at 55 */
    uint64_t sig = m;
    if (m == 0U)
    {
        return 0U;
    }
    while (sig >= (1ULL << 56))
    {
        sig = shift_right_jam(sig, 1U);
        ++e;
    }
    while (sig < (1ULL << 55))
    {
        sig <<= 1;
        --e;
    }
    return round_pack(sign, e, sig);
}

uint64_t sb_f64_from_i64(int64_t v)
{
    if (v < 0)
    {
        return from_magnitude(SIGN64, (uint64_t)(-(v + 1)) + 1U);
    }
    return from_magnitude(0U, (uint64_t)v);
}

uint64_t sb_f64_from_u64(uint64_t v)
{
    return from_magnitude(0U, v);
}

uint64_t sb_f64_from_f32(uint32_t f)
{
    const uint64_t sign = (uint64_t)(f >> 31) << 63;
    const int32_t e = (int32_t)((f >> 23) & 0xFFU);
    uint32_t m = f & 0x7FFFFFU;
    int32_t ee;
    if (e == 0xFF)
    {
        if (m != 0U)
        {
            return sign | EXP_MASK | QUIET_BIT | ((uint64_t)m << 29);
        }
        return sign | EXP_MASK;
    }
    if (e == 0)
    {
        if (m == 0U)
        {
            return sign;
        }
        ee = 1 - 127 + 1023;                          /* subnormal float */
        while ((m & 0x800000U) == 0U)
        {
            m <<= 1;
            --ee;
        }
        m &= 0x7FFFFFU;
        return sign | ((uint64_t)ee << 52) | ((uint64_t)m << 29);
    }
    return sign | ((uint64_t)(e - 127 + 1023) << 52) | ((uint64_t)m << 29);
}

/* ------------------------------------------------------------------------ */
/* AEABI / libgcc replacements — soft-double ARM targets only.              */
/* ------------------------------------------------------------------------ */
#if defined(__arm__) && !defined(SB_NO_SOFTDOUBLE) && \
    (!defined(__ARM_FP) || ((__ARM_FP & 0x8) == 0))

#if defined(__ARM_PCS_VFP)
#define SB_AEABI __attribute__((pcs("aapcs")))  /* helpers use the base PCS */
#else
#define SB_AEABI
#endif

static uint64_t bits_d(double d)
{
    uint64_t u;
    (void)memcpy(&u, &d, sizeof(u));
    return u;
}

static double d_bits(uint64_t u)
{
    double d;
    (void)memcpy(&d, &u, sizeof(d));
    return d;
}

SB_AEABI double __aeabi_dadd(double a, double b);
SB_AEABI double __aeabi_dsub(double a, double b);
SB_AEABI double __aeabi_drsub(double a, double b);
SB_AEABI double __aeabi_i2d(int32_t v);
SB_AEABI double __aeabi_ui2d(uint32_t v);
SB_AEABI double __aeabi_l2d(int64_t v);
SB_AEABI double __aeabi_ul2d(uint64_t v);
SB_AEABI double __aeabi_f2d(float f);
double __adddf3(double a, double b);
double __subdf3(double a, double b);
double __floatsidf(int32_t v);
double __floatunsidf(uint32_t v);
double __floatdidf(int64_t v);
double __floatundidf(uint64_t v);
double __extendsfdf2(float f);

SB_AEABI double __aeabi_dadd(double a, double b)
{
    return d_bits(sb_f64_add(bits_d(a), bits_d(b)));
}
SB_AEABI double __aeabi_dsub(double a, double b)
{
    return d_bits(sb_f64_sub(bits_d(a), bits_d(b)));
}
SB_AEABI double __aeabi_drsub(double a, double b)
{
    return d_bits(sb_f64_sub(bits_d(b), bits_d(a)));
}
SB_AEABI double __aeabi_i2d(int32_t v) { return d_bits(sb_f64_from_i64(v)); }
SB_AEABI double __aeabi_ui2d(uint32_t v) { return d_bits(sb_f64_from_u64(v)); }
SB_AEABI double __aeabi_l2d(int64_t v) { return d_bits(sb_f64_from_i64(v)); }
SB_AEABI double __aeabi_ul2d(uint64_t v) { return d_bits(sb_f64_from_u64(v)); }
SB_AEABI double __aeabi_f2d(float f)
{
    uint32_t u;
    (void)memcpy(&u, &f, sizeof(u));
    return d_bits(sb_f64_from_f32(u));
}
double __adddf3(double a, double b) { return __aeabi_dadd(a, b); }
double __subdf3(double a, double b) { return __aeabi_dsub(a, b); }
double __floatsidf(int32_t v) { return __aeabi_i2d(v); }
double __floatunsidf(uint32_t v) { return __aeabi_ui2d(v); }
double __floatdidf(int64_t v) { return __aeabi_l2d(v); }
double __floatundidf(uint64_t v) { return __aeabi_ul2d(v); }
double __extendsfdf2(float f) { return __aeabi_f2d(f); }

#endif
