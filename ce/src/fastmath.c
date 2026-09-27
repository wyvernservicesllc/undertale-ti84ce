/* floorf, ceilf, truncf and rintf on the float's bytes. They replace the C
 * library's (linked first, so libc's are never pulled in), which take
 * ~20000 cycles each on the calculator; 32-bit shifts are slow too, so
 * this works a byte at a time. Results are exact, like libm's. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

typedef union
{
    float f;
    uint8_t b[4];
} fbits_t;

/* the float's exponent, unbiased */
static int fexp(const fbits_t *v)
{
    return (int)(((v->b[3] & 0x7f) << 1) | (v->b[2] >> 7)) - 127;
}

static bool bit(const fbits_t *v, int n)
{
    return (v->b[n >> 3] >> (n & 7)) & 1;
}

/* clears bits 0 to n - 1: true if any was set */
static bool clear_low(fbits_t *v, int n)
{
    bool any = false;
    for (int i = 0; n > 0; i++, n -= 8)
    {
        uint8_t m = n >= 8 ? 0xff : (uint8_t)((1 << n) - 1);
        if (v->b[i] & m)
        {
            any = true;
            v->b[i] &= (uint8_t)~m;
        }
    }
    return any;
}

/* adds 1 at bit n: one more unit in the last place of a whole number (a
   carry out of the mantissa bumps the exponent, as it should) */
static void add_unit(fbits_t *v, int n)
{
    int i = n >> 3;
    unsigned c = 1u << (n & 7);
    for (; i < 4 && c; i++)
    {
        c += v->b[i];
        v->b[i] = (uint8_t)c;
        c >>= 8;
    }
}

static float one(bool neg)
{
    return neg ? -1.0f : 1.0f;
}

/* x toward zero; *frac: it had a fraction */
static float trunc_frac(float x, bool *frac)
{
    fbits_t v;
    int e;
    v.f = x;
    e = fexp(&v);
    if (e >= 23)
    {
        *frac = false;
        return x; /* whole, inf or nan */
    }
    if (e < 0)
    {
        *frac = (v.b[3] & 0x7f) || v.b[2] || v.b[1] || v.b[0];
        v.b[0] = v.b[1] = v.b[2] = 0;
        v.b[3] &= 0x80;
        return v.f;
    }
    *frac = clear_low(&v, 23 - e);
    return v.f;
}

float truncf(float x)
{
    bool frac;
    return trunc_frac(x, &frac);
}

/* x toward zero, then one further out when it had a fraction and its
   sign is neg_side's (floor: negative, ceil: positive) */
static float step_out(float x, bool neg_side)
{
    bool frac;
    fbits_t v, t;
    int e;
    v.f = x;
    t.f = trunc_frac(x, &frac);
    if (!frac || ((v.b[3] & 0x80) != 0) != neg_side)
    {
        return t.f;
    }
    e = fexp(&v);
    if (e < 0)
    {
        return one(neg_side);
    }
    add_unit(&t, 23 - e);
    return t.f;
}

float floorf(float x)
{
    return step_out(x, true);
}

float ceilf(float x)
{
    return step_out(x, false);
}

/* to nearest, halves to even */
float rintf(float x)
{
    fbits_t v;
    int e, n;
    bool half, rest, odd;
    v.f = x;
    e = fexp(&v);
    if (e >= 23)
    {
        return x;
    }
    if (e < -1)
    {
        v.b[0] = v.b[1] = v.b[2] = 0;
        v.b[3] &= 0x80;
        return v.f; /* below 0.5: signed zero */
    }
    if (e == -1)
    {
        /* [0.5, 1): 1, except exactly 0.5 (to even: 0) */
        bool exact = !(v.b[2] & 0x7f) && !v.b[1] && !v.b[0];
        if (exact)
        {
            v.b[2] = 0;
            v.b[3] &= 0x80;
            return v.f;
        }
        return one(v.b[3] & 0x80);
    }
    n = 23 - e; /* fraction bits, 1 to 23 */
    half = bit(&v, n - 1);
    {
        fbits_t w = v;
        rest = clear_low(&w, n - 1);
    }
    odd = n == 23 ? true : bit(&v, n); /* at n = 23: the implicit 1 */
    clear_low(&v, n);
    if (half && (rest || odd))
    {
        add_unit(&v, n);
    }
    return v.f;
}
