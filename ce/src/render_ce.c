/*
 * The VM's renderer for the TI-84 Plus CE: 8 bpp, double buffered, using
 * the pack's 256-color palette (index 0 = transparent in images).
 *
 * The LCD can't blend, so:
 * - a translucent rectangle or one-color sprite covering the whole screen
 *   (GameMaker fades) becomes a palette tint for the frame;
 * - other translucent drawing is dithered (checkerboard) or skipped;
 * - image_blend colors use a palette lookup table per color.
 */
#include <graphx.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "heap.h"

#include "../../vm/vm.h"

#define W SCREEN_W
#define H SCREEN_H

static uint8_t base_rgb[256][3];

/* set by the main loop when it's behind: the game still runs this frame's
   draw events (Undertale keeps logic in them) but nothing is drawn */
bool render_skip;
static uint32_t tint_color;
static gmreal_t tint_alpha;
static int shown_a = -1; /* the palette's tint now (apply_palette) */
static uint32_t shown_c;

void render_init(void)
{
    const uint8_t *pal = far_ptr(gd.palette);
    memcpy(base_rgb, pal, 768);
    shown_a = -1;
}

static void apply_palette(void)
{
    /* the tint in 1/256 steps: integer math (soft floats cost ~1M cycles
       for the 768 components) */
    int a = tint_alpha > 0 ? (int)(tint_alpha * 256) : 0;
    int tr = GM_R(tint_color), tg = GM_G(tint_color), tb = GM_B(tint_color);
    if (a > 256)
    {
        a = 256;
    }
    if (a == shown_a && (a == 0 || tint_color == shown_c))
    {
        return;
    }
    for (int i = 0; i < 256; i++)
    {
        int r = base_rgb[i][0], g = base_rgb[i][1], b = base_rgb[i][2];
        if (a)
        {
            r += ((tr - r) * a) >> 8;
            g += ((tg - g) * a) >> 8;
            b += ((tb - b) * a) >> 8;
        }
        gfx_palette[i] = gfx_RGBTo1555(r, g, b);
    }
    shown_a = a;
    shown_c = tint_color;
}

/* ---- colors ---- */

/* squares of 0..255, for color distances in 24-bit math */
static uint16_t sq[256];

static uint8_t nearest(int r, int g, int b)
{
    int best = 1;
    unsigned bestd = 0xffffff;
    if (!sq[2])
    {
        for (int i = 0; i < 256; i++)
        {
            sq[i] = (uint16_t)(i * i);
        }
    }
    for (int i = 1; i < 256; i++)
    {
        const uint8_t *c = base_rgb[i];
        int dr = c[0] - r, dg = c[1] - g, db = c[2] - b;
        unsigned d = (unsigned)sq[dr < 0 ? -dr : dr] + sq[dg < 0 ? -dg : dg] + sq[db < 0 ? -db : db];
        if (d < bestd)
        {
            bestd = d;
            best = i;
        }
    }
    return (uint8_t)best;
}

#define NCOLOR_CACHE 32
static uint32_t ccache_key[NCOLOR_CACHE];
static uint8_t ccache_val[NCOLOR_CACHE], ccache_n, ccache_next;

static uint8_t color_index(uint32_t c)
{
    for (int i = 0; i < ccache_n; i++)
    {
        if (ccache_key[i] == c)
        {
            return ccache_val[i];
        }
    }
    {
        uint8_t v = nearest(GM_R(c), GM_G(c), GM_B(c));
        int slot = ccache_n < NCOLOR_CACHE ? ccache_n++ : ccache_next++ % NCOLOR_CACHE;
        ccache_key[slot] = c;
        ccache_val[slot] = v;
        return v;
    }
}

/* palette index -> index of (color * blend) */
#define NBLEND 4
static uint32_t blend_key[NBLEND];
static uint8_t blend_lut[NBLEND][256] __attribute__((aligned(256))); /* for blit.s */
static uint8_t blend_n, blend_next;

static const uint8_t *blend_table(uint32_t blend)
{
    int slot;
    for (int i = 0; i < blend_n; i++)
    {
        if (blend_key[i] == blend)
        {
            return blend_lut[i];
        }
    }
    slot = blend_n < NBLEND ? blend_n++ : blend_next++ % NBLEND;
    blend_key[slot] = blend;
    blend_lut[slot][0] = 0;
    for (int i = 1; i < 256; i++)
    {
        blend_lut[slot][i] = nearest(base_rgb[i][0] * GM_R(blend) / 255, base_rgb[i][1] * GM_G(blend) / 255,
                                     base_rgb[i][2] * GM_B(blend) / 255);
    }
    return blend_lut[slot];
}

/* ---- profiling (make DEBUG=1) ---- */

#ifdef CE_DEBUG
static uint32_t prof_img, prof_shape, prof_glyph, prof_begin, prof_swap;
#define PROF_START uint32_t prof_t0_ = plat_time_ms()
#define PROF_END(var) (var += plat_time_ms() - prof_t0_)
#elif defined(CE_PROFILE)
/* extra slots of the profile table (tools/ceprof.py) */
enum { prof_begin = 256, prof_img, prof_glyph, prof_shape, prof_swap };
#define PROF_START uint32_t prof_t0_ = prof_now()
#define PROF_END(var) prof_add(var, prof_now() - prof_t0_)
#else
#define PROF_START
#define PROF_END(var)
#endif

/* ---- frame ---- */

void render_begin(uint32_t clear_color, bool clear)
{
    PROF_START;
    tint_alpha = 0;
    if (render_skip)
    {
        return;
    }
    gfx_FillScreen(clear ? color_index(clear_color) : color_index(0));
    PROF_END(prof_begin);
}

#ifdef CE_DEBUG
#include <time.h>
extern unsigned vm_ops;
extern uint32_t code_misses;
uint32_t prof_logic_end; /* set by game_frame before drawing */

static void print_num(int x, int y, uint32_t n)
{
    char buf[12];
    int i = 11;
    buf[i] = 0;
    do
    {
        buf[--i] = (char)('0' + n % 10);
        n /= 10;
    } while (n && i);
    gfx_PrintStringXY(buf + i, x, y);
}
#endif

void render_end(void)
{
#ifdef CE_DEBUG
    /* frame time (ms), VM instructions and code cache misses per frame */
    {
        /* total ms, logic ms, draw ms, ops, misses */
        static uint32_t last;
        static uint32_t last_ops, last_miss;
        uint32_t now = plat_time_ms();
        gfx_SetColor(color_index(0));
        gfx_FillRectangle(0, 0, 220, 10);
        gfx_SetTextFGColor(color_index(0xffffff));
        gfx_SetTextBGColor(color_index(0));
        print_num(0, 1, now - last);
        print_num(44, 1, prof_logic_end - last);
        print_num(88, 1, now - prof_logic_end);
        print_num(132, 1, vm_ops - last_ops);
        print_num(180, 1, code_misses - last_miss);
        gfx_FillRectangle(0, 10, 220, 10);
        print_num(0, 11, prof_begin);
        print_num(44, 11, prof_img);
        print_num(88, 11, prof_shape);
        print_num(132, 11, prof_glyph);
        prof_begin = prof_img = prof_shape = prof_glyph = 0;
        last = now;
        last_ops = vm_ops;
        last_miss = code_misses;
    }
#endif
    if (render_skip)
    {
        return;
    }
    {
        PROF_START;
        apply_palette();
        gfx_SwapDraw();
        PROF_END(prof_swap);
    }
}

/* translucency: 0 skip, 1 dither, 2 solid */
static int alpha_mode(gmreal_t a)
{
    return a < 0.25f ? 0 : a < 0.75f ? 1 : 2;
}

static bool covers_screen(gmreal_t x0, gmreal_t y0, gmreal_t x1, gmreal_t y1)
{
    return x0 <= 0 && y0 <= 0 && x1 >= W && y1 >= H;
}

static void add_tint(uint32_t color, gmreal_t alpha)
{
    /* stack fades: new = old blended toward color */
    if (tint_alpha <= 0)
    {
        tint_color = color;
        tint_alpha = alpha;
    }
    else
    {
        tint_color = color;
        tint_alpha = tint_alpha + (1 - tint_alpha) * alpha;
    }
    if (tint_alpha > 1)
    {
        tint_alpha = 1;
    }
}

/* ---- images ---- */

static uint8_t rowbuf[1024];

/* floor for the few per-call conversions (floorf is very slow here) */
static int ifloor(gmreal_t f)
{
    int32_t t = (int32_t)f;
    if (f < (gmreal_t)t)
    {
        t--;
    }
    return (int)t;
}

/* Decode an RLE row (tools/cepack.py rle_row) into out[0..w). */
static void rle_decode(uint8_t *out, const uint8_t *p, const uint8_t *end, unsigned w)
{
    uint8_t *o = out, *oend = out + w;
    while (p < end && o < oend)
    {
        uint8_t t = *p++;
        unsigned n;
        if (t < 0x40)
        {
            n = t + 1u;
            if (n > (unsigned)(oend - o)) n = (unsigned)(oend - o);
            memset(o, 0, n);
            o += n;
        }
        else if (t < 0x80)
        {
            n = (t & 0x3fu) + 1u;
            if (n > (unsigned)(oend - o)) n = (unsigned)(oend - o);
            memset(o, *p++, n);
            o += n;
        }
        else
        {
            n = (t & 0x7fu) + 1u;
            if (n > (unsigned)(oend - o)) n = (unsigned)(oend - o);
            memcpy(o, p, n);
            p += (t & 0x7fu) + 1u;
            o += n;
        }
    }
    if (o < oend)
    {
        memset(o, 0, (size_t)(oend - o));
    }
}

/* Draw count pixels of an RLE row, starting at source pixel skip, onto
   dst; transparent pixels are left alone. */
void glyph_blit(uint8_t *dst, const uint8_t *bits, unsigned rowb, unsigned h, uint8_t color);
void rle_copy(uint8_t *dst, const uint8_t *p, const uint8_t *end, unsigned count);

static void rle_draw(uint8_t *dst, const uint8_t *p, const uint8_t *end, unsigned skip, unsigned count,
                     const uint8_t *lut)
{
    while (p < end && count)
    {
        uint8_t t = *p++;
        unsigned n;
        if (t < 0x40)
        {
            n = t + 1u;
            if (skip >= n) { skip -= n; continue; }
            n -= skip;
            skip = 0;
            if (n > count) n = count;
            dst += n;
            count -= n;
        }
        else if (t < 0x80)
        {
            uint8_t c = *p++;
            n = (t & 0x3fu) + 1u;
            if (skip >= n) { skip -= n; continue; }
            n -= skip;
            skip = 0;
            if (n > count) n = count;
            memset(dst, lut ? lut[c] : c, n);
            dst += n;
            count -= n;
        }
        else
        {
            unsigned len = (t & 0x7fu) + 1u;
            const uint8_t *src = p;
            p += len;
            if (skip >= len) { skip -= len; continue; }
            src += skip;
            n = len - skip;
            skip = 0;
            if (n > count) n = count;
            if (lut)
            {
                for (unsigned i = 0; i < n; i++)
                {
                    dst[i] = lut[src[i]];
                }
            }
            else
            {
                memcpy(dst, src, n);
            }
            dst += n;
            count -= n;
        }
    }
}
/* scaled images: source pixel per screen column */
static uint8_t *scale_tab[W];
void scale_row(uint8_t *dst, uint8_t *const *tab, unsigned n);
/* blit.s */
void dither_fill(uint8_t *dst, uint8_t n, uint8_t color);
void blit_tab(uint8_t *dst, uint8_t *const *tab, unsigned n, const uint8_t *lut, uint8_t step);
void rot_span(uint8_t *dst, uint8_t n, unsigned u, unsigned v, unsigned du, unsigned dv, const uint8_t *row0,
              uint8_t step);


static void render_image_(far_t img, int w, int h, int sx, int sy, int sw, int sh,
                          gmreal_t x, gmreal_t y, gmreal_t xscale, gmreal_t yscale, gmreal_t angle,
                          gmreal_t ox, gmreal_t oy, uint32_t blend, gmreal_t alpha);

#ifdef CE_TEST
uint32_t test_nimg, test_nimg_skipped; /* draw calls, reported by main.c */
#endif

void render_image(far_t img, int w, int h, int sx, int sy, int sw, int sh,
                  gmreal_t x, gmreal_t y, gmreal_t xscale, gmreal_t yscale, gmreal_t angle,
                  gmreal_t ox, gmreal_t oy, uint32_t blend, gmreal_t alpha)
{
#ifdef CE_TEST
    test_nimg++;
    if (render_skip)
    {
        test_nimg_skipped++;
    }
#endif
    if (render_skip)
    {
        return;
    }
    PROF_START;
    render_image_(img, w, h, sx, sy, sw, sh, x, y, xscale, yscale, angle, ox, oy, blend, alpha);
    PROF_END(prof_img);
}

/* ---- rotated images ---- */

/* The decoded source parts of recently rotated images, laid out for
   rot_span (blit.s): in arenas of 256-byte rows (256-aligned, in one 64 KB
   bank) with a blank row above and below, the blend already applied. A
   wide image has arena 0 to itself; narrower ones share arena 1, side by
   side with a blank column between them. The same images are usually
   drawn again next frame, and decoding costs more than drawing. */
#define RA_ROWS 64 /* the tallest image */
#define RA_SIZE ((RA_ROWS + 2) * 256)
#define NROT 8
static uint8_t *ra_mem[2], *ra_row0[2];
static struct
{
    far_t img;
    int sx, sy, sw, sh;
    uint32_t blend;
    uint8_t arena, col; /* 0: free */
    uint32_t used;
} rotc[NROT];
static uint32_t rot_clock;

static void rot_drop(int k)
{
    if (rotc[k].col)
    {
        uint8_t *p = ra_row0[rotc[k].arena] + rotc[k].col;
        for (int v = 0; v < rotc[k].sh; v++, p += 256)
        {
            memset(p, 0, rotc[k].sw); /* blank again, for the neighbors */
        }
        rotc[k].col = 0;
    }
}

/* malloc is short of memory: give the arenas back */
static bool rot_reclaim(void)
{
    bool any = false;
    for (int a = 0; a < 2; a++)
    {
        if (ra_mem[a])
        {
            for (int k = 0; k < NROT; k++)
            {
                if (rotc[k].col && rotc[k].arena == a)
                {
                    rotc[k].col = 0;
                }
            }
            free(ra_mem[a]);
            ra_mem[a] = ra_row0[a] = NULL;
            any = true;
        }
    }
    return any;
}

static bool ra_get(int a)
{
    if (!ra_mem[a])
    {
        heap_set_reclaim(rot_reclaim);
        ra_mem[a] = malloc_aligned(RA_SIZE);
        if (!ra_mem[a] && a == 0 && ra_mem[1])
        {
            /* the wide one matters more: take the narrow ones' arena */
            rot_reclaim();
            ra_mem[a] = malloc_aligned(RA_SIZE);
        }
        if (ra_mem[a])
        {
            memset(ra_mem[a], 0, RA_SIZE);
            ra_row0[a] = ra_mem[a] + 256;
        }
    }
    return ra_mem[a] != NULL;
}

/* the lowest column range with room for w columns and a blank one after */
static int ra_place(int a, int w)
{
    for (int c = 1; c + w < 256;)
    {
        int clash = -1;
        for (int k = 0; k < NROT; k++)
        {
            if (rotc[k].col && rotc[k].arena == a && c < rotc[k].col + rotc[k].sw + 1 && rotc[k].col < c + w + 1)
            {
                clash = k;
                break;
            }
        }
        if (clash < 0)
        {
            return c;
        }
        c = rotc[clash].col + rotc[clash].sw + 1;
    }
    return 0;
}

/* row 0 of the image, its first column at column 0 of the result */
static const uint8_t *rot_source(far_t img, int w, int sx, int sy, int sw, int sh, uint32_t blend,
                                 const uint8_t *lut, int *col)
{
    int k, a = sw > 120 ? 0 : 1, slot = -1, c;
    for (k = 0; k < NROT; k++)
    {
        if (rotc[k].col && rotc[k].img == img && rotc[k].sx == sx && rotc[k].sy == sy && rotc[k].sw == sw &&
            rotc[k].sh == sh && rotc[k].blend == blend)
        {
            rotc[k].used = ++rot_clock;
            *col = rotc[k].col;
            return ra_row0[rotc[k].arena];
        }
    }
#ifdef CE_PROFILE
    prof_add(273, 1);
#endif
    if (!ra_get(a))
    {
#ifdef CE_PROFILE
        prof_add(279, 1);
#endif
        return NULL;
    }
    /* room: let the least recently used of the arena go until it fits */
    while (!(c = ra_place(a, sw)))
    {
        int old = -1;
        for (k = 0; k < NROT; k++)
        {
            if (rotc[k].col && rotc[k].arena == a && (old < 0 || rotc[k].used < rotc[old].used))
            {
                old = k;
            }
        }
        if (old < 0)
        {
            return NULL;
        }
        rot_drop(old);
    }
    for (k = 0; k < NROT; k++)
    {
        if (!rotc[k].col && (slot < 0 || rotc[k].used < rotc[slot].used))
        {
            slot = k;
        }
    }
    if (slot < 0)
    {
        slot = 0;
        for (k = 1; k < NROT; k++)
        {
            if (rotc[k].used < rotc[slot].used)
            {
                slot = k;
            }
        }
        rot_drop(slot);
    }
    for (int v = 0; v < sh; v++)
    {
        uint8_t *row = ra_row0[a] + v * 256 + c;
        img_decode_row(img, sy + v, rowbuf, w);
        if (lut)
        {
            for (int u = 0; u < sw; u++)
            {
                row[u] = lut[rowbuf[sx + u]];
            }
        }
        else
        {
            memcpy(row, rowbuf + sx, sw);
        }
    }
    rotc[slot].img = img;
    rotc[slot].sx = sx;
    rotc[slot].sy = sy;
    rotc[slot].sw = sw;
    rotc[slot].sh = sh;
    rotc[slot].blend = blend;
    rotc[slot].arena = (uint8_t)a;
    rotc[slot].col = (uint8_t)c;
    rotc[slot].used = ++rot_clock;
    *col = c;
    return ra_row0[a];
}

static void rotate_slow(far_t img, int w, int sx, int sy, int sw, int sh, gmreal_t X, gmreal_t Y, gmreal_t xs,
                        gmreal_t ys, gmreal_t angle, gmreal_t ox, gmreal_t oy, const uint8_t *lut, int mode);

static int32_t fix16(gmreal_t f)
{
    return (int32_t)(f * 65536);
}

/* Rows of the screen are clipped to the image's rotated outline (a
   parallelogram, scanned edge by edge in 16.16 fixed point), then blit.s
   steps through the source along each. */
static void rotate_fast_(far_t img, int w, int sx, int sy, int sw, int sh, gmreal_t X, gmreal_t Y, gmreal_t xs,
                         gmreal_t ys, gmreal_t angle, gmreal_t ox, gmreal_t oy, uint32_t blend, const uint8_t *lut,
                         int mode);
static void rotate_fast(far_t img, int w, int sx, int sy, int sw, int sh, gmreal_t X, gmreal_t Y, gmreal_t xs,
                        gmreal_t ys, gmreal_t angle, gmreal_t ox, gmreal_t oy, uint32_t blend, const uint8_t *lut,
                        int mode)
{
#ifdef CE_PROFILE
    uint32_t t0 = prof_now();
#endif
    rotate_fast_(img, w, sx, sy, sw, sh, X, Y, xs, ys, angle, ox, oy, blend, lut, mode);
#ifdef CE_PROFILE
    prof_add(272, prof_now() - t0);
#endif
}
static void rotate_fast_(far_t img, int w, int sx, int sy, int sw, int sh, gmreal_t X, gmreal_t Y, gmreal_t xs,
                         gmreal_t ys, gmreal_t angle, gmreal_t ox, gmreal_t oy, uint32_t blend, const uint8_t *lut,
                         int mode)
{
    int col;
    const uint8_t *row0 = rot_source(img, w, sx, sy, sw, sh, blend, lut, &col);
    gmreal_t c = gm_dcos(angle), sn = gm_dsin(angle);
    gmreal_t lx[4] = { -ox * xs, (sw - ox) * xs, (sw - ox) * xs, -ox * xs }; /* around the outline */
    gmreal_t ly[4] = { -oy * ys, -oy * ys, (sh - oy) * ys, (sh - oy) * ys };
    gmreal_t px[4], py[4], ymin = 1e9f, ymax = -1e9f;
    int32_t ex[4], eslope[4];
    int er0[4], er1[4], y0, y1;
    int32_t dudx, dudy, dvdx, dvdy, u00, v00;
    if (!row0)
    {
        rotate_slow(img, w, sx, sy, sw, sh, X, Y, xs, ys, angle, ox, oy, lut, mode);
        return;
    }
    for (int i = 0; i < 4; i++)
    {
        px[i] = X + lx[i] * c + ly[i] * sn;
        py[i] = Y - lx[i] * sn + ly[i] * c;
        if (py[i] < ymin) ymin = py[i];
        if (py[i] > ymax) ymax = py[i];
    }
    /* rows whose centers are inside, and each edge's x at them */
    y0 = ifloor(ymin + 0.5f);
    y1 = ifloor(ymax + 0.5f);
    if (y0 < 0) y0 = 0;
    if (y1 > H) y1 = H;
    if (y0 >= y1)
    {
        return;
    }
    for (int i = 0; i < 4; i++)
    {
        gmreal_t xa = px[i], ya = py[i], xb = px[(i + 1) & 3], yb = py[(i + 1) & 3], slope;
        if (ya > yb)
        {
            gmreal_t t = xa; xa = xb; xb = t;
            t = ya; ya = yb; yb = t;
        }
        er0[i] = ifloor(ya + 0.5f); /* first row center at or below ya */
        er1[i] = ifloor(yb + 0.5f);
        if (er1[i] <= er0[i])
        {
            er0[i] = er1[i] = 0; /* flat */
            continue;
        }
        slope = (xb - xa) / (yb - ya);
        ex[i] = fix16(xa + (er0[i] + 0.5f - ya) * slope);
        eslope[i] = fix16(slope);
    }
    /* source position per screen pixel center, in 8.16 */
    dudx = fix16(c / xs);
    dudy = fix16(-sn / xs);
    dvdx = fix16(sn / ys);
    dvdy = fix16(c / ys);
    u00 = fix16(((0.5f - X) * c - (0.5f - Y) * sn) / xs + ox + col);
    v00 = fix16(((0.5f - X) * sn + (0.5f - Y) * c) / ys + oy);
    for (int y = y0; y < y1; y++)
    {
        int32_t xl = INT32_MAX, xr = INT32_MIN;
        int a, b, k0;
        for (int i = 0; i < 4; i++)
        {
            if (y >= er0[i] && y < er1[i])
            {
                int32_t x = ex[i] + eslope[i] * (y - er0[i]);
                if (x < xl) xl = x;
                if (x > xr) xr = x;
            }
        }
        if (xl > xr)
        {
            continue;
        }
        /* pixel centers from xl to xr */
        a = (int)((xl + 32767) >> 16);
        b = (int)((xr + 32767) >> 16);
        if (a < 0) a = 0;
        if (b > W) b = W;
        k0 = mode == 1 ? (a + y) & 1 : 0;
        a += k0;
        if (a >= b)
        {
            continue;
        }
        {
            int32_t u = u00 + dudx * a + dudy * y, v = v00 + dvdx * a + dvdy * y;
            int n = mode == 1 ? (b - a + 1) / 2 : b - a;
            int st = mode == 1 ? 2 : 1;
            int32_t du = dudx * st, dv = dvdx * st;
            uint8_t *dst = &gfx_vbuffer[y][a];
            while (n > 0)
            {
                int k = n > 255 ? 255 : n;
                rot_span(dst, (uint8_t)k, (unsigned)u, (unsigned)v, (unsigned)du, (unsigned)dv, row0, (uint8_t)st);
                n -= k;
                dst += k * st;
                u += du * k;
                v += dv * k;
            }
        }
    }
}

static void rotate_slow(far_t img, int w, int sx, int sy, int sw, int sh, gmreal_t X, gmreal_t Y, gmreal_t xs,
                        gmreal_t ys, gmreal_t angle, gmreal_t ox, gmreal_t oy, const uint8_t *lut, int mode)
{

{
    uint8_t *rotbuf;
    if (!mode || sw * sh > 16384 || !(rotbuf = malloc((size_t)sw * sh)))
    {
        return;
    }
    for (int v = 0; v < sh; v++)
    {
        img_decode_row(img, sy + v, rowbuf, w);
        memcpy(rotbuf + v * sw, rowbuf + sx, sw);
    }
    {
        gmreal_t c = gm_dcos(angle), s = gm_dsin(angle);
        gmreal_t lx[4] = { -ox * xs, (sw - ox) * xs, -ox * xs, (sw - ox) * xs };
        gmreal_t ly[4] = { -oy * ys, -oy * ys, (sh - oy) * ys, (sh - oy) * ys };
        gmreal_t minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
        int32_t dudx, dvdx;
        int x0, x1, y0, y1;
        for (int i = 0; i < 4; i++)
        {
            gmreal_t cx = X + lx[i] * c + ly[i] * s, cy = Y - lx[i] * s + ly[i] * c;
            if (cx < minx) minx = cx;
            if (cx > maxx) maxx = cx;
            if (cy < miny) miny = cy;
            if (cy > maxy) maxy = cy;
        }
        x0 = (int)floorf(minx);
        x1 = (int)ceilf(maxx);
        y0 = (int)floorf(miny);
        y1 = (int)ceilf(maxy);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > W) x1 = W;
        if (y1 > H) y1 = H;
        /* source position per screen pixel, stepped in 16.16 along rows */
        dudx = (int32_t)(c / xs * 65536);
        dvdx = (int32_t)(s / ys * 65536);
        for (int py = y0; py < y1 && x0 < x1; py++)
        {
            uint8_t *dst = &gfx_vbuffer[py][0];
            gmreal_t dx = x0 + 0.5f - X, dy = py + 0.5f - Y;
            int32_t u = (int32_t)(((dx * c - dy * s) / xs + ox) * 65536);
            int32_t v = (int32_t)(((dx * s + dy * c) / ys + oy) * 65536);
            for (int px = x0; px < x1; px++, u += dudx, v += dvdx)
            {
                int iu, iv;
                uint8_t col;
                if (u < 0 || v < 0)
                {
                    continue;
                }
                iu = (int)(u >> 16);
                iv = (int)(v >> 16);
                if (iu >= sw || iv >= sh)
                {
                    continue;
                }
                col = rotbuf[iv * sw + iu];
                if (!col || (mode == 1 && ((px ^ py) & 1)))
                {
                    continue;
                }
                dst[px] = lut ? lut[col] : col;
            }
        }
    }
    free(rotbuf);
}
}

static void render_image_(far_t img, int w, int h, int sx, int sy, int sw, int sh,
                          gmreal_t x, gmreal_t y, gmreal_t xscale, gmreal_t yscale, gmreal_t angle,
                          gmreal_t ox, gmreal_t oy, uint32_t blend, gmreal_t alpha)
{
#ifdef CE_PROFILE
    uint32_t tstart = prof_now();
#endif
    int mode = alpha_mode(alpha);
    const uint8_t *lut = blend == 0xffffff ? NULL : blend_table(blend);
    gmreal_t sc = render_scale;
    gmreal_t X = (x + render_ox) * sc, Y = (y + render_oy) * sc;
    gmreal_t xs = xscale * sc, ys = yscale * sc;

    if (w > 1024 || !img || xs == 0 || ys == 0)
    {
        return;
    }

    if (angle == 0)
    {
        /* screen rectangle of the (sx, sy, sw, sh) part */
        gmreal_t fx0 = X - ox * xs, fy0 = Y - oy * ys;
        gmreal_t fx1 = fx0 + sw * xs, fy1 = fy0 + sh * ys;
        bool flipx = xs < 0, flipy = ys < 0;
        int left, top, dw, dh, cx0, cx1, cy0, cy1;
        if (flipx) { gmreal_t t = fx0; fx0 = fx1; fx1 = t; }
        if (flipy) { gmreal_t t = fy0; fy0 = fy1; fy1 = t; }
        if (alpha < 1 && covers_screen(fx0, fy0, fx1, fy1))
        {
            /* a full-screen fade: tint with the image's color */
            img_decode_row(img, sy + sh / 2, rowbuf, w);
            {
                uint8_t c = rowbuf[sx + sw / 2];
                if (c)
                {
                    uint32_t col = base_rgb[c][0] | base_rgb[c][1] << 8 | (uint32_t)base_rgb[c][2] << 16;
                    if (blend != 0xffffff)
                    {
                        col = (GM_R(col) * GM_R(blend) / 255) | (GM_G(col) * GM_G(blend) / 255) << 8 |
                              (uint32_t)(GM_B(col) * GM_B(blend) / 255) << 16;
                    }
                    add_tint(col, alpha);
                }
            }
            return;
        }
        if (!mode)
        {
            return;
        }
        left = ifloor(fx0 + 0.5f);
        top = ifloor(fy0 + 0.5f);
        dw = ifloor(fx1 + 0.5f) - left;
        dh = ifloor(fy1 + 0.5f) - top;
        if (dw <= 0 || dh <= 0)
        {
            return;
        }
        cx0 = left < 0 ? 0 : left;
        cy0 = top < 0 ? 0 : top;
        cx1 = left + dw > W ? W : left + dw;
        cy1 = top + dh > H ? H : top + dh;
        if (cx0 >= cx1 || cy0 >= cy1)
        {
            return;
        }

#ifdef CE_PROFILE
        prof_add(271, prof_now() - tstart);
#endif
        if (dw == sw && dh == sh)
        {
            /* 1:1 (possibly mirrored): RLE rows straight onto the screen */
            bool tab_ready = false;
#ifdef CE_PROFILE
            uint32_t t11 = prof_now();
            int slot11 = (!flipx && mode == 2) ? 275 : 274;
#endif
            for (int dy = cy0; dy < cy1; dy++)
            {
                int v = dy - top;
#ifdef CE_PROFILE
                uint32_t t0 = prof_now();
#endif
                const uint8_t *end, *p = img_row(img, sy + (flipy ? sh - 1 - v : v), &end);
                uint8_t *dst = &gfx_vbuffer[dy][0];
#ifdef CE_PROFILE
                prof_add(261, prof_now() - t0);
#endif
                if (!flipx && mode == 2)
                {
                    if (!lut && sx + cx0 - left == 0)
                    {
#ifdef CE_PROFILE
                        t0 = prof_now();
#endif
                        rle_copy(dst + cx0, p, end, (unsigned)(cx1 - cx0)); /* rle.s */
#ifdef CE_PROFILE
                        prof_add(262, prof_now() - t0);
#endif
                    }
                    else
                    {
                        rle_draw(dst + cx0, p, end, (unsigned)(sx + cx0 - left), (unsigned)(cx1 - cx0), lut);
                    }
                }
                else
                {
                    /* mirrored or dithered: decode, then through the column table */
                    int k0 = mode == 1 ? (cx0 + dy) & 1 : 0;
                    rle_decode(rowbuf, p, end, (unsigned)w);
                    if (!tab_ready)
                    {
                        for (int k = 0; k < cx1 - cx0; k++)
                        {
                            int u = cx0 + k - left;
                            scale_tab[k] = &rowbuf[sx + (flipx ? sw - 1 - u : u)];
                        }
                        tab_ready = true;
                    }
                    blit_tab(dst + cx0 + k0, scale_tab + k0,
                             mode == 1 ? (unsigned)(cx1 - cx0 - k0 + 1) / 2 : (unsigned)(cx1 - cx0), lut,
                             mode == 1 ? 2 : 1);
                }
            }
#ifdef CE_PROFILE
            prof_add(slot11, prof_now() - t11);
#endif
            return;
        }

        /* scaled: 16.16 fixed-point steps through the source */
        {
#ifdef CE_PROFILE
            uint32_t tsc = prof_now();
#endif
            uint32_t ustep = (uint32_t)((gmreal_t)sw * 65536 / dw);
            uint32_t vstep = (uint32_t)((gmreal_t)sh * 65536 / dh);
            uint32_t vacc = vstep / 2 + vstep * (uint32_t)(cy0 - top);
            uint32_t uacc = ustep / 2 + ustep * (uint32_t)(cx0 - left);
            int prev_v = -1, n = cx1 - cx0;
            bool dither = mode == 1, fast = !dither && !lut;
            /* the source pixel of each screen column, the same on every row */
            for (int k = 0; k < n; k++, uacc += ustep)
            {
                int u = (int)(uacc >> 16);
                if (u >= sw)
                {
                    u = sw - 1;
                }
                scale_tab[k] = &rowbuf[sx + (flipx ? sw - 1 - u : u)];
            }
            for (int dy = cy0; dy < cy1; dy++, vacc += vstep)
            {
                int v = (int)(vacc >> 16);
                uint8_t *dst = &gfx_vbuffer[dy][cx0];
                if (v >= sh)
                {
                    v = sh - 1;
                }
                if (flipy)
                {
                    v = sh - 1 - v;
                }
                if (v != prev_v)
                {
#ifdef CE_PROFILE
                    uint32_t t0 = prof_now();
#endif
                    const uint8_t *end, *p = img_row(img, sy + v, &end);
                    rle_decode(rowbuf, p, end, (unsigned)w);
                    prev_v = v;
#ifdef CE_PROFILE
                    prof_add(263, prof_now() - t0);
#endif
                }
                if (fast)
                {
                    scale_row(dst, scale_tab, (unsigned)n); /* rle.s */
                    continue;
                }
                {
                    int k0 = dither ? (cx0 + dy) & 1 : 0;
                    blit_tab(dst + k0, scale_tab + k0, dither ? (unsigned)(n - k0 + 1) / 2 : (unsigned)n, lut,
                             dither ? 2 : 1);
                }
            }
#ifdef CE_PROFILE
            prof_add(265, prof_now() - tsc);
            prof_add(fast ? 278 : 277, prof_now() - tsc);
            if (n * (cy1 - cy0) < 400)
            {
                prof_add(276, prof_now() - tsc);
            }
#endif
        }
        return;
    }

    /* rotated */
    if (!mode)
    {
        return;
    }
    if (sw < 254 && sh <= RA_ROWS)
    {
        rotate_fast(img, w, sx, sy, sw, sh, X, Y, xs, ys, angle, ox, oy, blend, lut, mode);
        return;
    }
    rotate_slow(img, w, sx, sy, sw, sh, X, Y, xs, ys, angle, ox, oy, lut, mode);
}

static void render_glyph_(far_t bitmap, int gw, int gh, gmreal_t x, gmreal_t y, gmreal_t xscale, gmreal_t yscale,
                          gmreal_t angle, uint32_t color, gmreal_t alpha);
static void glyph_px(const uint8_t *bits, int gw, int gh, int left, int top, int dw, int dh, uint8_t ci, int mode);

void render_glyph_px(far_t bitmap, int gw, int gh, int left, int top, int dw, int dh, uint32_t color, gmreal_t alpha)
{
    int mode;
    if (render_skip)
    {
        return;
    }
    mode = alpha_mode(alpha);
    if (!mode)
    {
        return;
    }
    PROF_START;
    glyph_px(far_ptr(bitmap), gw, gh, left, top, dw, dh, color_index(color), mode);
    PROF_END(prof_glyph);
}

void render_glyph(far_t bitmap, int gw, int gh, gmreal_t x, gmreal_t y, gmreal_t xscale, gmreal_t yscale,
                  gmreal_t angle, uint32_t color, gmreal_t alpha)
{
    if (render_skip)
    {
        return;
    }
    PROF_START;
    render_glyph_(bitmap, gw, gh, x, y, xscale, yscale, angle, color, alpha);
    PROF_END(prof_glyph);
}

static void render_glyph_(far_t bitmap, int gw, int gh, gmreal_t x, gmreal_t y, gmreal_t xscale, gmreal_t yscale,
                          gmreal_t angle, uint32_t color, gmreal_t alpha)
{
    const uint8_t *bits = far_ptr(bitmap);
    int mode = alpha_mode(alpha);
    uint8_t ci;
    gmreal_t sc = render_scale;
    gmreal_t X = (x + render_ox) * sc, Y = (y + render_oy) * sc, xs = xscale * sc, ys = yscale * sc;
    int left, top, dw, dh;
    (void)angle; /* rotated text is drawn upright */
    if (!mode || xs <= 0 || ys <= 0)
    {
        return;
    }
    ci = color_index(color);
    left = ifloor(X + 0.5f);
    top = ifloor(Y + 0.5f);
    dw = ifloor(X + gw * xs + 0.5f) - left;
    dh = ifloor(Y + gh * ys + 0.5f) - top;
    glyph_px(bits, gw, gh, left, top, dw, dh, ci, mode);
}

static void glyph_px(const uint8_t *bits, int gw, int gh, int left, int top, int dw, int dh, uint8_t ci, int mode)
{
    int rowb = (gw + 7) / 8;
    if (dw <= 0 || dh <= 0 || left >= W || top >= H || left + dw <= 0 || top + dh <= 0 || gw > 255 || gh > 255)
    {
        return;
    }
    if (dw == gw && dh == gh && mode == 2 && left >= 0 && top >= 0 && left + gw <= W && top + gh <= H)
    {
        /* 1:1 and fully on screen: the common case */
        glyph_blit(&gfx_vbuffer[top][left], bits, rowb, gh, ci);
        return;
    }
    {
        /* scaled or clipped: each screen column's source byte and bit
           first (8.8 fixed point; glyphs are small) */
        static uint8_t ubyte[W], ubit[W];
        unsigned ustep = ((unsigned)gw << 8) / (unsigned)dw, vstep = ((unsigned)gh << 8) / (unsigned)dh;
        int x0 = left < 0 ? -left : 0, x1 = left + dw > W ? W - left : dw;
        int y0 = top < 0 ? -top : 0, y1 = top + dh > H ? H - top : dh;
        unsigned acc = ustep / 2 + ustep * (unsigned)x0;
        bool dither = mode == 1;
        for (int dx = x0; dx < x1; dx++, acc += ustep)
        {
            unsigned u = acc >> 8;
            if (u >= (unsigned)gw)
            {
                u = (unsigned)gw - 1;
            }
            ubyte[dx] = (uint8_t)(u >> 3);
            ubit[dx] = (uint8_t)(0x80 >> (u & 7));
        }
        acc = vstep / 2 + vstep * (unsigned)y0;
        for (int dy = y0; dy < y1; dy++, acc += vstep)
        {
            unsigned v = acc >> 8;
            int py = top + dy;
            const uint8_t *row = bits + (v < (unsigned)gh ? v : (unsigned)gh - 1) * rowb;
            uint8_t *dst = &gfx_vbuffer[py][left];
            for (int dx = x0; dx < x1; dx++)
            {
                if ((row[ubyte[dx]] & ubit[dx]) && !(dither && ((left + dx + py) & 1)))
                {
                    dst[dx] = ci;
                }
            }
        }
    }
}

void render_glyph_screen(far_t bitmap, int gw, int gh, int x, int y, uint32_t color, gmreal_t alpha)
{
    if (render_skip)
    {
        return;
    }
    const uint8_t *bits;
    int rowb = (gw + 7) / 8, mode = alpha_mode(alpha);
    uint8_t ci;
    PROF_START;
    if (!mode || x >= W || y >= H || x + gw <= 0 || y + gh <= 0)
    {
        PROF_END(prof_glyph);
        return;
    }
    ci = color_index(color);
    bits = far_ptr(bitmap);
    if (mode == 2 && x >= 0 && y >= 0 && x + gw <= W && y + gh <= H)
    {
        glyph_blit(&gfx_vbuffer[y][x], bits, rowb, gh, ci);
        PROF_END(prof_glyph);
        return;
    }
    for (int v = 0; v < gh; v++, bits += rowb)
    {
        int py = y + v;
        uint8_t *dst;
        const uint8_t *b = bits;
        uint8_t m = 0x80, byte = *b;
        if (py < 0 || py >= H)
        {
            continue;
        }
        dst = &gfx_vbuffer[py][0];
        for (int u = 0; u < gw; u++)
        {
            int px = x + u;
            if ((byte & m) && px >= 0 && px < W && !(mode == 1 && ((px ^ py) & 1)))
            {
                dst[px] = ci;
            }
            m >>= 1;
            if (!m)
            {
                m = 0x80;
                byte = *++b;
            }
        }
    }
    PROF_END(prof_glyph);
}

/* ---- shapes ---- */

static void span(int y, int x0, int x1, uint8_t c, int mode)
{
    uint8_t *dst;
    if (y < 0 || y >= H)
    {
        return;
    }
    if (x0 < 0) x0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (x0 > x1)
    {
        return;
    }
    dst = &gfx_vbuffer[y][0];
    if (mode == 2)
    {
        memset(dst + x0, c, x1 - x0 + 1);
        return;
    }
    x0 += (x0 ^ y) & 1; /* the pixels with x + y even */
    if (x0 <= x1)
    {
        dither_fill(dst + x0, (uint8_t)((x1 - x0) / 2 + 1), c); /* blit.s */
    }
}

static void render_rect_(gmreal_t x1, gmreal_t y1, gmreal_t x2, gmreal_t y2, uint32_t color, gmreal_t alpha, bool outline);

void render_rect(gmreal_t x1, gmreal_t y1, gmreal_t x2, gmreal_t y2, uint32_t color, gmreal_t alpha, bool outline)
{
    if (render_skip)
    {
        return;
    }
    PROF_START;
    render_rect_(x1, y1, x2, y2, color, alpha, outline);
    PROF_END(prof_shape);
}

static void render_rect_(gmreal_t x1, gmreal_t y1, gmreal_t x2, gmreal_t y2, uint32_t color, gmreal_t alpha, bool outline)
{
    gmreal_t sc = render_scale;
    int a, b, c, d, mode = alpha_mode(alpha);
    uint8_t ci;
    if (x1 > x2) { gmreal_t t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { gmreal_t t = y1; y1 = y2; y2 = t; }
    if (render_int)
    {
        a = ifloor(x1) + render_iox;
        b = ifloor(y1) + render_ioy;
        c = ifloor(x2) + render_iox;
        d = ifloor(y2) + render_ioy;
    }
    else
    {
        a = ifloor((x1 + render_ox) * sc);
        b = ifloor((y1 + render_oy) * sc);
        c = ifloor((x2 + 1 + render_ox) * sc) - 1;
        d = ifloor((y2 + 1 + render_oy) * sc) - 1;
    }
    if (c < a) c = a;
    if (d < b) d = b;
    if (!outline && alpha < 1 && a <= 0 && b <= 0 && c >= W - 1 && d >= H - 1)
    {
        add_tint(color, alpha);
        return;
    }
    if (!mode)
    {
        return;
    }
    ci = color_index(color);
    if (outline)
    {
        span(b, a, c, ci, mode);
        span(d, a, c, ci, mode);
        for (int y = b + 1; y < d; y++)
        {
            span(y, a, a, ci, mode);
            span(y, c, c, ci, mode);
        }
        return;
    }
    if (mode == 2)
    {
        /* graphx's fill is assembly: much faster than spans in C */
        if (a < 0) a = 0;
        if (b < 0) b = 0;
        if (c > W - 1) c = W - 1;
        if (d > H - 1) d = H - 1;
        if (a <= c && b <= d)
        {
            gfx_SetColor(ci);
            gfx_FillRectangle_NoClip(a, (uint8_t)b, c - a + 1, (uint8_t)(d - b + 1));
        }
        return;
    }
    for (int y = b; y <= d; y++)
    {
        span(y, a, c, ci, mode);
    }
}

void render_line(gmreal_t x1, gmreal_t y1, gmreal_t x2, gmreal_t y2, gmreal_t width, uint32_t color, gmreal_t alpha)
{
    if (render_skip)
    {
        return;
    }
    gmreal_t sc = render_scale, dx, dy;
    int steps, hw, mode = alpha_mode(alpha);
    uint8_t ci;
    if (!mode)
    {
        return;
    }
    ci = color_index(color);
    x1 = (x1 + render_ox) * sc;
    y1 = (y1 + render_oy) * sc;
    x2 = (x2 + render_ox) * sc;
    y2 = (y2 + render_oy) * sc;
    width *= sc;
    dx = x2 - x1;
    dy = y2 - y1;
    steps = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy)) + 1;
    hw = (int)(width / 2);
    for (int i = 0; i <= steps; i++)
    {
        gmreal_t f = (gmreal_t)i / steps;
        int px = ifloor(x1 + dx * f), py = ifloor(y1 + dy * f);
        for (int oy = -hw; oy <= hw; oy++)
        {
            span(py + oy, px - hw, px + hw, ci, mode);
        }
    }
}

void render_circle(gmreal_t x, gmreal_t y, gmreal_t r, uint32_t color, gmreal_t alpha, bool outline)
{
    if (render_skip)
    {
        return;
    }
    gmreal_t sc = render_scale;
    int mode = alpha_mode(alpha), y0, y1;
    uint8_t ci;
    if (!mode)
    {
        return;
    }
    ci = color_index(color);
    x = (x + render_ox) * sc;
    y = (y + render_oy) * sc;
    r *= sc;
    y0 = (int)floorf(y - r);
    y1 = (int)ceilf(y + r);
    for (int py = y0; py <= y1; py++)
    {
        gmreal_t dy = py + 0.5f - y;
        gmreal_t half;
        if (fabsf(dy) > r)
        {
            continue;
        }
        half = sqrtf(r * r - dy * dy);
        if (outline)
        {
            gmreal_t inner = r - 1 > fabsf(dy) ? sqrtf((r - 1) * (r - 1) - dy * dy) : 0;
            span(py, (int)floorf(x - half), (int)floorf(x - inner), ci, mode);
            span(py, (int)floorf(x + inner), (int)floorf(x + half), ci, mode);
        }
        else
        {
            span(py, (int)floorf(x - half), (int)floorf(x + half), ci, mode);
        }
    }
}

void render_triangle(gmreal_t x1, gmreal_t y1, gmreal_t x2, gmreal_t y2, gmreal_t x3, gmreal_t y3, uint32_t color,
                     gmreal_t alpha, bool outline)
{
    if (render_skip)
    {
        return;
    }
    gmreal_t sc = render_scale;
    int mode = alpha_mode(alpha);
    uint8_t ci;
    if (outline)
    {
        render_line(x1, y1, x2, y2, 1, color, alpha);
        render_line(x2, y2, x3, y3, 1, color, alpha);
        render_line(x3, y3, x1, y1, 1, color, alpha);
        return;
    }
    if (!mode)
    {
        return;
    }
    ci = color_index(color);
    x1 = (x1 + render_ox) * sc; y1 = (y1 + render_oy) * sc;
    x2 = (x2 + render_ox) * sc; y2 = (y2 + render_oy) * sc;
    x3 = (x3 + render_ox) * sc; y3 = (y3 + render_oy) * sc;
    {
        int ya = (int)floorf(fminf(y1, fminf(y2, y3))), yb = (int)ceilf(fmaxf(y1, fmaxf(y2, y3)));
        for (int py = ya; py <= yb; py++)
        {
            /* intersect the scanline with the three edges */
            gmreal_t yy = py + 0.5f, xs[3];
            int n = 0;
            gmreal_t ex[3][4] = { { x1, y1, x2, y2 }, { x2, y2, x3, y3 }, { x3, y3, x1, y1 } };
            for (int e = 0; e < 3; e++)
            {
                gmreal_t ax = ex[e][0], ay = ex[e][1], bx = ex[e][2], by = ex[e][3];
                if ((yy >= ay && yy < by) || (yy >= by && yy < ay))
                {
                    xs[n++] = ax + (yy - ay) * (bx - ax) / (by - ay);
                }
            }
            if (n >= 2)
            {
                gmreal_t l = fminf(xs[0], xs[1]), r = fmaxf(xs[0], xs[1]);
                span(py, (int)ceilf(l - 0.5f), (int)ceilf(r - 0.5f) - 1, ci, mode);
            }
        }
    }
}
