/* The draw pass (views, backgrounds, tiles and instances by depth), tile
   layers, paths and text. Pixels go through render_* (per platform). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"

gmreal_t render_ox, render_oy, render_scale = 1;
bool render_int;
int render_iox, render_ioy;

static int ifloor(gmreal_t f)
{
    int32_t t = (int32_t)f;
    if (f < (gmreal_t)t)
    {
        t--;
    }
    return (int)t;
}

/* floor(x * 256) (about: for drawing only), from the float's bits: float
   math costs thousands of cycles per operation on the calculator */
static int32_t fix8(gmreal_t x)
{
    union
    {
        gmreal_t f;
        uint8_t b[4];
    } v;
    int e;
    unsigned m;
    int32_t r;
    v.f = x;
    e = (((v.b[3] & 0x7f) << 1) | (v.b[2] >> 7)) - 127;
    if (e < -9)
    {
        return 0;
    }
    if (e > 15)
    {
        return (v.b[3] & 0x80) ? -(32767 << 8) : (32767 << 8);
    }
    m = (unsigned)(v.b[2] | 0x80) << 16 | (unsigned)v.b[1] << 8 | v.b[0]; /* 24 bits */
    r = (int32_t)(m >> (15 - e));
    return (v.b[3] & 0x80) ? -r : r;
}

/* x * 256 when that is a whole number from 1 to 2048 (a scale drawn in
   fixed point), else 0 */
static int32_t scale8(gmreal_t x)
{
    int32_t f = fix8(x);
    return f > 0 && f <= 2048 && (gmreal_t)f == x * 256 ? f : 0;
}

/* ---- tiles of the current room ---- */

/* Tiles are drawn straight from the pack; what the game can change is per
   layer (all tiles at one depth): a shift and whether it shows. A room's
   tiles use at most 256 depths (the record's depth is an index). */
typedef struct
{
    int32_t depth;
    int16_t dx, dy;
    uint8_t visible;
    int first; /* its first tile: layers draw in that order at equal depth */
} tile_layer_t;

static int ntiles;
static uint32_t first_tile;
static uint8_t layer_of[256]; /* tile record depth index -> layer */
static tile_layer_t *layers;
static int nlayers;

void tiles_load_room(int room)
{
    const room_rec_t *r = ROOM(room);
    free(layers);
    layers = NULL;
    nlayers = 0;
    ntiles = r->ntiles;
    first_tile = r->first_tile;
    memset(layer_of, 0xff, sizeof layer_of);
    for (int i = 0; i < ntiles; i++)
    {
        uint8_t d = TILE(first_tile + i)->depth;
        if (layer_of[d] == 0xff)
        {
            layer_of[d] = (uint8_t)nlayers++;
        }
    }
    if (!nlayers)
    {
        return;
    }
    layers = malloc(sizeof(tile_layer_t) * nlayers);
    if (!layers)
    {
        vm_error("out of memory (tile layers)");
        ntiles = nlayers = 0;
        return;
    }
    memset(layer_of, 0xff, sizeof layer_of);
    nlayers = 0;
    for (int i = 0; i < ntiles; i++)
    {
        const tile_rec_t *t = TILE(first_tile + i);
        if (layer_of[t->depth] == 0xff)
        {
            tile_layer_t *l = &layers[nlayers];
            layer_of[t->depth] = (uint8_t)nlayers++;
            l->depth = tile_depth(t);
            l->dx = l->dy = 0;
            l->visible = 1;
            l->first = i;
        }
    }
}

void tile_layer_shift(int depth, int dx, int dy)
{
    for (int i = 0; i < nlayers; i++)
    {
        if (layers[i].depth == depth)
        {
            layers[i].dx += dx;
            layers[i].dy += dy;
        }
    }
}

void tile_layer_visible(int depth, bool visible)
{
    for (int i = 0; i < nlayers; i++)
    {
        if (layers[i].depth == depth)
        {
            layers[i].visible = visible;
        }
    }
}

/* the tiles of one layer */
static void draw_layer(int li)
{
    const tile_layer_t *l = &layers[li];
    if (!l->visible)
    {
        return;
    }
    for (int i = l->first; i < ntiles; i++)
    {
        const tile_rec_t *r = TILE(first_tile + i);
        far_t img;
        if (layer_of[r->depth] != li)
        {
            continue;
        }
        trace_use(TRACE_BG, r->bg);
        img = bg_image(r->bg);
        if (img)
        {
            render_image(img, BG(r->bg)->w, BG(r->bg)->h, r->sx, r->sy, r->w, r->h, (gmreal_t)(r->x + l->dx),
                         (gmreal_t)(r->y + l->dy), 1, 1, 0, 0, 0, 0xffffff, 1);
        }
    }
}

/* ---- sprites and backgrounds ---- */

void draw_sprite_full(int spr, gmreal_t frame, gmreal_t x, gmreal_t y, gmreal_t xs, gmreal_t ys,
                      gmreal_t angle, uint32_t blend, gmreal_t alpha)
{
    const sprite_rec_t *s;
    far_t img;
    if (spr < 0 || spr >= (int)gd.nsprites)
    {
        return;
    }
    trace_use(TRACE_SPRITE, spr);
    s = SPR(spr);
    img = sprite_image(spr, (int)floorf(frame));
    if (!img)
    {
        return;
    }
    render_image(img, s->w, s->h, 0, 0, s->w, s->h, x, y, xs, ys, angle, s->ox, s->oy, blend, alpha);
}

void draw_sprite_part_full(int spr, gmreal_t frame, int left, int top, int w, int h,
                           gmreal_t x, gmreal_t y, gmreal_t xs, gmreal_t ys, uint32_t blend, gmreal_t alpha)
{
    const sprite_rec_t *s;
    far_t img;
    if (spr < 0 || spr >= (int)gd.nsprites)
    {
        return;
    }
    trace_use(TRACE_SPRITE, spr);
    s = SPR(spr);
    img = sprite_image(spr, (int)floorf(frame));
    if (!img)
    {
        return;
    }
    if (left < 0) { w += left; x -= left * xs; left = 0; }
    if (top < 0) { h += top; y -= top * ys; top = 0; }
    if (left + w > s->w) w = s->w - left;
    if (top + h > s->h) h = s->h - top;
    if (w <= 0 || h <= 0)
    {
        return;
    }
    render_image(img, s->w, s->h, left, top, w, h, x, y, xs, ys, 0, 0, 0, blend, alpha);
}

void draw_background_full(int bg, int left, int top, int w, int h, gmreal_t x, gmreal_t y,
                          gmreal_t xs, gmreal_t ys, uint32_t blend, gmreal_t alpha)
{
    far_t img = bg_image(bg);
    const bg_rec_t *b;
    trace_use(TRACE_BG, bg);
    if (!img)
    {
        return;
    }
    b = BG(bg);
    if (w < 0)
    {
        w = b->w;
        h = b->h;
    }
    if (left + w > b->w) w = b->w - left;
    if (top + h > b->h) h = b->h - top;
    if (w <= 0 || h <= 0)
    {
        return;
    }
    render_image(img, b->w, b->h, left, top, w, h, x, y, xs, ys, 0, 0, 0, blend, alpha);
}

void draw_self(instance_t *in)
{
    draw_sprite_full(in->sprite_index, in->image_index, in->x, in->y, in->image_xscale,
                     in->image_yscale, in->image_angle, in->image_blend, in->image_alpha);
}

static void draw_room_background(background_t *b, const view_t *v)
{
    const bg_rec_t *r;
    gmreal_t x0, y0, x1, y1;
    if (!b->visible || b->index < 0 || b->index >= (int)gd.nbgs || !bg_image(b->index))
    {
        return;
    }
    r = BG(b->index);
    if (!r->w || !r->h)
    {
        return;
    }
    x0 = b->x;
    y0 = b->y;
    x1 = x0;
    y1 = y0;
    if (b->htiled)
    {
        gmreal_t w = r->w * b->xscale;
        x0 = v->x - fmodf(v->x - b->x, w);
        if (x0 > v->x) x0 -= w;
        x1 = v->x + v->w;
    }
    if (b->vtiled)
    {
        gmreal_t h = r->h * b->yscale;
        y0 = v->y - fmodf(v->y - b->y, h);
        if (y0 > v->y) y0 -= h;
        y1 = v->y + v->h;
    }
    for (gmreal_t y = y0; y <= y1; y += r->h * b->yscale)
    {
        for (gmreal_t x = x0; x <= x1; x += r->w * b->xscale)
        {
            draw_background_full(b->index, 0, 0, -1, -1, x, y, b->xscale, b->yscale, b->blend, b->alpha);
            if (!b->htiled)
            {
                break;
            }
        }
        if (!b->vtiled)
        {
            break;
        }
    }
}

/* ---- views ---- */

static void view_follow(view_t *v)
{
    instance_t *list[1];
    instance_t *in;
    if (v->follow < 0 || inst_select(v->follow, list, 1) == 0)
    {
        return;
    }
    in = list[0];
    if (2 * v->hborder >= v->w)
    {
        v->x = in->x - v->w / 2;
    }
    else if (in->x - v->hborder < v->x)
    {
        v->x = in->x - v->hborder;
    }
    else if (in->x + v->hborder > v->x + v->w)
    {
        v->x = in->x + v->hborder - v->w;
    }
    if (2 * v->vborder >= v->h)
    {
        v->y = in->y - v->h / 2;
    }
    else if (in->y - v->vborder < v->y)
    {
        v->y = in->y - v->vborder;
    }
    else if (in->y + v->vborder > v->y + v->h)
    {
        v->y = in->y + v->vborder - v->h;
    }
    if (v->x > gs.room_width - v->w) v->x = gs.room_width - v->w;
    if (v->y > gs.room_height - v->h) v->y = gs.room_height - v->h;
    if (v->x < 0) v->x = 0;
    if (v->y < 0) v->y = 0;
}

/* ---- the draw pass ---- */

typedef struct
{
    gmreal_t depth;
    int order;
    instance_t *inst; /* NULL: a tile depth layer */
    int32_t tile_depth;
} drawable_t;

static int cmp_drawable(const void *a, const void *b)
{
    const drawable_t *x = a, *y = b;
    if (x->depth != y->depth)
    {
        return x->depth > y->depth ? -1 : 1;
    }
    return x->order - y->order;
}

static void draw_view(const view_t *v)
{
    drawable_t *list;
    int n = 0;

    render_ox = -v->x;
    render_oy = -v->y;
    render_scale = 1;
    if (v->w > 0 && v->h > 0)
    {
        gmreal_t sx = SCREEN_W / v->w, sy = SCREEN_H / v->h;
        render_scale = sx < sy ? sx : sy;
    }
    render_iox = ifloor(render_ox);
    render_ioy = ifloor(render_oy);
    render_int = render_scale == 1 && (gmreal_t)render_iox == render_ox && (gmreal_t)render_ioy == render_oy;
    for (int i = 0; i < 8; i++)
    {
        if (!gs.bgs[i].foreground)
        {
            draw_room_background(&gs.bgs[i], v);
        }
    }

    /* tiles: one entry per layer */
    list = malloc(sizeof(drawable_t) * (ninstances + nlayers + 1));
    if (!list)
    {
        vm_error("out of memory (draw list)");
        return;
    }
    for (int i = 0; i < nlayers; i++)
    {
        list[n].depth = (gmreal_t)layers[i].depth;
        list[n].order = -1000000 + layers[i].first;
        list[n].inst = NULL;
        list[n].tile_depth = i; /* the layer */
        n++;
    }
    for (int i = 0; i < ninstances; i++)
    {
        if (instances[i]->alive && instances[i]->visible)
        {
            list[n].depth = instances[i]->depth;
            list[n].order = i;
            list[n].inst = instances[i];
            n++;
        }
    }
    qsort(list, n, sizeof(drawable_t), cmp_drawable);
    for (int i = 0; i < n; i++)
    {
        if (list[i].inst)
        {
            instance_t *in = list[i].inst;
            if (!in->alive)
            {
                continue;
            }
            if (!event_run(in, NULL, EV_DRAW, 0))
            {
                draw_self(in);
            }
        }
        else
        {
            draw_layer((int)list[i].tile_depth);
        }
    }
    free(list);

    for (int i = 0; i < 8; i++)
    {
        if (gs.bgs[i].foreground)
        {
            draw_room_background(&gs.bgs[i], v);
        }
    }
}

void draw_frame(void)
{
    for (int i = 0; i < 8; i++)
    {
        gs.bgs[i].x += gs.bgs[i].hspeed;
        gs.bgs[i].y += gs.bgs[i].vspeed;
    }
    render_begin(gs.background_color, gs.background_showcolor);
    if (gs.views_enabled)
    {
        for (int i = 0; i < MAX_VIEWS; i++)
        {
            view_t *v = &gs.views[i];
            if (!v->enabled || !v->visible)
            {
                continue;
            }
            view_follow(v);
            gs.view_current = i;
            draw_view(v);
        }
    }
    else
    {
        view_t v;
        memset(&v, 0, sizeof v);
        v.w = gs.room_width;
        v.h = gs.room_height;
        gs.view_current = 0;
        draw_view(&v);
    }
    render_end();
}

/* ---- paths ---- */

static gmreal_t path_length(const path_rec_t *p)
{
    gmreal_t len = 0;
    int n = p->npoints + (p->closed ? 1 : 0);
    for (int i = 1; i < n; i++)
    {
        const path_point_t *a = POINT(p->first_point + (i - 1) % p->npoints), *b = POINT(p->first_point + i % p->npoints);
        len += sqrtf((b->x - a->x) * (b->x - a->x) + (b->y - a->y) * (b->y - a->y));
    }
    return len;
}

static void path_point_at(const path_rec_t *p, gmreal_t pos, gmreal_t *x, gmreal_t *y)
{
    gmreal_t total = path_length(p), target = pos * total, acc = 0;
    int n = p->npoints + (p->closed ? 1 : 0);
    if (p->npoints == 0)
    {
        *x = *y = 0;
        return;
    }
    for (int i = 1; i < n; i++)
    {
        const path_point_t *a = POINT(p->first_point + (i - 1) % p->npoints), *b = POINT(p->first_point + i % p->npoints);
        gmreal_t seg = sqrtf((b->x - a->x) * (b->x - a->x) + (b->y - a->y) * (b->y - a->y));
        if (acc + seg >= target && seg > 0)
        {
            gmreal_t f = (target - acc) / seg;
            *x = a->x + (b->x - a->x) * f;
            *y = a->y + (b->y - a->y) * f;
            return;
        }
        acc += seg;
    }
    *x = POINT(p->first_point + (n - 1) % p->npoints)->x;
    *y = POINT(p->first_point + (n - 1) % p->npoints)->y;
}

void path_begin(instance_t *in, int path, gmreal_t speed, int endaction, bool absolute)
{
    const path_rec_t *p;
    gmreal_t px, py;
    if (path < 0 || path >= (int)gd.npaths)
    {
        return;
    }
    p = PATH(path);
    inst_m(in)->path_index = (int16_t)path;
    inst_m(in)->path_speed = speed;
    inst_m(in)->path_endaction = (uint8_t)endaction;
    inst_m(in)->path_position = speed >= 0 ? 0 : 1;
    path_point_at(p, in->m->path_position, &px, &py);
    if (absolute)
    {
        inst_m(in)->path_xstart = 0;
        inst_m(in)->path_ystart = 0;
        in->x = px;
        in->y = py;
    }
    else
    {
        inst_m(in)->path_xstart = in->x - px;
        inst_m(in)->path_ystart = in->y - py;
    }
    inst_m(in)->hspeed = inst_m(in)->vspeed = inst_m(in)->speed = 0;
}

void path_step(instance_t *in)
{
    const path_rec_t *p = PATH(in->m->path_index);
    gmreal_t len = path_length(p), px, py;
    bool ended = false;
    if (len <= 0)
    {
        inst_m(in)->path_index = -1;
        return;
    }
    inst_m(in)->path_position += in->m->path_speed / len;
    if (in->m->path_position >= 1 || in->m->path_position < 0)
    {
        switch (in->m->path_endaction)
        {
        case 0: /* stop */
            inst_m(in)->path_position = in->m->path_position >= 1 ? 1 : 0;
            ended = true;
            break;
        case 1: /* restart */
            inst_m(in)->path_position = fmodf(in->m->path_position + 1, 1);
            break;
        case 2: /* continue from here */
        {
            gmreal_t ex, ey, sx, sy;
            path_point_at(p, 1, &ex, &ey);
            path_point_at(p, 0, &sx, &sy);
            inst_m(in)->path_xstart += ex - sx;
            inst_m(in)->path_ystart += ey - sy;
            inst_m(in)->path_position = fmodf(in->m->path_position + 1, 1);
            break;
        }
        case 3: /* reverse */
            inst_m(in)->path_speed = -in->m->path_speed;
            inst_m(in)->path_position = in->m->path_position >= 1 ? 1 : 0;
            break;
        }
    }
    path_point_at(p, in->m->path_position, &px, &py);
    in->x = in->m->path_xstart + px * in->m->path_scale;
    in->y = in->m->path_ystart + py * in->m->path_scale;
    if (ended)
    {
        inst_m(in)->path_index = -1;
        event_run(in, NULL, EV_OTHER, EV_OTHER_PATH_END);
    }
}

/* ---- text ---- */

/* ASCII glyphs of the last font used, so text doesn't binary-search the
   glyph table in flash for every letter */
/* ASCII glyphs of the last few fonts, looked up as they get used (the
   battle HUD switches fonts many times a frame) */
#define GLYPH_CACHES 2
#define GLYPH_UNSET ((const glyph_rec_t *)1)
static int gc_font[GLYPH_CACHES] = { -1, -1 };
static const glyph_rec_t *gc_glyph[GLYPH_CACHES][128];
static uint8_t gc_next, gc_last;

static const glyph_rec_t *find_glyph_slow(int font, uint32_t ch);

static const glyph_rec_t *find_glyph(int font, uint32_t ch)
{
    if (ch < 128)
    {
        const glyph_rec_t **t;
        int k = gc_last;
        if (gc_font[k] != font)
        {
            for (k = 0; k < GLYPH_CACHES && gc_font[k] != font; k++)
            {
            }
            if (k == GLYPH_CACHES)
            {
                k = gc_next;
                gc_next = (uint8_t)((gc_next + 1) % GLYPH_CACHES);
                gc_font[k] = font;
                for (int c = 0; c < 128; c++)
                {
                    gc_glyph[k][c] = GLYPH_UNSET;
                }
            }
            gc_last = (uint8_t)k;
        }
        t = &gc_glyph[k][ch];
        if (*t == GLYPH_UNSET)
        {
            *t = find_glyph_slow(font, ch);
        }
        return *t;
    }
    return find_glyph_slow(font, ch);
}

static const glyph_rec_t *find_glyph_slow(int font, uint32_t ch)
{
    const font_rec_t *f = FONT(font);
    uint32_t base = f->first_glyph;
    int lo = 0, hi = f->nglyphs;
    while (lo < hi)
    {
        int mid = (lo + hi) / 2;
        if (GLYPH(base + mid)->ch < ch)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    if (lo < f->nglyphs && GLYPH(base + lo)->ch == ch)
    {
        return GLYPH(base + lo);
    }
    return NULL;
}

/* next UTF-8 code point */
static uint32_t next_char(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = *p++;
    if (c >= 0xc0)
    {
        int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
        c &= 0x3f >> extra;
        while (extra-- && (*p & 0xc0) == 0x80)
        {
            c = c << 6 | (*p++ & 0x3f);
        }
    }
    *s = (const char *)p;
    return c;
}

int font_line_height(int font)
{
    static uint8_t heights[64]; /* per font, 0 = not known yet */
    const font_rec_t *f;
    int h = 0;
    if (font < 0 || font >= (int)gd.nfonts)
    {
        return 12;
    }
    if (font < 64 && heights[font])
    {
        return heights[font];
    }
    f = FONT(font);
    for (int i = 0; i < f->nglyphs; i++)
    {
        const glyph_rec_t *g = GLYPH(f->first_glyph + i);
        if (g->ch < 0x3000 && g->h > h)
        {
            h = g->h;
        }
    }
    h = h ? h : 12;
    if (font < 64 && h < 256)
    {
        heights[font] = (uint8_t)h;
    }
    return h;
}

static gmreal_t line_width(int font, const char *s)
{
    gmreal_t w = 0;
    while (*s && *s != '#' && *s != '\n')
    {
        uint32_t c = next_char(&s);
        const glyph_rec_t *g = find_glyph(font, c);
        if (!g)
        {
            g = find_glyph(font, ' ');
        }
        if (g)
        {
            w += g->shift;
        }
    }
    return w;
}

gmreal_t text_width(const char *s)
{
    gmreal_t best = 0;
    int font = gs.draw_font;
    if (font < 0 || font >= (int)gd.nfonts)
    {
        return 0;
    }
    for (;;)
    {
        gmreal_t w = line_width(font, s);
        if (w > best)
        {
            best = w;
        }
        while (*s && *s != '#' && *s != '\n')
        {
            s++;
        }
        if (!*s)
        {
            break;
        }
        s++;
    }
    return best;
}

gmreal_t text_height(const char *s)
{
    int lines = 1;
    for (; *s; s++)
    {
        if (*s == '#' || *s == '\n')
        {
            lines++;
        }
    }
    return (gmreal_t)(lines * font_line_height(gs.draw_font));
}

void draw_text_full(gmreal_t x, gmreal_t y, const char *s, gmreal_t xs, gmreal_t ys, gmreal_t angle,
                    uint32_t color, gmreal_t alpha)
{
    int font = gs.draw_font, lh;
    gmreal_t ly;
    int nlines = 1;
    if (font < 0 || font >= (int)gd.nfonts)
    {
        return;
    }
    trace_use(TRACE_FONT, font);

    lh = font_line_height(font);
    for (const char *p = s; *p; p++)
    {
        if (*p == '#' || *p == '\n')
        {
            nlines++;
        }
    }
    if (render_int && xs == 1 && ys == 1 && angle == 0)
    {
        /* the common case, in whole pixels */
        int bx = ifloor(x + 0.5f) + render_iox, by = ifloor(y + 0.5f) + render_ioy;
        int iy = gs.valign == 1 ? -(nlines * lh) / 2 : gs.valign == 2 ? -nlines * lh : 0;
        for (;;)
        {
            int ix = 0, w = 0;
            if (gs.halign)
            {
                w = (int)line_width(font, s);
                ix = gs.halign == 1 ? -w / 2 : -w;
            }
            while (*s && *s != '#' && *s != '\n')
            {
                uint32_t c = next_char(&s);
                const glyph_rec_t *g = find_glyph(font, c);
                if (!g)
                {
                    g = find_glyph(font, ' ');
                    if (!g)
                    {
                        continue;
                    }
                }
                if (g->w && g->h)
                {
                    far_t bm = rd_far(g->bitmap);
                    if (bm)
                    {
                        render_glyph_screen(bm, g->w, g->h, bx + ix + g->offset, by + iy, color, alpha);
                    }
                }
                ix += g->shift;
            }
            if (!*s)
            {
                return;
            }
            s++;
            iy += lh;
        }
    }
    {
        /* exact scales (like the battle's 640x480 room shown at half size):
           glyph positions in 8.8 fixed point, no float math per glyph */
        /* the scales, remembered by their bits (usually the same each call) */
        static union { gmreal_t f; uint32_t u; } kx, ky, ks, kox, koy;
        static int32_t fx, fy, sc8, ox8, oy8;
        union { gmreal_t f; uint32_t u; } nx, ny, ns, nox, noy;
        nx.f = xs;
        ny.f = ys;
        ns.f = render_scale;
        nox.f = render_ox;
        noy.f = render_oy;
        if (nx.u != kx.u || ny.u != ky.u || ns.u != ks.u)
        {
            kx.u = nx.u;
            ky.u = ny.u;
            ks.u = ns.u;
            sc8 = scale8(render_scale);
            fx = sc8 ? scale8(xs * render_scale) : 0;
            fy = sc8 ? scale8(ys * render_scale) : 0;
        }
        if (nox.u != kox.u || noy.u != koy.u)
        {
            kox.u = nox.u;
            koy.u = noy.u;
            ox8 = fix8(render_ox);
            oy8 = fix8(render_oy);
        }
        if (angle == 0 && fx && fy)
        {
            int32_t bx = (fix8(x) + ox8) * sc8 >> 8;
            int32_t Y = (fix8(y) + oy8) * sc8 >> 8;
            if (gs.valign == 1)
            {
                Y -= (int32_t)nlines * lh * fy / 2;
            }
            else if (gs.valign == 2)
            {
                Y -= (int32_t)nlines * lh * fy;
            }
            for (;;)
            {
                int32_t X = bx;
                if (gs.halign)
                {
                    int32_t w = (int32_t)(line_width(font, s) * fx);
                    X -= gs.halign == 1 ? w / 2 : w;
                }
                while (*s && *s != '#' && *s != '\n')
                {
                    uint32_t c = next_char(&s);
                    const glyph_rec_t *g = find_glyph(font, c);
                    if (!g)
                    {
                        g = find_glyph(font, ' ');
                        if (!g)
                        {
                            continue;
                        }
                    }
                    if (g->w && g->h)
                    {
                        far_t bm = rd_far(g->bitmap);
                        if (bm)
                        {
                            int32_t gx = X + (int32_t)g->offset * fx;
                            int left = (int)((gx + 128) >> 8), top = (int)((Y + 128) >> 8);
                            int dw = (int)((gx + (int32_t)g->w * fx + 128) >> 8) - left;
                            int dh = (int)((Y + (int32_t)g->h * fy + 128) >> 8) - top;
                            render_glyph_px(bm, g->w, g->h, left, top, dw, dh, color, alpha);
                        }
                    }
                    X += (int32_t)g->shift * fx;
                }
                if (!*s)
                {
                    return;
                }
                s++;
                Y += (int32_t)lh * fy;
            }
        }
    }
    ly = 0;
    if (gs.valign == 1)
    {
        ly = -nlines * lh / 2.0f;
    }
    else if (gs.valign == 2)
    {
        ly = (gmreal_t)(-nlines * lh);
    }
    for (;;)
    {
        gmreal_t lx = 0, w = line_width(font, s);
        if (gs.halign == 1)
        {
            lx = -w / 2;
        }
        else if (gs.halign == 2)
        {
            lx = -w;
        }
        while (*s && *s != '#' && *s != '\n')
        {
            uint32_t c = next_char(&s);
            const glyph_rec_t *g = find_glyph(font, c);
            if (!g)
            {
                g = find_glyph(font, ' ');
                if (!g)
                {
                    continue;
                }
            }
            if (g->w && g->h)
            {
                gmreal_t gx = lx + g->offset, gy = ly;
                gmreal_t rx = gx * xs, ry = gy * ys;
                if (angle != 0)
                {
                    gmreal_t c2 = gm_dcos(angle), s2 = gm_dsin(angle);
                    gmreal_t tx = rx * c2 + ry * s2, ty = -rx * s2 + ry * c2;
                    rx = tx;
                    ry = ty;
                }
                far_t bm = rd_far(g->bitmap);
                if (bm)
                {
                    render_glyph(bm, g->w, g->h, x + rx, y + ry, xs, ys, angle, color, alpha);
                }
            }
            lx += g->shift;
        }
        if (!*s)
        {
            break;
        }
        s++;
        ly += lh;
    }
}
