/* GML values: 32-bit reals, strings (string table or reference-counted
   heap strings) and copy-on-write arrays. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"

void v_retain(value_t v)
{
    if (v.t == VT_HSTR)
    {
        v.u.hs->refs++;
    }
    else if (v.t == VT_ARRAY)
    {
        v.u.a->refs++;
    }
}

static void arr_free(gmarr_t *a)
{
    for (int i = 0; i < a->n; i++)
    {
        v_release(a->e[i].v);
    }
    free(a->e);
    free(a);
}

void v_release(value_t v)
{
    if (v.t == VT_HSTR)
    {
        if (--v.u.hs->refs == 0)
        {
            free(v.u.hs);
        }
    }
    else if (v.t == VT_ARRAY)
    {
        if (--v.u.a->refs == 0)
        {
            arr_free(v.u.a);
        }
    }
}

value_t v_str(const char *s, int len)
{
    value_t v;
    gmstr_t *h;

    if (len < 0)
    {
        len = (int)strlen(s);
    }
    if (len > 65000)
    {
        len = 65000;
    }
    h = malloc(sizeof(gmstr_t) + len + 1);
    if (!h)
    {
        vm_error("out of memory (string)");
        return v_cstr("");
    }
    h->refs = 1;
    h->len = (uint16_t)len;
    memcpy(h->s, s, len);
    h->s[len] = 0;
    v.t = VT_HSTR;
    v.u.hs = h;
    return v;
}

const char *v_cstring(value_t v)
{
    if (v.t == VT_CSTR)
    {
        return v.u.cs;
    }
    if (v.t == VT_HSTR)
    {
        return v.u.hs->s;
    }
    return "";
}

gmreal_t v_num(value_t v)
{
    if (v.t == VT_REAL)
    {
        return v.u.r;
    }
    return 0;
}

/* Floats as integers: comparing the bits is much cheaper than float math
   on the eZ80. key() orders like the floats themselves. */
static inline int32_t fbits(gmreal_t f)
{
    union
    {
        gmreal_t f;
        int32_t i;
    } u;
    u.f = f;
    return u.i;
}

static inline int32_t fkey(gmreal_t f)
{
    int32_t i = fbits(f);
    return i < 0 ? (int32_t)(0x80000000u - (uint32_t)i) : i;
}

bool v_truthy(value_t v)
{
    if (v.t == VT_REAL)
    {
        return fbits(v.u.r) > 0x3F000000; /* > 0.5 */
    }
    return false;
}

/* GameMaker compares reals with a small epsilon; only values within a few
   thousand ulps need the (slow) float subtraction */
static bool near(gmreal_t x, gmreal_t y)
{
    int32_t d = fkey(x) - fkey(y);
    if (d == 0)
    {
        return true;
    }
    if (d > 4096 || d < -4096)
    {
        /* far apart, unless both are tiny (around zero) */
        if ((fbits(x) & 0x7fffffff) > 0x38D1B717 || (fbits(y) & 0x7fffffff) > 0x38D1B717) /* 1e-4 */
        {
            return false;
        }
    }
    return fabsf(x - y) < 1e-5f;
}

int32_t v_int(value_t v)
{
    gmreal_t r = v_num(v);
    if (r >= 2147483647.0f)
    {
        return 2147483647;
    }
    if (r <= -2147483647.0f)
    {
        return -2147483647;
    }
    return (int32_t)r;
}

int32_t v_round(value_t v)
{
    gmreal_t r = v_num(v);
    return (int32_t)floorf(r + 0.5f);
}

/* GML's string(): integers without decimals, others with up to 2. */
value_t v_tostring(value_t v)
{
    char buf[32];

    if (v_is_str(v))
    {
        v_retain(v);
        return v;
    }
    if (v.t == VT_UNDEF)
    {
        return v_cstr("undefined");
    }
    if (v.t == VT_ARRAY)
    {
        return v_cstr("array");
    }
    {
        /* no printf on the calculator: format by hand, integers without
           decimals and others with up to two, like GameMaker */
        gmreal_t r = v.u.r;
        char *p = buf;
        int32_t ip;
        int32_t frac;
        if (r < 0)
        {
            *p++ = '-';
            r = -r;
        }
        if (r >= 2e9f)
        {
            r = 2e9f;
        }
        frac = (int32_t)floorf((r - floorf(r)) * 100 + 0.5f);
        ip = (int32_t)floorf(r);
        if (frac >= 100)
        {
            ip++;
            frac -= 100;
        }
        {
            char tmp[12];
            int n = 0;
            do
            {
                tmp[n++] = (char)('0' + ip % 10);
                ip /= 10;
            } while (ip);
            while (n)
            {
                *p++ = tmp[--n];
            }
        }
        if (frac)
        {
            *p++ = '.';
            *p++ = (char)('0' + frac / 10);
            if (frac % 10)
            {
                *p++ = (char)('0' + frac % 10);
            }
        }
        *p = 0;
        if (buf[0] == '-' && buf[1] == '0' && !buf[2])
        {
            buf[0] = '0';
            buf[1] = 0;
        }
    }
    return v_str(buf, -1);
}

bool v_equal(value_t a, value_t b)
{
    if (v_is_str(a) && v_is_str(b))
    {
        return !strcmp(v_cstring(a), v_cstring(b));
    }
    if (v_is_str(a) || v_is_str(b))
    {
        return false;
    }
    if (a.t == VT_UNDEF || b.t == VT_UNDEF)
    {
        return a.t == b.t;
    }
    if (a.t == VT_ARRAY || b.t == VT_ARRAY)
    {
        return a.t == b.t && a.u.a == b.u.a;
    }
    return near(a.u.r, b.u.r);
}

int v_compare(value_t a, value_t b)
{
    if (v_is_str(a) && v_is_str(b))
    {
        return strcmp(v_cstring(a), v_cstring(b));
    }
    {
        gmreal_t x = v_num(a), y = v_num(b);
        if (near(x, y))
        {
            return 0;
        }
        return fkey(x) < fkey(y) ? -1 : 1;
    }
}

/* ---- arrays ---- */

static int arr_find(const gmarr_t *a, int32_t idx, bool *found)
{
    int lo = 0, hi = a->n;
    while (lo < hi)
    {
        int mid = (lo + hi) / 2;
        if (a->e[mid].idx < idx)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    *found = lo < a->n && a->e[lo].idx == idx;
    return lo;
}

value_t arr_get(value_t arr, int32_t idx)
{
    bool found;
    int i;
    if (arr.t != VT_ARRAY)
    {
        /* reading index 0 of a plain value gives the value itself */
        return idx == 0 ? arr : v_real(0);
    }
    if (idx < 0 || idx > ARR_IDX_MAX)
    {
        return v_real(0);
    }
    i = arr_find(arr.u.a, idx, &found);
    return found ? arr.u.a->e[i].v : v_real(0);
}

static gmarr_t *arr_new(void)
{
    gmarr_t *a = calloc(1, sizeof(gmarr_t));
    if (!a)
    {
        vm_error("out of memory (array)");
        return NULL;
    }
    a->refs = 1;
    return a;
}

static gmarr_t *arr_copy(const gmarr_t *src)
{
    gmarr_t *a = arr_new();
    if (!a)
    {
        return NULL;
    }
    a->e = malloc(sizeof(arr_entry_t) * (src->n ? src->n : 1));
    a->cap = src->n;
    a->n = src->n;
    for (int i = 0; i < src->n; i++)
    {
        a->e[i] = src->e[i];
        v_retain(a->e[i].v);
    }
    return a;
}

void arr_set(value_t *slot, int32_t idx, value_t v)
{
    gmarr_t *a;
    bool found;
    int i;

    if (idx < 0 || idx > ARR_IDX_MAX)
    {
        vm_error("array index %ld out of range", (long)idx);
        v_release(v);
        return;
    }
    if (slot->t != VT_ARRAY)
    {
        v_release(*slot);
        slot->t = VT_ARRAY;
        slot->u.a = arr_new();
        if (!slot->u.a)
        {
            slot->t = VT_UNDEF;
            v_release(v);
            return;
        }
    }
    else if (slot->u.a->refs > 1)
    {
        gmarr_t *copy = arr_copy(slot->u.a);
        slot->u.a->refs--;
        slot->u.a = copy;
    }
    a = slot->u.a;
    i = arr_find(a, idx, &found);
    if (found)
    {
        v_release(a->e[i].v);
        a->e[i].v = v;
        return;
    }
    if (a->n == a->cap)
    {
        /* 1.5x: RAM is tight on the calculator */
        uint16_t cap = a->cap ? a->cap + a->cap / 2 + 2 : 4;
        arr_entry_t *e = realloc(a->e, sizeof(arr_entry_t) * cap);
        if (!e)
        {
            vm_error("out of memory (array grow)");
            v_release(v);
            return;
        }
        a->e = e;
        a->cap = cap;
    }
    memmove(&a->e[i + 1], &a->e[i], sizeof(arr_entry_t) * (a->n - i));
    a->e[i].idx = idx;
    a->e[i].v = v;
    a->n++;
}
