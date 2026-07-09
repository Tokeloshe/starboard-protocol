// SPDX-License-Identifier: AGPL-3.0-or-later
// Starboard Protocol — reference implementation (C99 port)
// Copyright (C) 2026 James H. All rights reserved.
// Dual licensing: see COMMERCIAL-LICENSE.md.
//
// canon.c — quantization lattice and the two pinned transcendentals.
// Operation-for-operation transliteration of canon.hpp: same literals, same
// Horner order, same range reduction. Only nearbyint/ldexp (both IEEE-exact)
// come from libm; there is no libm exp/sin/cos anywhere here.

#include "starboard.h"
#include <math.h>

double sb_quantize(double x, double q)
{
    return nearbyint(x / q) * q;
}

double sb_canon_exp(double x)
{
    double kd;
    double r;
    double p;
    const double INV_LN2 = 1.4426950408889634;
    const double LN2_HI  = 6.93147180369123816490e-01;
    const double LN2_LO  = 1.90821492927058770002e-10;

    if (x < -708.0)
    {
        return 0.0;
    }
    if (x == 0.0)
    {
        return 1.0;
    }
    kd = nearbyint(x * INV_LN2);
    r  = (x - (kd * LN2_HI)) - (kd * LN2_LO);
    p = 1.6059043836821613e-10;                  /* 1/13! */
    p = (p * r) + 2.08767569878681e-09;          /* 1/12! */
    p = (p * r) + 2.505210838544172e-08;         /* 1/11! */
    p = (p * r) + 2.7557319223985893e-07;        /* 1/10! */
    p = (p * r) + 2.755731922398589e-06;         /* 1/9!  */
    p = (p * r) + 2.48015873015873e-05;          /* 1/8!  */
    p = (p * r) + 1.984126984126984e-04;         /* 1/7!  */
    p = (p * r) + 1.388888888888889e-03;         /* 1/6!  */
    p = (p * r) + 8.333333333333333e-03;         /* 1/5!  */
    p = (p * r) + 4.166666666666666e-02;         /* 1/4!  */
    p = (p * r) + 1.6666666666666666e-01;        /* 1/3!  */
    p = (p * r) + 0.5;                           /* 1/2!  */
    p = (p * r) + 1.0;                           /* 1/1!  */
    p = (p * r) + 1.0;                           /* 1/0!  */
    return ldexp(p, (int)kd);
}

void sb_canon_sincos(double x, double *sin_out, double *cos_out)
{
    const double INV_PIO2 = 0.6366197723675814;
    const double PIO2_HI  = 1.5707963109016418;
    const double PIO2_MED = 1.5893254712295857e-08;
    const double PIO2_LO  = 6.123233995736766e-17;
    double kd;
    double r;
    int64_t k;
    int q;
    double r2;
    double sp;
    double s;
    double cp;
    double c;

    kd = nearbyint(x * INV_PIO2);
    r  = ((x - (kd * PIO2_HI)) - (kd * PIO2_MED)) - (kd * PIO2_LO);
    k  = (int64_t)kd;
    q  = (int)((((k % 4) + 4) % 4));
    r2 = r * r;

    sp = 2.8114572543455206e-15;                 /*  1/17! */
    sp = (sp * r2) - 7.647163731819816e-13;      /* -1/15! */
    sp = (sp * r2) + 1.6059043836821613e-10;     /*  1/13! */
    sp = (sp * r2) - 2.505210838544172e-08;      /* -1/11! */
    sp = (sp * r2) + 2.7557319223985893e-06;     /*  1/9!  */
    sp = (sp * r2) - 1.984126984126984e-04;      /* -1/7!  */
    sp = (sp * r2) + 8.333333333333333e-03;      /*  1/5!  */
    sp = (sp * r2) - 1.6666666666666666e-01;     /* -1/3!  */
    s = r + (r * (r2 * sp));

    cp = -1.5619206968586225e-16;                /* -1/18! */
    cp = (cp * r2) + 4.779477332387385e-14;      /*  1/16! */
    cp = (cp * r2) - 1.1470745597729725e-11;     /* -1/14! */
    cp = (cp * r2) + 2.08767569878681e-09;       /*  1/12! */
    cp = (cp * r2) - 2.755731922398589e-07;      /* -1/10! */
    cp = (cp * r2) + 2.48015873015873e-05;       /*  1/8!  */
    cp = (cp * r2) - 1.388888888888889e-03;      /* -1/6!  */
    cp = (cp * r2) + 4.1666666666666664e-02;     /*  1/4!  */
    cp = (cp * r2) - 0.5;                        /* -1/2!  */
    c = 1.0 + (r2 * cp);

    switch (q)
    {
        case 0:
            *sin_out = s;
            *cos_out = c;
            break;
        case 1:
            *sin_out = c;
            *cos_out = -s;
            break;
        case 2:
            *sin_out = -s;
            *cos_out = -c;
            break;
        default:
            *sin_out = -c;
            *cos_out = s;
            break;
    }
}

double sb_canon_sin(double x)
{
    double s;
    double c;
    sb_canon_sincos(x, &s, &c);
    return s;
}

double sb_canon_cos(double x)
{
    double s;
    double c;
    sb_canon_sincos(x, &s, &c);
    return c;
}
