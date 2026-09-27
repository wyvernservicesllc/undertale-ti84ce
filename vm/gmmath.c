/* Fast trigonometry, computed the same way on the calculator and the Mac.

   The C library's sinf and atan2f take over 100,000 CPU cycles each on the
   calculator's software floats; bullets moving under gravity need several
   per frame. These go through angles as integers (2^24 per turn) and
   interpolated tables (tools/gen_gmmath.py), accurate to about 1e-6, and
   exact at multiples of 90 degrees. The integer work stays within 24 bits,
   the calculator's native size (32-bit shifts and multiplies are slow
   library calls there); on the Mac the same values fit in 32 bits. */
#include <math.h>

#include "vm.h"

#ifndef __TICE__
typedef uint32_t uint24_t;
#endif

#include "gmmath_tab.h"

#define TURN 16777216.0f /* 2^24 */
#define MASK24 0xffffffu

typedef union
{
    gmreal_t f;
    uint32_t u;
    uint8_t b[4]; /* little-endian on both machines */
} fbits;

/* |f| >= 32768 (from the exponent, without a float compare) */
static bool is_big(gmreal_t f)
{
    fbits b;
    b.f = f;
    return (b.b[3] & 0x7f) >= 0x47;
}

/* v / 2^shift, exactly (v < 2^24) */
static gmreal_t fix_float(uint24_t v, int shift)
{
    fbits b;
    b.f = (gmreal_t)v;
    if (v)
    {
        b.u -= (uint32_t)shift << 23;
    }
    return b.f;
}

/* sin of t / 2^24 turns, 1.23 fixed point, as a float */
static gmreal_t turn_sin(uint24_t t)
{
    uint24_t f = t & 0x3fffff, i, r, v;
    uint8_t q = (uint8_t)((t >> 22) & 3);
    gmreal_t out;
    if (q & 1)
    {
        f = 0x400000 - f; /* sin(90 + a) = sin(90 - a) */
    }
    i = f >> 12;
    r = f & 0xfff;
    if (i >= 1024)
    {
        v = sin_tab[1024];
    }
    else
    {
        uint24_t d = sin_tab[i + 1] - sin_tab[i]; /* under 2^14 */
        v = sin_tab[i] + ((d * (r >> 6)) >> 6) + ((d * (r & 63)) >> 12);
    }
    out = fix_float(v, 23);
    return q & 2 ? -out : out;
}

/* A function call, so the compiler can't compute it on both paths of a
   branch: fmodf is a very slow ROM routine on the calculator. */
static NOINLINE gmreal_t wrap(gmreal_t x, gmreal_t period)
{
    return fmodf(x, period);
}

/* degrees to turns (mod 1) */
static uint24_t deg_turn(gmreal_t deg)
{
    if (is_big(deg))
    {
        deg = wrap(deg, 360);
    }
    return (uint24_t)(int32_t)(deg * (TURN / 360)) & MASK24;
}

static uint24_t rad_turn(gmreal_t rad)
{
    if (is_big(rad * (1.0f / 64)))
    {
        rad = wrap(rad, 6.2831853f);
    }
    return (uint24_t)(int32_t)(rad * (TURN / 6.2831853f)) & MASK24;
}

gmreal_t gm_dsin(gmreal_t deg)
{
    return turn_sin(deg_turn(deg));
}

gmreal_t gm_dcos(gmreal_t deg)
{
    return turn_sin((deg_turn(deg) + 0x400000) & MASK24);
}

gmreal_t gm_sin(gmreal_t rad)
{
    return turn_sin(rad_turn(rad));
}

gmreal_t gm_cos(gmreal_t rad)
{
    return turn_sin((rad_turn(rad) + 0x400000) & MASK24);
}

/* atan2(y, x) in degrees, -180 to 180 */
gmreal_t gm_datan2(gmreal_t y, gmreal_t x)
{
    gmreal_t ax = fabsf(x), ay = fabsf(y), ratio, out;
    bool swap = ay > ax;
    uint24_t r, i, rem, a;
    if (ax == 0 && ay == 0)
    {
        return signbit(x) ? (signbit(y) ? -180 : 180) : (signbit(y) ? -0.0f : 0.0f);
    }
    ratio = swap ? ax / ay : ay / ax; /* 0 to 1 */
    r = (uint24_t)(ratio * 8388608.0f); /* 1.23 */
    i = r >> 13;
    rem = r & 0x1fff;
    if (i >= 1024)
    {
        a = atan_tab[1024];
    }
    else
    {
        uint24_t d = atan_tab[i + 1] - atan_tab[i]; /* under 2^12 */
        a = atan_tab[i] + ((d * (rem >> 7)) >> 6) + ((d * (rem & 127)) >> 13);
    }
    /* degrees in 8.16 */
    if (swap)
    {
        a = 90 * 65536 - a;
    }
    if (signbit(x))
    {
        a = 180 * 65536 - a;
    }
    out = fix_float(a, 16);
    return signbit(y) ? -out : out;
}

gmreal_t gm_atan2(gmreal_t y, gmreal_t x)
{
    return gm_datan2(y, x) * (3.14159265f / 180);
}

#ifdef CE_BENCH
gmreal_t gm_bench_turn_sin(unsigned t) { return turn_sin(t); }
unsigned gm_bench_deg_turn(gmreal_t d) { return deg_turn(d); }
gmreal_t gm_bench_fix(unsigned v) { return fix_float(v, 23); }
#endif
