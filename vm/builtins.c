/* GameMaker built-in functions and variables used by Undertale. Audio,
   Steam, gamepads, surfaces and windows are stubs: the calculator has none
   of them. */
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VM_NAMES
#include "vm.h"

#define PI_F 3.14159265f
#define NOONE (-4)

extern void draw_sprite_full(int spr, gmreal_t frame, gmreal_t x, gmreal_t y, gmreal_t xs, gmreal_t ys,
                             gmreal_t angle, uint32_t blend, gmreal_t alpha);
extern void draw_sprite_part_full(int spr, gmreal_t frame, int left, int top, int w, int h,
                                  gmreal_t x, gmreal_t y, gmreal_t xs, gmreal_t ys, uint32_t blend, gmreal_t alpha);
extern void draw_background_full(int bg, int left, int top, int w, int h, gmreal_t x, gmreal_t y,
                                 gmreal_t xs, gmreal_t ys, uint32_t blend, gmreal_t alpha);
extern void draw_self(instance_t *in);
extern void draw_text_full(gmreal_t x, gmreal_t y, const char *s, gmreal_t xs, gmreal_t ys, gmreal_t angle,
                           uint32_t color, gmreal_t alpha);
extern gmreal_t text_width(const char *s);
extern gmreal_t text_height(const char *s);
extern void tile_layer_shift(int depth, int dx, int dy);
extern void tile_layer_visible(int depth, bool visible);
extern void path_begin(instance_t *in, int path, gmreal_t speed, int endaction, bool absolute);
extern bool event_run_as(instance_t *in, instance_t *other, int obj, int type, int sub);
extern bool room_is_persistent(int room);
extern void room_set_persistent(int room, bool p);

static bool dnd_relative;
static int g_charname = -2, g_gold, g_itemname, g_menucoord; /* for scr_gettext */
static uint32_t rng_state = 12345;

/* ---- helpers ---- */

/* argument access: real functions, not macros, to keep the dispatcher
   small on the calculator (it has ~240 cases) */
static int b_argc;
static value_t *b_args;

static NOINLINE gmreal_t arg_r(int i)
{
    return i < b_argc ? v_num(b_args[i]) : 0;
}

static NOINLINE int32_t arg_i(int i)
{
    return i < b_argc ? v_int(b_args[i]) : 0;
}

static NOINLINE const char *arg_s(int i)
{
    return i < b_argc ? v_cstring(b_args[i]) : "";
}

#define ARG_R(i) arg_r(i)
#define ARG_I(i) arg_i(i)
#define ARG_S(i) arg_s(i)

static NOINLINE gmreal_t frand(void)
{
    union
    {
        gmreal_t f;
        uint32_t u;
    } r;
    rng_state = rng_state * 1103515245u + 12345u;
    /* (bits 8-31) / 2^24, exactly: the float of the integer with 24
       taken off its exponent (a division costs ~8000 cycles on the CE) */
    r.f = (gmreal_t)((rng_state >> 8) & 0xffffff);
    if (r.u)
    {
        r.u -= (uint32_t)24 << 23;
    }
    return r.f;
}

/* random(range) */
gmreal_t vm_random(gmreal_t range)
{
    gmreal_t r = frand();
    return range == 0 ? 0 : r * range;
}

static NOINLINE value_t id_or_noone(instance_t *in)
{
    return v_real(in ? (gmreal_t)in->id : NOONE);
}

static NOINLINE void set_speed_dir(instance_t *in, gmreal_t speed, gmreal_t dir)
{
    inst_m(in)->speed = speed;
    inst_m(in)->direction = fmodf(dir, 360);
    if (in->m->direction < 0)
    {
        inst_m(in)->direction += 360;
    }
    inst_m(in)->hspeed = speed * gm_dcos(in->m->direction);
    inst_m(in)->vspeed = -speed * gm_dsin(in->m->direction);
}

static NOINLINE void sync_from_hv(instance_t *in)
{
    inst_m(in)->speed = sqrtf(in->m->hspeed * in->m->hspeed + in->m->vspeed * in->m->vspeed);
    if (in->m->hspeed != 0 || in->m->vspeed != 0)
    {
        gmreal_t d = gm_datan2(-in->m->vspeed, in->m->hspeed);
        inst_m(in)->direction = d < 0 ? d + 360 : d;
    }
}

/* UTF-8 helpers: byte offset of code point n (0-based), code point count */
static NOINLINE int utf8_offset(const char *s, int n)
{
    int i = 0;
    while (s[i] && n > 0)
    {
        i++;
        while ((s[i] & 0xc0) == 0x80)
        {
            i++;
        }
        n--;
    }
    return i;
}

static NOINLINE int utf8_len(const char *s)
{
    int n = 0;
    for (; *s; s++)
    {
        if ((*s & 0xc0) != 0x80)
        {
            n++;
        }
    }
    return n;
}

static NOINLINE uint32_t utf8_first(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
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
    return c;
}

static NOINLINE int utf8_encode(uint32_t c, char *out)
{
    if (c < 0x80) { out[0] = (char)c; return 1; }
    if (c < 0x800) { out[0] = (char)(0xc0 | c >> 6); out[1] = (char)(0x80 | (c & 0x3f)); return 2; }
    if (c < 0x10000)
    {
        out[0] = (char)(0xe0 | c >> 12);
        out[1] = (char)(0x80 | ((c >> 6) & 0x3f));
        out[2] = (char)(0x80 | (c & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | c >> 18);
    out[1] = (char)(0x80 | ((c >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((c >> 6) & 0x3f));
    out[3] = (char)(0x80 | (c & 0x3f));
    return 4;
}

static NOINLINE uint32_t merge_colors(uint32_t a, uint32_t b, gmreal_t t)
{
    int r = (int)(GM_R(a) + (GM_R(b) - (int)GM_R(a)) * t);
    int g = (int)(GM_G(a) + (GM_G(b) - (int)GM_G(a)) * t);
    int bl = (int)(GM_B(a) + (GM_B(b) - (int)GM_B(a)) * t);
    return (uint32_t)r | (uint32_t)g << 8 | (uint32_t)bl << 16;
}

static NOINLINE uint32_t hsv(gmreal_t h, gmreal_t s, gmreal_t v)
{
    gmreal_t hh = h / 255 * 6, ss = s / 255, vv = v / 255, r, g, b;
    int i = (int)floorf(hh) % 6;
    gmreal_t f = hh - floorf(hh), p = vv * (1 - ss), q = vv * (1 - ss * f), t = vv * (1 - ss * (1 - f));
    switch (i)
    {
    case 0: r = vv; g = t; b = p; break;
    case 1: r = q; g = vv; b = p; break;
    case 2: r = p; g = vv; b = t; break;
    case 3: r = p; g = q; b = vv; break;
    case 4: r = t; g = p; b = vv; break;
    default: r = vv; g = p; b = q; break;
    }
    return (uint32_t)(int32_t)(r * 255) | (uint32_t)(int32_t)(g * 255) << 8 | (uint32_t)(int32_t)(b * 255) << 16;
}

/* ---- collision queries ---- */

static bool query_match(instance_t *in, int obj, bool notme)
{
    if (!in->alive || (notme && in == vm_self))
    {
        return false;
    }
    if (obj == -3)
    {
        return true;
    }
    if (obj >= 100000)
    {
        return in->id == obj;
    }
    return inst_is_a(in, obj);
}

static NOINLINE instance_t *query_rect(gmreal_t x1, gmreal_t y1, gmreal_t x2, gmreal_t y2, int obj, bool prec, bool notme)
{
    for (int i = 0; i < ninstances; i++)
    {
        instance_t *in = instances[i];
        if (query_match(in, obj, notme) && inst_rect(in, x1, y1, x2, y2, prec))
        {
            return in;
        }
    }
    return NULL;
}

static NOINLINE instance_t *query_point(gmreal_t x, gmreal_t y, int obj, bool prec, bool notme)
{
    for (int i = 0; i < ninstances; i++)
    {
        instance_t *in = instances[i];
        if (query_match(in, obj, notme) && inst_point(in, x, y, prec))
        {
            return in;
        }
    }
    return NULL;
}

static NOINLINE instance_t *query_line(gmreal_t x1, gmreal_t y1, gmreal_t x2, gmreal_t y2, int obj, bool prec, bool notme)
{
    gmreal_t dx = x2 - x1, dy = y2 - y1;
    int steps = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy)) + 1;
    for (int i = 0; i < ninstances; i++)
    {
        instance_t *in = instances[i];
        gmreal_t l, t, r, b;
        if (!query_match(in, obj, notme) || !inst_bbox(in, &l, &t, &r, &b))
        {
            continue;
        }
        if ((x1 < l && x2 < l) || (x1 > r + 1 && x2 > r + 1) || (y1 < t && y2 < t) || (y1 > b + 1 && y2 > b + 1))
        {
            continue;
        }
        for (int s = 0; s <= steps; s++)
        {
            gmreal_t f = steps ? (gmreal_t)s / steps : 0;
            if (inst_point(in, x1 + dx * f, y1 + dy * f, prec))
            {
                return in;
            }
        }
    }
    return NULL;
}

static NOINLINE instance_t *query_circle(gmreal_t cx, gmreal_t cy, gmreal_t rad, int obj, bool prec, bool notme)
{
    for (int i = 0; i < ninstances; i++)
    {
        instance_t *in = instances[i];
        gmreal_t l, t, r, b, nx, ny;
        if (!query_match(in, obj, notme) || !inst_bbox(in, &l, &t, &r, &b))
        {
            continue;
        }
        nx = cx < l ? l : cx > r ? r : cx;
        ny = cy < t ? t : cy > b ? b : cy;
        if ((nx - cx) * (nx - cx) + (ny - cy) * (ny - cy) <= rad * rad)
        {
            return in;
        }
    }
    (void)prec;
    return NULL;
}

static NOINLINE gmreal_t bbox_distance(instance_t *a, gmreal_t px, gmreal_t py, instance_t *b)
{
    gmreal_t al, at, ar, ab, bl, bt, br, bb, dx, dy;
    if (!inst_bbox(a, &al, &at, &ar, &ab))
    {
        al = ar = a->x;
        at = ab = a->y;
    }
    if (b)
    {
        if (!inst_bbox(b, &bl, &bt, &br, &bb))
        {
            bl = br = b->x;
            bt = bb = b->y;
        }
    }
    else
    {
        bl = br = px;
        bt = bb = py;
    }
    dx = bl > ar ? bl - ar : al > br ? al - br : 0;
    dy = bt > ab ? bt - ab : at > bb ? at - bb : 0;
    return sqrtf(dx * dx + dy * dy);
}

/* ---- ds_map ---- */

typedef struct
{
    value_t key, val;
} map_entry_t;

typedef struct
{
    bool used;
    int n, cap; /* cap is a power of two */
    map_entry_t *e;
} dsmap_t;

static dsmap_t *maps;
static int nmaps;

static uint32_t key_hash(value_t k)
{
    uint32_t h = 2166136261u;
    if (v_is_str(k))
    {
        for (const char *s = v_cstring(k); *s; s++)
        {
            h = (h ^ (uint8_t)*s) * 16777619u;
        }
    }
    else
    {
        gmreal_t r = v_num(k);
        uint32_t bits;
        memcpy(&bits, &r, 4);
        h = (h ^ bits) * 16777619u;
    }
    return h;
}

static bool key_eq(value_t a, value_t b)
{
    if (v_is_str(a) != v_is_str(b))
    {
        return false;
    }
    return v_equal(a, b);
}

static dsmap_t *get_map(int id)
{
    if (id < 0 || id >= nmaps || !maps[id].used)
    {
        return NULL;
    }
    return &maps[id];
}

static NOINLINE map_entry_t *map_slot(dsmap_t *m, value_t key, bool *found)
{
    uint32_t h;
    if (!m->cap)
    {
        *found = false;
        return NULL;
    }
    h = key_hash(key) & (m->cap - 1);
    for (;;)
    {
        map_entry_t *e = &m->e[h];
        if (e->key.t == VT_UNDEF && e->val.t != VT_REAL)
        {
            *found = false;
            return e;
        }
        if (e->key.t != VT_UNDEF && key_eq(e->key, key))
        {
            *found = true;
            return e;
        }
        h = (h + 1) & (m->cap - 1);
    }
}

static void map_grow(dsmap_t *m)
{
    map_entry_t *old = m->e;
    int oldcap = m->cap;
    int cap = oldcap ? oldcap * 2 : 16;
    m->e = malloc(sizeof(map_entry_t) * cap);
    if (!m->e)
    {
        m->e = old;
        vm_error("out of memory (ds_map)");
        return;
    }
    for (int i = 0; i < cap; i++)
    {
        m->e[i].key = v_undef();
        m->e[i].val = v_undef();
    }
    m->cap = cap;
    m->n = 0;
    for (int i = 0; i < oldcap; i++)
    {
        if (old[i].key.t != VT_UNDEF)
        {
            bool found;
            map_entry_t *e = map_slot(m, old[i].key, &found);
            *e = old[i];
            m->n++;
        }
    }
    free(old);
}

static NOINLINE void map_put(dsmap_t *m, value_t key, value_t val, bool replace)
{
    bool found;
    map_entry_t *e;
    if ((m->n + 1) * 4 >= m->cap * 3)
    {
        map_grow(m);
    }
    e = map_slot(m, key, &found);
    if (!e)
    {
        return;
    }
    if (found)
    {
        if (!replace)
        {
            return;
        }
        v_release(e->val);
        v_retain(val);
        e->val = val;
        return;
    }
    if (key.t == VT_CSTR || key.t == VT_REAL)
    {
        e->key = key;
    }
    else
    {
        e->key = v_str(v_cstring(key), -1);
    }
    v_retain(val);
    e->val = val;
    m->n++;
}

static NOINLINE int map_create(void)
{
    for (int i = 0; i < nmaps; i++)
    {
        if (!maps[i].used)
        {
            memset(&maps[i], 0, sizeof(dsmap_t));
            maps[i].used = true;
            return i;
        }
    }
    {
        dsmap_t *m = realloc(maps, sizeof(dsmap_t) * (nmaps + 1));
        if (!m)
        {
            return -1;
        }
        maps = m;
        memset(&maps[nmaps], 0, sizeof(dsmap_t));
        maps[nmaps].used = true;
        return nmaps++;
    }
}

static NOINLINE void map_destroy(int id)
{
    dsmap_t *m = get_map(id);
    if (!m)
    {
        return;
    }
    for (int i = 0; i < m->cap; i++)
    {
        v_release(m->e[i].key);
        v_release(m->e[i].val);
    }
    free(m->e);
    memset(m, 0, sizeof *m);
}

static NOINLINE void cat(char *out, size_t *o, const char *a, const char *b, const char *c, const char *d);

/* ---- ini files ---- */

typedef struct
{
    char *name;
    char *text; /* whole file, "[sec]\nkey=value\n..." */
    bool dirty;
    bool open;
} ini_t;

static ini_t ini;

static NOINLINE void ini_close_file(void)
{
    if (ini.open && ini.dirty && ini.name)
    {
        plat_file_write(ini.name, ini.text, (int)strlen(ini.text));
    }
    free(ini.name);
    free(ini.text);
    memset(&ini, 0, sizeof ini);
}

static NOINLINE void ini_open_file(const char *name)
{
    char *data;
    int len;
    ini_close_file();
    ini.name = strdup(name);
    ini.open = true;
    if (plat_file_read(name, &data, &len))
    {
        ini.text = malloc(len + 1);
        if (ini.text)
        {
            memcpy(ini.text, data, len);
            ini.text[len] = 0;
        }
        free(data);
    }
    if (!ini.text)
    {
        ini.text = strdup("");
    }
}

/* find the value of sec/key; returns pointer into ini.text and its length */
static NOINLINE const char *ini_find(const char *sec, const char *key, int *vlen, bool *has_sec)
{
    const char *p = ini.text;
    bool in_sec = false;
    size_t klen = strlen(key);
    if (has_sec)
    {
        *has_sec = false;
    }
    while (p && *p)
    {
        const char *eol = strchr(p, '\n');
        int len = eol ? (int)(eol - p) : (int)strlen(p);
        if (len > 0 && p[len - 1] == '\r')
        {
            len--;
        }
        if (len > 0 && p[0] == '[')
        {
            const char *close = memchr(p, ']', len);
            in_sec = close && (size_t)(close - p - 1) == strlen(sec) && !strncmp(p + 1, sec, close - p - 1);
            if (in_sec && has_sec)
            {
                *has_sec = true;
            }
        }
        else if (in_sec && (size_t)len > klen && !strncmp(p, key, klen) && p[klen] == '=')
        {
            const char *v = p + klen + 1;
            int l = len - (int)klen - 1;
            if (l >= 2 && v[0] == '"' && v[l - 1] == '"')
            {
                v++;
                l -= 2;
            }
            *vlen = l;
            return v;
        }
        p = eol ? eol + 1 : NULL;
    }
    return NULL;
}

static NOINLINE void ini_write(const char *sec, const char *key, const char *val)
{
    /* rebuild the text with the key replaced or added */
    size_t cap = strlen(ini.text) + strlen(sec) + strlen(key) + strlen(val) + 16;
    char *out = malloc(cap);
    const char *p = ini.text;
    bool in_sec = false, done = false, seen_sec = false;
    size_t o = 0, klen = strlen(key);
    if (!out)
    {
        return;
    }
    while (p && *p)
    {
        const char *eol = strchr(p, '\n');
        int len = eol ? (int)(eol - p) : (int)strlen(p);
        int tl = len;
        if (tl > 0 && p[tl - 1] == '\r')
        {
            tl--;
        }
        if (tl > 0 && p[0] == '[')
        {
            const char *close = memchr(p, ']', tl);
            if (in_sec && !done)
            {
                cat(out, &o, key, "=\"", val, "\"\n");
                done = true;
            }
            in_sec = close && (size_t)(close - p - 1) == strlen(sec) && !strncmp(p + 1, sec, close - p - 1);
            if (in_sec)
            {
                seen_sec = true;
            }
        }
        else if (in_sec && !done && (size_t)tl > klen && !strncmp(p, key, klen) && p[klen] == '=')
        {
            cat(out, &o, key, "=\"", val, "\"\n");
            done = true;
            p = eol ? eol + 1 : NULL;
            continue;
        }
        memcpy(out + o, p, len);
        o += len;
        out[o++] = '\n';
        p = eol ? eol + 1 : NULL;
    }
    if (!done)
    {
        if (!seen_sec)
        {
            cat(out, &o, "[", sec, "]\n", NULL);
        }
        cat(out, &o, key, "=\"", val, "\"\n");
    }
    out[o] = 0;
    free(ini.text);
    ini.text = out;
    ini.dirty = true;
}

/* append strings to out at *o */
static NOINLINE void cat(char *out, size_t *o, const char *a, const char *b, const char *c, const char *d)
{
    const char *parts[4] = { a, b, c, d };
    for (int i = 0; i < 4; i++)
    {
        if (parts[i])
        {
            size_t l = strlen(parts[i]);
            memcpy(out + *o, parts[i], l);
            *o += l;
        }
    }
}

/* ---- text files ---- */

#define MAX_FILES 4

typedef struct
{
    bool used, writing;
    char *name;
    char *buf;
    int len, pos, cap;
} tfile_t;

static tfile_t files[MAX_FILES];

static NOINLINE int tfile_open(const char *name, bool writing)
{
    for (int i = 0; i < MAX_FILES; i++)
    {
        tfile_t *f = &files[i];
        if (f->used)
        {
            continue;
        }
        memset(f, 0, sizeof *f);
        f->used = true;
        f->writing = writing;
        f->name = strdup(name);
        if (!writing)
        {
            if (!plat_file_read(name, &f->buf, &f->len))
            {
                f->buf = NULL;
                f->len = 0;
            }
        }
        return i;
    }
    return -1;
}

static tfile_t *tfile(int id)
{
    return id >= 0 && id < MAX_FILES && files[id].used ? &files[id] : NULL;
}

static NOINLINE void tfile_append(tfile_t *f, const char *s)
{
    int l = (int)strlen(s);
    if (f->len + l + 1 > f->cap)
    {
        int cap = (f->len + l + 1) * 2;
        char *b = realloc(f->buf, cap);
        if (!b)
        {
            return;
        }
        f->buf = b;
        f->cap = cap;
    }
    memcpy(f->buf + f->len, s, l);
    f->len += l;
}

static NOINLINE void tfile_close(int id)
{
    tfile_t *f = tfile(id);
    if (!f)
    {
        return;
    }
    if (f->writing)
    {
        plat_file_write(f->name, f->buf ? f->buf : "", f->len);
    }
    free(f->buf);
    free(f->name);
    memset(f, 0, sizeof *f);
}

/* rest of the current line */
static NOINLINE value_t tfile_read_string(tfile_t *f)
{
    int start = f->pos, end = f->pos;
    while (end < f->len && f->buf[end] != '\n' && f->buf[end] != '\r')
    {
        end++;
    }
    f->pos = end;
    return v_str(f->buf ? f->buf + start : "", end - start);
}

static NOINLINE void tfile_readln(tfile_t *f)
{
    while (f->pos < f->len && f->buf[f->pos] != '\n')
    {
        f->pos++;
    }
    if (f->pos < f->len)
    {
        f->pos++;
    }
}

/* ---- deactivated instances ---- */

static instance_t **inactive;
static int ninactive;

static NOINLINE void deactivate_all(bool notme)
{
    int j = 0;
    instance_t **l = realloc(inactive, sizeof(instance_t *) * (ninactive + ninstances + 1));
    if (!l)
    {
        return;
    }
    inactive = l;
    for (int i = 0; i < ninstances; i++)
    {
        if (notme && instances[i] == vm_self)
        {
            instances[j++] = instances[i];
        }
        else if (instances[i]->alive)
        {
            inactive[ninactive++] = instances[i];
        }
        else
        {
            instances[j++] = instances[i];
        }
    }
    ninstances = j;
}

extern void inst_reinsert(instance_t *in);

static NOINLINE void activate(int obj)
{
    int j = 0;
    for (int i = 0; i < ninactive; i++)
    {
        instance_t *in = inactive[i];
        if (obj == -3 || inst_is_a(in, obj))
        {
            inst_reinsert(in);
        }
        else
        {
            inactive[j++] = in;
        }
    }
    ninactive = j;
}

void builtins_reset(void)
{
    for (int i = 0; i < nmaps; i++)
    {
        map_destroy(i);
    }
    free(maps);
    maps = NULL;
    nmaps = 0;
    ini_close_file();
    for (int i = 0; i < MAX_FILES; i++)
    {
        tfile_close(i);
    }
    ninactive = 0;
    dnd_relative = false;
    g_charname = -2;
}

/* ---- rooms ---- */

static int room_order_pos(int room)
{
    for (uint32_t i = 0; i < gd.nroomorder; i++)
    {
        if (room_order((int)i) == room)
        {
            return (int)i;
        }
    }
    return -1;
}

static NOINLINE int room_next_of(int room)
{
    int p = room_order_pos(room);
    return p >= 0 && p + 1 < (int)gd.nroomorder ? room_order(p + 1) : -1;
}

static NOINLINE int room_prev_of(int room)
{
    int p = room_order_pos(room);
    return p > 0 ? room_order(p - 1) : -1;
}

/* ---- scr_gettext ---- */

/* The rest of scr_gettext: args[0] is the text (from the text table);
   \[C] (name), \[G] (gold), \[I] (selected item) and \[1]-\[9]
   (arguments) get filled in. */
value_t native_gettext(int argc, value_t *args)
{
    const char *t = argc > 0 ? v_cstring(args[0]) : "";
    const char *p;
    char *buf;
    int cap, len = 0;
    value_t r;

    if (!strstr(t, "\\["))
    {
        if (argc > 0)
        {
            v_retain(args[0]);
            return args[0];
        }
        return v_cstr("");
    }
    if (g_charname == -2)
    {
        g_charname = data_find_global("charname");
        g_gold = data_find_global("gold");
        g_itemname = data_find_global("itemname");
        g_menucoord = data_find_global("menucoord");
    }
    cap = (int)strlen(t) + 64;
    buf = malloc(cap);
    if (!buf)
    {
        return v_cstr(t);
    }
    for (p = t; *p;)
    {
        if (p[0] == '\\' && p[1] == '[' && p[2] && p[3] == ']')
        {
            value_t rep = v_cstr("");
            bool owned = false;
            char sel = p[2];
            const char *rs;
            int rl;
            if (sel == 'C' && g_charname >= 0)
            {
                rep = globals[g_charname];
            }
            else if (sel == 'G' && g_gold >= 0)
            {
                rep = v_tostring(globals[g_gold]);
                owned = true;
            }
            else if (sel == 'I' && g_itemname >= 0 && g_menucoord >= 0)
            {
                rep = arr_get(globals[g_itemname], v_int(arr_get(globals[g_menucoord], 1)));
            }
            else if (sel >= '1' && sel <= '9' && argc > sel - '0')
            {
                rep = v_tostring(args[sel - '0']);
                owned = true;
            }
            rs = v_cstring(rep);
            rl = (int)strlen(rs);
            if (len + rl + 1 > cap)
            {
                char *nb;
                cap = len + rl + 64;
                nb = realloc(buf, cap);
                if (!nb)
                {
                    break;
                }
                buf = nb;
            }
            memcpy(buf + len, rs, rl);
            len += rl;
            if (owned)
            {
                v_release(rep);
            }
            p += 4;
            continue;
        }
        if (len + 2 > cap)
        {
            char *nb;
            cap += 64;
            nb = realloc(buf, cap);
            if (!nb)
            {
                break;
            }
            buf = nb;
        }
        buf[len++] = *p++;
    }
    r = v_str(buf, len);
    free(buf);
    return r;
}

/* ---- the dispatcher ---- */

static value_t builtin_dispatch(int f, int argc, value_t *args, instance_t *self);

value_t builtin_call(int f, int argc, value_t *args)
{
    instance_t *self = vm_self;
    int saved_argc = b_argc;
    value_t *saved_args = b_args;
    value_t r;
    b_argc = argc;
    b_args = args;
    r = builtin_dispatch(f, argc, args, self);
    b_argc = saved_argc;
    b_args = saved_args;
    return r;
}

/* ---- the built-in functions, one each ---- */

/* instances */
static value_t bi_instance_create(int f, int argc, value_t *args, instance_t *self)
{
    instance_t *in = inst_create(ARG_I(2), ARG_R(0), ARG_R(1));
    return id_or_noone(in);
}

static value_t bi_action_create_object(int f, int argc, value_t *args, instance_t *self)
{
    gmreal_t x = ARG_R(1), y = ARG_R(2);
    instance_t *in;
    if (dnd_relative && self)
    {
        x += self->x;
        y += self->y;
    }
    in = inst_create(ARG_I(0), x, y);
    return id_or_noone(in);
}

static value_t bi_instance_destroy(int f, int argc, value_t *args, instance_t *self)
{
        inst_destroy(self, true);
        return v_real(0);
}

static value_t bi_instance_exists(int f, int argc, value_t *args, instance_t *self)
{
    instance_t *l[1];
    return v_real(inst_select(ARG_I(0), l, 1) > 0 ? 1 : 0);
}

static value_t bi_instance_number(int f, int argc, value_t *args, instance_t *self)
{
    int n = 0;
    int obj = ARG_I(0);
    for (int i = 0; i < ninstances; i++)
    {
        if (instances[i]->alive && (obj == -3 || inst_is_a(instances[i], obj)))
        {
            n++;
        }
    }
    return v_real((gmreal_t)n);
}

static value_t bi_instance_find(int f, int argc, value_t *args, instance_t *self)
{
    int obj = ARG_I(0), k = ARG_I(1);
    for (int i = 0; i < ninstances; i++)
    {
        if (instances[i]->alive && (obj == -3 || inst_is_a(instances[i], obj)) && k-- == 0)
        {
            return v_real((gmreal_t)instances[i]->id);
        }
    }
    return v_real(NOONE);
}

static value_t bi_instance_position(int f, int argc, value_t *args, instance_t *self)
{
        return id_or_noone(query_point(ARG_R(0), ARG_R(1), ARG_I(2), true, false));
}

static value_t bi_instance_change(int f, int argc, value_t *args, instance_t *self)
{
        if (self)
        {
            int obj = ARG_I(0);
            if (ARG_I(1))
            {
                event_run(self, NULL, EV_DESTROY, 0);
            }
            self->obj = (int16_t)obj;
            self->sprite_index = OBJ(obj)->sprite;
            self->mask_index = OBJ(obj)->mask;
            if (ARG_I(1))
            {
                event_run(self, NULL, EV_CREATE, 0);
            }
        }
        return v_real(0);
}

static value_t bi_instance_deactivate_all(int f, int argc, value_t *args, instance_t *self)
{
        deactivate_all(ARG_I(0) != 0);
        return v_real(0);
}

static value_t bi_instance_activate_all(int f, int argc, value_t *args, instance_t *self)
{
        activate(-3);
        return v_real(0);
}

static value_t bi_instance_activate_object(int f, int argc, value_t *args, instance_t *self)
{
        activate(ARG_I(0));
        return v_real(0);

    /* events and scripts */
}

static value_t bi_event_user(int f, int argc, value_t *args, instance_t *self)
{
        if (self)
        {
            event_run_as(self, vm_other, self->obj, EV_OTHER, EV_OTHER_USER0 + ARG_I(0));
        }
        return v_real(0);
}

static value_t bi_event_perform(int f, int argc, value_t *args, instance_t *self)
{
        if (self)
        {
            event_run_as(self, vm_other, self->obj, ARG_I(0), ARG_I(1));
        }
        return v_real(0);
}

static value_t bi_event_inherited(int f, int argc, value_t *args, instance_t *self)
{
        if (self && vm_ev_obj >= 0 && OBJ(vm_ev_obj)->parent >= 0)
        {
            event_run_as(self, vm_other, OBJ(vm_ev_obj)->parent, vm_ev_type, vm_ev_sub);
        }
        return v_real(0);
}

static value_t bi_script_execute(int f, int argc, value_t *args, instance_t *self)
{
        if (argc < 1)
        {
            return v_real(0);
        }
        return vm_call_script(ARG_I(0), self, vm_other, argc - 1, args + 1);

    /* math */
}

static value_t bi_random(int f, int argc, value_t *args, instance_t *self)
{
        return v_real(vm_random(ARG_R(0)));
}

static value_t bi_irandom(int f, int argc, value_t *args, instance_t *self)
{
        return v_real(floorf(frand() * (ARG_I(0) + 1)));
}

static value_t bi_choose(int f, int argc, value_t *args, instance_t *self)
{
    value_t v;
    if (argc == 0)
    {
        return v_real(0);
    }
    v = args[(int)(frand() * argc) % argc];
    v_retain(v);
    return v;
}

static value_t bi_randomize(int f, int argc, value_t *args, instance_t *self)
{
        rng_state = plat_time_ms();
        return v_real(0);
}

static value_t bi_floor(int f, int argc, value_t *args, instance_t *self)
{
    return v_real(floorf(ARG_R(0)));
}

static value_t bi_ceil(int f, int argc, value_t *args, instance_t *self)
{
    return v_real(ceilf(ARG_R(0)));
}

static value_t bi_round(int f, int argc, value_t *args, instance_t *self)
{
    return v_real(rintf(ARG_R(0))); /* ce/src/fastmath.c on the calculator */
}

static value_t bi_abs(int f, int argc, value_t *args, instance_t *self)
{
    return v_real(fabsf(ARG_R(0)));
}

static value_t bi_sin(int f, int argc, value_t *args, instance_t *self)
{
    return v_real(gm_sin(ARG_R(0)));
}

static value_t bi_cos(int f, int argc, value_t *args, instance_t *self)
{
    return v_real(gm_cos(ARG_R(0)));
}

static value_t bi_sqrt(int f, int argc, value_t *args, instance_t *self)
{
    return v_real(sqrtf(ARG_R(0)));
}

static value_t bi_sqr(int f, int argc, value_t *args, instance_t *self)
{
    return v_real(ARG_R(0) * ARG_R(0));
}

static value_t bi_power(int f, int argc, value_t *args, instance_t *self)
{
    return v_real(powf(ARG_R(0), ARG_R(1)));
}

static value_t bi_degtorad(int f, int argc, value_t *args, instance_t *self)
{
    return v_real(ARG_R(0) * PI_F / 180);
}

static value_t bi_darctan2(int f, int argc, value_t *args, instance_t *self)
{
    return v_real(gm_datan2(ARG_R(0), ARG_R(1)));
}

static value_t bi_min(int f, int argc, value_t *args, instance_t *self)
{
    gmreal_t r = ARG_R(0);
    for (int i = 1; i < argc; i++)
    {
        gmreal_t x = v_num(args[i]);
        if (f == F_min ? x < r : x > r)
        {
            r = x;
        }
    }
    return v_real(r);
}

static value_t bi_lengthdir_x(int f, int argc, value_t *args, instance_t *self)
{
    return v_real(ARG_R(0) * gm_dcos(ARG_R(1)));
}

static value_t bi_lengthdir_y(int f, int argc, value_t *args, instance_t *self)
{
    return v_real(-ARG_R(0) * gm_dsin(ARG_R(1)));
}

static value_t bi_point_direction(int f, int argc, value_t *args, instance_t *self)
{
    gmreal_t d = gm_datan2(-(ARG_R(3) - ARG_R(1)), ARG_R(2) - ARG_R(0));
    return v_real(d < 0 ? d + 360 : d);
}

static value_t bi_point_distance(int f, int argc, value_t *args, instance_t *self)
{
    gmreal_t dx = ARG_R(2) - ARG_R(0), dy = ARG_R(3) - ARG_R(1);
    return v_real(sqrtf(dx * dx + dy * dy));
}

static value_t bi_distance_to_point(int f, int argc, value_t *args, instance_t *self)
{
        return v_real(self ? bbox_distance(self, ARG_R(0), ARG_R(1), NULL) : 0);
}

static value_t bi_distance_to_object(int f, int argc, value_t *args, instance_t *self)
{
    gmreal_t best = 1000000;
    int obj = ARG_I(0);
    if (!self)
    {
        return v_real(best);
    }
    for (int i = 0; i < ninstances; i++)
    {
        if (instances[i] != self && instances[i]->alive && (obj == -3 || inst_is_a(instances[i], obj)))
        {
            gmreal_t d = bbox_distance(self, 0, 0, instances[i]);
            if (d < best)
            {
                best = d;
            }
        }
    }
    return v_real(best);
}

static value_t bi_move_towards_point(int f, int argc, value_t *args, instance_t *self)
{
        if (self)
        {
            gmreal_t d = gm_datan2(-(ARG_R(1) - self->y), ARG_R(0) - self->x);
            set_speed_dir(self, ARG_R(2), d < 0 ? d + 360 : d);
        }
        return v_real(0);
}

static value_t bi_move_snap(int f, int argc, value_t *args, instance_t *self)
{
        if (self)
        {
            gmreal_t hs = ARG_R(0), vs = ARG_R(1);
            if (hs > 0) self->x = roundf(self->x / hs) * hs;
            if (vs > 0) self->y = roundf(self->y / vs) * vs;
        }
        return v_real(0);
}

static value_t bi_is_undefined(int f, int argc, value_t *args, instance_t *self)
{
        return v_real(argc > 0 && args[0].t == VT_UNDEF ? 1 : 0);
}

static value_t bi_real(int f, int argc, value_t *args, instance_t *self)
{
        if (argc > 0 && v_is_str(args[0]))
        {
            return v_real((gmreal_t)atof(v_cstring(args[0])));
        }
        return v_real(ARG_R(0));
}

static value_t bi_string(int f, int argc, value_t *args, instance_t *self)
{
        return argc > 0 ? v_tostring(args[0]) : v_cstr("");

    /* strings */
}

static value_t bi_string_length(int f, int argc, value_t *args, instance_t *self)
{
    return v_real((gmreal_t)utf8_len(ARG_S(0)));
}

static value_t bi_string_byte_length(int f, int argc, value_t *args, instance_t *self)
{
    return v_real((gmreal_t)strlen(ARG_S(0)));
}

static value_t bi_string_char_at(int f, int argc, value_t *args, instance_t *self)
{
    const char *s = ARG_S(0);
    int i = ARG_I(1) - 1, a, b;
    if (i < 0)
    {
        i = 0;
    }
    a = utf8_offset(s, i);
    b = utf8_offset(s, i + 1);
    if (b - a == 1 && (unsigned char)s[a] < 128)
    {
        /* one ASCII character (text writers, a letter at a time):
           a constant string, no allocation */
        static char one[128][2];
        char *o = one[(unsigned char)s[a]];
        o[0] = s[a];
        return v_cstr(o);
    }
    return v_str(s + a, b - a);
}

static value_t bi_ord(int f, int argc, value_t *args, instance_t *self)
{
    return v_real((gmreal_t)utf8_first(ARG_S(0)));
}

static value_t bi_chr(int f, int argc, value_t *args, instance_t *self)
{
    char buf[5];
    int n = utf8_encode((uint32_t)ARG_I(0), buf);
    return v_str(buf, n);
}

static value_t bi_string_pos(int f, int argc, value_t *args, instance_t *self)
{
    const char *sub = ARG_S(0), *s = ARG_S(1);
    const char *p = *sub ? strstr(s, sub) : NULL;
    if (!p)
    {
        return v_real(0);
    }
    {
        int n = 1;
        for (const char *q = s; q < p; q++)
        {
            if ((*q & 0xc0) != 0x80)
            {
                n++;
            }
        }
        return v_real((gmreal_t)n);
    }
}

static value_t bi_string_copy(int f, int argc, value_t *args, instance_t *self)
{
    const char *s = ARG_S(0);
    int i = ARG_I(1) - 1, n = ARG_I(2), a, b;
    if (i < 0)
    {
        n += i;
        i = 0;
    }
    if (n < 0)
    {
        n = 0;
    }
    a = utf8_offset(s, i);
    b = utf8_offset(s, i + n);
    return v_str(s + a, b - a);
}

static value_t bi_string_delete(int f, int argc, value_t *args, instance_t *self)
{
    const char *s = ARG_S(0);
    int i = ARG_I(1) - 1, n = ARG_I(2), a, b, len;
    char *buf;
    value_t r;
    if (i < 0)
    {
        i = 0;
    }
    a = utf8_offset(s, i);
    b = utf8_offset(s, i + (n > 0 ? n : 0));
    len = (int)strlen(s);
    buf = malloc(len + 1);
    if (!buf)
    {
        return v_cstr("");
    }
    memcpy(buf, s, a);
    memcpy(buf + a, s + b, len - b);
    r = v_str(buf, a + len - b);
    free(buf);
    return r;
}

static value_t bi_string_replace_all(int f, int argc, value_t *args, instance_t *self)
{
    const char *s = ARG_S(0), *sub = ARG_S(1), *rep = ARG_S(2);
    size_t sl = strlen(sub), rl = strlen(rep), cap = strlen(s) + 1;
    char *out, *o;
    value_t r;
    if (!sl)
    {
        return v_str(s, -1);
    }
    for (const char *p = s; (p = strstr(p, sub)); p += sl)
    {
        cap += rl;
    }
    out = malloc(cap);
    if (!out)
    {
        return v_cstr("");
    }
    o = out;
    while (*s)
    {
        if (!strncmp(s, sub, sl))
        {
            memcpy(o, rep, rl);
            o += rl;
            s += sl;
        }
        else
        {
            *o++ = *s++;
        }
    }
    r = v_str(out, (int)(o - out));
    free(out);
    return r;
}

static value_t bi_string_lower(int f, int argc, value_t *args, instance_t *self)
{
    value_t r = v_str(ARG_S(0), -1);
    if (r.t == VT_HSTR)
    {
        for (char *p = r.u.hs->s; *p; p++)
        {
            *p = (char)(f == F_string_lower ? tolower((unsigned char)*p) : toupper((unsigned char)*p));
        }
    }
    return r;
}

static value_t bi_string_repeat(int f, int argc, value_t *args, instance_t *self)
{
    const char *s = ARG_S(0);
    int n = ARG_I(1), l = (int)strlen(s);
    char *buf;
    value_t r;
    if (n <= 0 || !l)
    {
        return v_cstr("");
    }
    buf = malloc(l * n + 1);
    if (!buf)
    {
        return v_cstr("");
    }
    for (int i = 0; i < n; i++)
    {
        memcpy(buf + i * l, s, l);
    }
    r = v_str(buf, l * n);
    free(buf);
    return r;
}

static value_t bi_string_width(int f, int argc, value_t *args, instance_t *self)
{
    return v_real(text_width(ARG_S(0)));
}

static value_t bi_string_height(int f, int argc, value_t *args, instance_t *self)
{
    return v_real(text_height(ARG_S(0)));

    /* drawing */
}

static value_t bi_draw_set_color(int f, int argc, value_t *args, instance_t *self)
{
        gs.draw_color = (uint32_t)ARG_I(0) & 0xffffff;
        return v_real(0);
}

static value_t bi_draw_set_alpha(int f, int argc, value_t *args, instance_t *self)
{
        gs.draw_alpha = ARG_R(0);
        return v_real(0);
}

static value_t bi_draw_set_font(int f, int argc, value_t *args, instance_t *self)
{
        gs.draw_font = ARG_I(0);
        return v_real(0);
}

static value_t bi_draw_set_halign(int f, int argc, value_t *args, instance_t *self)
{
        gs.halign = ARG_I(0);
        return v_real(0);
}

static value_t bi_draw_self(int f, int argc, value_t *args, instance_t *self)
{
        if (self)
        {
            draw_self(self);
        }
        return v_real(0);
}

static value_t bi_draw_sprite(int f, int argc, value_t *args, instance_t *self)
{
    gmreal_t sub = ARG_R(1);
    if (sub < 0 && self)
    {
        sub = self->image_index;
    }
    draw_sprite_full(ARG_I(0), sub, ARG_R(2), ARG_R(3), 1, 1, 0, 0xffffff, gs.draw_alpha);
    return v_real(0);
}

static value_t bi_draw_sprite_ext(int f, int argc, value_t *args, instance_t *self)
{
    gmreal_t sub = ARG_R(1);
    if (sub < 0 && self)
    {
        sub = self->image_index;
    }
    draw_sprite_full(ARG_I(0), sub, ARG_R(2), ARG_R(3), ARG_R(4), ARG_R(5), ARG_R(6),
                     (uint32_t)ARG_I(7) & 0xffffff, ARG_R(8));
    return v_real(0);
}

static value_t bi_draw_sprite_part(int f, int argc, value_t *args, instance_t *self)
{
    gmreal_t sub = ARG_R(1);
    bool ext = f == F_draw_sprite_part_ext;
    if (sub < 0 && self)
    {
        sub = self->image_index;
    }
    draw_sprite_part_full(ARG_I(0), sub, ARG_I(2), ARG_I(3), ARG_I(4), ARG_I(5), ARG_R(6), ARG_R(7),
                          ext ? ARG_R(8) : 1, ext ? ARG_R(9) : 1,
                          ext ? (uint32_t)ARG_I(10) & 0xffffff : 0xffffff, ext ? ARG_R(11) : gs.draw_alpha);
    return v_real(0);
}

static value_t bi_draw_sprite_stretched(int f, int argc, value_t *args, instance_t *self)
{
    int s = ARG_I(0);
    gmreal_t sub = ARG_R(1);
    if (s >= 0 && s < (int)gd.nsprites && SPR(s)->w && SPR(s)->h)
    {
        const sprite_rec_t *sp = SPR(s);
        gmreal_t xs = ARG_R(4) / sp->w, ys = ARG_R(5) / sp->h;
        draw_sprite_full(s, sub, ARG_R(2) + sp->ox * xs, ARG_R(3) + sp->oy * ys, xs, ys, 0, 0xffffff, gs.draw_alpha);
    }
    return v_real(0);
}

static value_t bi_draw_background(int f, int argc, value_t *args, instance_t *self)
{
        draw_background_full(ARG_I(0), 0, 0, -1, -1, ARG_R(1), ARG_R(2), 1, 1, 0xffffff, gs.draw_alpha);
        return v_real(0);
}

static value_t bi_draw_background_part_ext(int f, int argc, value_t *args, instance_t *self)
{
        draw_background_full(ARG_I(0), ARG_I(1), ARG_I(2), ARG_I(3), ARG_I(4), ARG_R(5), ARG_R(6),
                             ARG_R(7), ARG_R(8), (uint32_t)ARG_I(9) & 0xffffff, ARG_R(10));
        return v_real(0);
}

static value_t bi_draw_background_stretched(int f, int argc, value_t *args, instance_t *self)
{
    int b = ARG_I(0);
    if (b >= 0 && b < (int)gd.nbgs && BG(b)->w && BG(b)->h)
    {
        draw_background_full(b, 0, 0, -1, -1, ARG_R(1), ARG_R(2), ARG_R(3) / BG(b)->w,
                             ARG_R(4) / BG(b)->h, 0xffffff, gs.draw_alpha);
    }
    return v_real(0);
}

static value_t bi_draw_text(int f, int argc, value_t *args, instance_t *self)
{
    value_t s = v_tostring(argc > 2 ? args[2] : v_cstr(""));
    draw_text_full(ARG_R(0), ARG_R(1), v_cstring(s), 1, 1, 0, gs.draw_color, gs.draw_alpha);
    v_release(s);
    return v_real(0);
}

static value_t bi_draw_text_transformed(int f, int argc, value_t *args, instance_t *self)
{
    value_t s = v_tostring(argc > 2 ? args[2] : v_cstr(""));
    draw_text_full(ARG_R(0), ARG_R(1), v_cstring(s), ARG_R(3), ARG_R(4), ARG_R(5), gs.draw_color, gs.draw_alpha);
    v_release(s);
    return v_real(0);
}

static value_t bi_draw_text_transformed_color(int f, int argc, value_t *args, instance_t *self)
{
    value_t s = v_tostring(argc > 2 ? args[2] : v_cstr(""));
    draw_text_full(ARG_R(0), ARG_R(1), v_cstring(s), ARG_R(3), ARG_R(4), ARG_R(5),
                   (uint32_t)ARG_I(6) & 0xffffff, ARG_R(10));
    v_release(s);
    return v_real(0);
}

static value_t bi_draw_rectangle(int f, int argc, value_t *args, instance_t *self)
{
        render_rect(ARG_R(0), ARG_R(1), ARG_R(2), ARG_R(3), gs.draw_color, gs.draw_alpha, ARG_I(4) != 0);
        return v_real(0);
}

static value_t bi_draw_rectangle_color(int f, int argc, value_t *args, instance_t *self)
{
        render_rect(ARG_R(0), ARG_R(1), ARG_R(2), ARG_R(3), (uint32_t)ARG_I(4) & 0xffffff, gs.draw_alpha, ARG_I(8) != 0);
        return v_real(0);
}

static value_t bi_draw_roundrect(int f, int argc, value_t *args, instance_t *self)
{
        render_rect(ARG_R(0), ARG_R(1), ARG_R(2), ARG_R(3), gs.draw_color, gs.draw_alpha, ARG_I(4) != 0);
        return v_real(0);
}

static value_t bi_draw_circle(int f, int argc, value_t *args, instance_t *self)
{
        render_circle(ARG_R(0), ARG_R(1), ARG_R(2), gs.draw_color, gs.draw_alpha, ARG_I(3) != 0);
        return v_real(0);
}

static value_t bi_draw_ellipse_color(int f, int argc, value_t *args, instance_t *self)
{
    gmreal_t cx = (ARG_R(0) + ARG_R(2)) / 2, cy = (ARG_R(1) + ARG_R(3)) / 2;
    gmreal_t r = fabsf(ARG_R(2) - ARG_R(0)) / 2;
    render_circle(cx, cy, r, (uint32_t)ARG_I(4) & 0xffffff, gs.draw_alpha, ARG_I(6) != 0);
    return v_real(0);
}

static value_t bi_draw_line(int f, int argc, value_t *args, instance_t *self)
{
        render_line(ARG_R(0), ARG_R(1), ARG_R(2), ARG_R(3), 1, gs.draw_color, gs.draw_alpha);
        return v_real(0);
}

static value_t bi_draw_line_width(int f, int argc, value_t *args, instance_t *self)
{
        render_line(ARG_R(0), ARG_R(1), ARG_R(2), ARG_R(3), ARG_R(4), gs.draw_color, gs.draw_alpha);
        return v_real(0);
}

static value_t bi_draw_line_width_color(int f, int argc, value_t *args, instance_t *self)
{
        render_line(ARG_R(0), ARG_R(1), ARG_R(2), ARG_R(3), ARG_R(4), (uint32_t)ARG_I(5) & 0xffffff, gs.draw_alpha);
        return v_real(0);
}

static value_t bi_draw_line_color(int f, int argc, value_t *args, instance_t *self)
{
        render_line(ARG_R(0), ARG_R(1), ARG_R(2), ARG_R(3), 1, (uint32_t)ARG_I(4) & 0xffffff, gs.draw_alpha);
        return v_real(0);
}

static value_t bi_draw_triangle(int f, int argc, value_t *args, instance_t *self)
{
        render_triangle(ARG_R(0), ARG_R(1), ARG_R(2), ARG_R(3), ARG_R(4), ARG_R(5), gs.draw_color, gs.draw_alpha, ARG_I(6) != 0);
        return v_real(0);
}

static value_t bi_draw_triangle_color(int f, int argc, value_t *args, instance_t *self)
{
        render_triangle(ARG_R(0), ARG_R(1), ARG_R(2), ARG_R(3), ARG_R(4), ARG_R(5), (uint32_t)ARG_I(6) & 0xffffff,
                        gs.draw_alpha, ARG_I(9) != 0);
        return v_real(0);
}

static value_t bi_draw_point_color(int f, int argc, value_t *args, instance_t *self)
{
        render_rect(ARG_R(0), ARG_R(1), ARG_R(0), ARG_R(1), (uint32_t)ARG_I(2) & 0xffffff, gs.draw_alpha, false);
        return v_real(0);
}

static value_t bi_draw_getpixel(int f, int argc, value_t *args, instance_t *self)
{
        return v_real(0);
}

static value_t bi_make_color_rgb(int f, int argc, value_t *args, instance_t *self)
{
        return v_real((gmreal_t)((ARG_I(0) & 255) | (ARG_I(1) & 255) << 8 | (ARG_I(2) & 255) << 16));
}

static value_t bi_make_color_hsv(int f, int argc, value_t *args, instance_t *self)
{
        return v_real((gmreal_t)hsv(ARG_R(0), ARG_R(1), ARG_R(2)));
}

static value_t bi_merge_color(int f, int argc, value_t *args, instance_t *self)
{
        return v_real((gmreal_t)merge_colors((uint32_t)ARG_I(0), (uint32_t)ARG_I(1), ARG_R(2)));

    /* sprites and backgrounds */
}

static value_t bi_sprite_get_width(int f, int argc, value_t *args, instance_t *self)
{
    int s = ARG_I(0);
    return v_real(s >= 0 && s < (int)gd.nsprites ? SPR(s)->w : 0);
}

static value_t bi_sprite_get_height(int f, int argc, value_t *args, instance_t *self)
{
    int s = ARG_I(0);
    return v_real(s >= 0 && s < (int)gd.nsprites ? SPR(s)->h : 0);
}

static value_t bi_sprite_exists(int f, int argc, value_t *args, instance_t *self)
{
        return v_real(ARG_I(0) >= 0 && ARG_I(0) < (int)gd.nsprites ? 1 : 0);
}

static value_t bi_sprite_get_name(int f, int argc, value_t *args, instance_t *self)
{
    int s = ARG_I(0);
    return v_cstr(s >= 0 && s < (int)gd.nsprites ? far_name(SPR(s)->name) : "");
}

static value_t bi_background_get_width(int f, int argc, value_t *args, instance_t *self)
{
    int b = ARG_I(0);
    return v_real(b >= 0 && b < (int)gd.nbgs ? BG(b)->w : 0);
}

static value_t bi_background_get_height(int f, int argc, value_t *args, instance_t *self)
{
    int b = ARG_I(0);
    return v_real(b >= 0 && b < (int)gd.nbgs ? BG(b)->h : 0);
}

static value_t bi_sprite_create_from_surface(int f, int argc, value_t *args, instance_t *self)
{
        return v_real(-1);

    /* collisions */
}

static value_t bi_collision_rectangle(int f, int argc, value_t *args, instance_t *self)
{
        return id_or_noone(query_rect(ARG_R(0), ARG_R(1), ARG_R(2), ARG_R(3), ARG_I(4), ARG_I(5) != 0, ARG_I(6) != 0));
}

static value_t bi_collision_point(int f, int argc, value_t *args, instance_t *self)
{
        return id_or_noone(query_point(ARG_R(0), ARG_R(1), ARG_I(2), ARG_I(3) != 0, ARG_I(4) != 0));
}

static value_t bi_collision_line(int f, int argc, value_t *args, instance_t *self)
{
        return id_or_noone(query_line(ARG_R(0), ARG_R(1), ARG_R(2), ARG_R(3), ARG_I(4), ARG_I(5) != 0, ARG_I(6) != 0));
}

static value_t bi_collision_circle(int f, int argc, value_t *args, instance_t *self)
{
        return id_or_noone(query_circle(ARG_R(0), ARG_R(1), ARG_R(2), ARG_I(3), ARG_I(4) != 0, ARG_I(5) != 0));

    /* rooms and the game */
}

static value_t bi_room_goto(int f, int argc, value_t *args, instance_t *self)
{
        room_goto(ARG_I(0));
        return v_real(0);
}

static value_t bi_room_goto_next(int f, int argc, value_t *args, instance_t *self)
{
        room_goto(room_next_of(gs.room));
        return v_real(0);
}

static value_t bi_room_goto_previous(int f, int argc, value_t *args, instance_t *self)
{
        room_goto(room_prev_of(gs.room));
        return v_real(0);
}

static value_t bi_room_restart(int f, int argc, value_t *args, instance_t *self)
{
        room_goto(gs.room);
        return v_real(0);
}

static value_t bi_room_next(int f, int argc, value_t *args, instance_t *self)
{
        return v_real((gmreal_t)room_next_of(ARG_I(0)));
}

static value_t bi_room_previous(int f, int argc, value_t *args, instance_t *self)
{
        return v_real((gmreal_t)room_prev_of(ARG_I(0)));
}

static value_t bi_room_set_persistent(int f, int argc, value_t *args, instance_t *self)
{
        if (ARG_I(0) >= 0 && ARG_I(0) < (int)gd.nrooms)
        {
            room_set_persistent(ARG_I(0), ARG_I(1) != 0);
        }
        return v_real(0);
}

static value_t bi_room_get_name(int f, int argc, value_t *args, instance_t *self)
{
    int r = ARG_I(0);
    return v_cstr(r >= 0 && r < (int)gd.nrooms ? far_name(ROOM(r)->name) : "");
}

static value_t bi_game_restart(int f, int argc, value_t *args, instance_t *self)
{
        gs.restart_game = true;
        return v_real(0);
}

static value_t bi_game_end(int f, int argc, value_t *args, instance_t *self)
{
        gs.end_game = true;
        return v_real(0);

    /* keyboard and controllers */
}

static value_t bi_keyboard_check(int f, int argc, value_t *args, instance_t *self)
{
    int k = ARG_I(0);
    if (k == VK_ANYKEY)
    {
        for (int i = 2; i < 256; i++)
        {
            if (key_down[i])
            {
                return v_real(1);
            }
        }
        return v_real(0);
    }
    if (k == VK_NOKEY)
    {
        for (int i = 2; i < 256; i++)
        {
            if (key_down[i])
            {
                return v_real(0);
            }
        }
        return v_real(1);
    }
    return v_real(k > 0 && k < 256 && key_down[k] ? 1 : 0);
}

static value_t bi_keyboard_check_pressed(int f, int argc, value_t *args, instance_t *self)
{
    int k = ARG_I(0);
    if (k == VK_ANYKEY)
    {
        for (int i = 2; i < 256; i++)
        {
            if (key_pressed[i])
            {
                return v_real(1);
            }
        }
        return v_real(0);
    }
    return v_real(k > 0 && k < 256 && key_pressed[k] ? 1 : 0);
}

static value_t bi_keyboard_check_released(int f, int argc, value_t *args, instance_t *self)
{
    int k = ARG_I(0);
    return v_real(k > 0 && k < 256 && key_released[k] ? 1 : 0);
}

static value_t bi_keyboard_clear(int f, int argc, value_t *args, instance_t *self)
{
    int k = ARG_I(0);
    if (k > 0 && k < 256)
    {
        key_down[k] = key_pressed[k] = key_released[k] = 0;
    }
    return v_real(0);
}

static value_t bi_keyboard_key_press(int f, int argc, value_t *args, instance_t *self)
{
    int k = ARG_I(0);
    if (k > 0 && k < 256)
    {
        if (!key_down[k])
        {
            key_pressed[k] = 1;
        }
        key_down[k] = 1;
    }
    return v_real(0);
}

static value_t bi_keyboard_key_release(int f, int argc, value_t *args, instance_t *self)
{
    int k = ARG_I(0);
    if (k > 0 && k < 256)
    {
        if (key_down[k])
        {
            key_released[k] = 1;
        }
        key_down[k] = 0;
    }
    return v_real(0);
}

static value_t bi_gamepad_button_check(int f, int argc, value_t *args, instance_t *self)
{
        return v_real(0);

    /* motion (drag-and-drop actions too) */
}

static value_t bi_action_set_relative(int f, int argc, value_t *args, instance_t *self)
{
        dnd_relative = ARG_I(0) != 0;
        return v_real(0);
}

static value_t bi_action_move_to(int f, int argc, value_t *args, instance_t *self)
{
        if (self)
        {
            self->x = ARG_R(0) + (dnd_relative ? self->x : 0);
            self->y = ARG_R(1) + (dnd_relative ? self->y : 0);
        }
        return v_real(0);
}

static value_t bi_action_set_alarm(int f, int argc, value_t *args, instance_t *self)
{
        if (self)
        {
            int a = ARG_I(1);
            if (a >= 0 && a < MAX_ALARMS)
            {
                inst_m(self)->alarm[a] = (int16_t)(ARG_I(0) + (dnd_relative ? self->m->alarm[a] : 0));
            }
        }
        return v_real(0);
}

static value_t bi_action_set_hspeed(int f, int argc, value_t *args, instance_t *self)
{
        if (self)
        {
            inst_m(self)->hspeed = ARG_R(0) + (dnd_relative ? self->m->hspeed : 0);
            sync_from_hv(self);
        }
        return v_real(0);
}

static value_t bi_action_set_motion(int f, int argc, value_t *args, instance_t *self)
{
        if (self)
        {
            if (dnd_relative)
            {
                inst_m(self)->hspeed += ARG_R(1) * gm_dcos(ARG_R(0));
                inst_m(self)->vspeed -= ARG_R(1) * gm_dsin(ARG_R(0));
                sync_from_hv(self);
            }
            else
            {
                set_speed_dir(self, ARG_R(1), ARG_R(0));
            }
        }
        return v_real(0);
}

static value_t bi_action_move(int f, int argc, value_t *args, instance_t *self)
{
        if (self)
        {
            /* "dirs" is 9 characters for the 3x3 direction grid */
            const char *dirs = ARG_S(0);
            static const int dx[9] = { -1, 0, 1, -1, 0, 1, -1, 0, 1 };
            static const int dy[9] = { 1, 1, 1, 0, 0, 0, -1, -1, -1 };
            int choices[9], n = 0;
            for (int i = 0; i < 9 && dirs[i]; i++)
            {
                if (dirs[i] == '1')
                {
                    choices[n++] = i;
                }
            }
            if (n)
            {
                int c = choices[(int)(frand() * n) % n];
                if (c == 4)
                {
                    set_speed_dir(self, 0, self->m->direction);
                }
                else
                {
                    gmreal_t d = gm_datan2((gmreal_t)dy[c], (gmreal_t)dx[c]);
                    set_speed_dir(self, ARG_R(1), d < 0 ? d + 360 : d);
                }
            }
        }
        return v_real(0);
}

static value_t bi_action_move_point(int f, int argc, value_t *args, instance_t *self)
{
        if (self)
        {
            gmreal_t d = gm_datan2(-(ARG_R(1) - self->y), ARG_R(0) - self->x);
            set_speed_dir(self, ARG_R(2), d < 0 ? d + 360 : d);
        }
        return v_real(0);
}

static value_t bi_action_set_gravity(int f, int argc, value_t *args, instance_t *self)
{
        if (self)
        {
            inst_m(self)->gravity_direction = ARG_R(0);
            inst_m(self)->gravity = ARG_R(1);
        }
        return v_real(0);
}

static value_t bi_action_set_friction(int f, int argc, value_t *args, instance_t *self)
{
        if (self)
        {
            inst_m(self)->friction = ARG_R(0);
        }
        return v_real(0);

    /* paths and tiles */
}

static value_t bi_path_start(int f, int argc, value_t *args, instance_t *self)
{
        if (self)
        {
            path_begin(self, ARG_I(0), ARG_R(1), ARG_I(2), ARG_I(3) != 0);
        }
        return v_real(0);
}

static value_t bi_path_end(int f, int argc, value_t *args, instance_t *self)
{
        if (self)
        {
            inst_m(self)->path_index = -1;
        }
        return v_real(0);
}

static value_t bi_tile_layer_shift(int f, int argc, value_t *args, instance_t *self)
{
        tile_layer_shift(ARG_I(0), ARG_I(1), ARG_I(2));
        return v_real(0);
}

static value_t bi_tile_layer_hide(int f, int argc, value_t *args, instance_t *self)
{
        tile_layer_visible(ARG_I(0), false);
        return v_real(0);
}

static value_t bi_tile_layer_show(int f, int argc, value_t *args, instance_t *self)
{
        tile_layer_visible(ARG_I(0), true);
        return v_real(0);

    /* ds_map */
}

static value_t bi_ds_map_create(int f, int argc, value_t *args, instance_t *self)
{
        return v_real((gmreal_t)map_create());
}

static value_t bi_ds_map_destroy(int f, int argc, value_t *args, instance_t *self)
{
        map_destroy(ARG_I(0));
        return v_real(0);
}

static value_t bi_ds_map_add(int f, int argc, value_t *args, instance_t *self)
{
    dsmap_t *m = get_map(ARG_I(0));
    if (m && argc >= 3)
    {
        map_put(m, args[1], args[2], f != F_ds_map_add);
    }
    return v_real(0);
}

static value_t bi_ds_map_find_value(int f, int argc, value_t *args, instance_t *self)
{
    dsmap_t *m;
    if (ARG_I(0) == TEXT_MAP_ID)
    {
        int t = text_find(ARG_S(1));
        return t >= 0 ? text_by_id((uint16_t)t) : v_undef();
    }
    m = get_map(ARG_I(0));
    bool found;
    map_entry_t *e;
    if (!m || argc < 2)
    {
        return v_undef();
    }
    e = map_slot(m, args[1], &found);
    if (!found)
    {
        return v_undef();
    }
    v_retain(e->val);
    return e->val;
}

static value_t bi_ds_map_delete(int f, int argc, value_t *args, instance_t *self)
{
    {
        dsmap_t *m = get_map(ARG_I(0));
        bool found;
        map_entry_t *e;
        if (!m || argc < 2)
        {
            return v_real(0);
        }
        e = map_slot(m, args[1], &found);
        if (found)
        {
            /* leave a tombstone: key undefined, value marked */
            v_release(e->key);
            v_release(e->val);
            e->key = v_undef();
            e->val = v_real(0);
        }
        return v_real(0);
    }

    /* ini */
}

static value_t bi_ini_open(int f, int argc, value_t *args, instance_t *self)
{
        ini_open_file(ARG_S(0));
        return v_real(0);
}

static value_t bi_ini_open_from_string(int f, int argc, value_t *args, instance_t *self)
{
        ini_close_file();
        ini.text = strdup(ARG_S(0));
        ini.open = true;
        return v_real(0);
}

static value_t bi_ini_close(int f, int argc, value_t *args, instance_t *self)
{
        ini_close_file();
        return v_real(0);
}

static value_t bi_ini_read_real(int f, int argc, value_t *args, instance_t *self)
{
    int len;
    const char *v = ini.text ? ini_find(ARG_S(0), ARG_S(1), &len, NULL) : NULL;
    char buf[64];
    if (!v)
    {
        return v_real(ARG_R(2));
    }
    if (len > 63)
    {
        len = 63;
    }
    memcpy(buf, v, len);
    buf[len] = 0;
    return v_real((gmreal_t)atof(buf));
}

static value_t bi_ini_read_string(int f, int argc, value_t *args, instance_t *self)
{
    int len;
    const char *v = ini.text ? ini_find(ARG_S(0), ARG_S(1), &len, NULL) : NULL;
    if (!v)
    {
        value_t d = argc > 2 ? args[2] : v_cstr("");
        v_retain(d);
        return d;
    }
    return v_str(v, len);
}

static value_t bi_ini_write_real(int f, int argc, value_t *args, instance_t *self)
{
        if (ini.text)
        {
            value_t s = v_tostring(argc > 2 ? args[2] : v_real(0));
            ini_write(ARG_S(0), ARG_S(1), v_cstring(s));
            v_release(s);
        }
        return v_real(0);
}

static value_t bi_ini_section_exists(int f, int argc, value_t *args, instance_t *self)
{
    {
        int len;
        bool has = false;
        if (ini.text)
        {
            ini_find(ARG_S(0), "\x01", &len, &has);
        }
        return v_real(has ? 1 : 0);
    }

    /* files */
}

static value_t bi_file_exists(int f, int argc, value_t *args, instance_t *self)
{
        return v_real(plat_file_exists(ARG_S(0)) ? 1 : 0);
}

static value_t bi_file_delete(int f, int argc, value_t *args, instance_t *self)
{
        plat_file_delete(ARG_S(0));
        return v_real(0);
}

static value_t bi_file_rename(int f, int argc, value_t *args, instance_t *self)
{
    char *data;
    int len;
    if (plat_file_read(ARG_S(0), &data, &len))
    {
        plat_file_write(ARG_S(1), data, len);
        free(data);
        plat_file_delete(ARG_S(0));
        return v_real(1);
    }
    return v_real(0);
}

static value_t bi_file_text_open_read(int f, int argc, value_t *args, instance_t *self)
{
        return v_real((gmreal_t)tfile_open(ARG_S(0), false));
}

static value_t bi_file_text_open_write(int f, int argc, value_t *args, instance_t *self)
{
        return v_real((gmreal_t)tfile_open(ARG_S(0), true));
}

static value_t bi_file_text_close(int f, int argc, value_t *args, instance_t *self)
{
        tfile_close(ARG_I(0));
        return v_real(0);
}

static value_t bi_file_text_read_string(int f, int argc, value_t *args, instance_t *self)
{
    tfile_t *t = tfile(ARG_I(0));
    return t ? tfile_read_string(t) : v_cstr("");
}

static value_t bi_file_text_read_real(int f, int argc, value_t *args, instance_t *self)
{
    tfile_t *t = tfile(ARG_I(0));
    value_t s;
    gmreal_t r;
    if (!t)
    {
        return v_real(0);
    }
    s = tfile_read_string(t);
    r = (gmreal_t)atof(v_cstring(s));
    v_release(s);
    return v_real(r);
}

static value_t bi_file_text_readln(int f, int argc, value_t *args, instance_t *self)
{
    tfile_t *t = tfile(ARG_I(0));
    if (t)
    {
        tfile_readln(t);
    }
    return v_real(0);
}

static value_t bi_file_text_eof(int f, int argc, value_t *args, instance_t *self)
{
    tfile_t *t = tfile(ARG_I(0));
    return v_real(!t || t->pos >= t->len ? 1 : 0);
}

static value_t bi_file_text_write_string(int f, int argc, value_t *args, instance_t *self)
{
    tfile_t *t = tfile(ARG_I(0));
    if (t && argc > 1)
    {
        value_t s = v_tostring(args[1]);
        tfile_append(t, v_cstring(s));
        v_release(s);
    }
    return v_real(0);
}

static value_t bi_file_text_writeln(int f, int argc, value_t *args, instance_t *self)
{
    {
        tfile_t *t = tfile(ARG_I(0));
        if (t)
        {
            tfile_append(t, "\r\n");
        }
        return v_real(0);
    }

    /* platform stubs */
}

static value_t bi_os_get_language(int f, int argc, value_t *args, instance_t *self)
{
        return v_cstr("en");
}

static value_t bi_os_get_region(int f, int argc, value_t *args, instance_t *self)
{
        return v_cstr("US");
}

static value_t bi_window_get_width(int f, int argc, value_t *args, instance_t *self)
{
        return v_real(640);
}

static value_t bi_window_get_height(int f, int argc, value_t *args, instance_t *self)
{
        return v_real(480);
}

static value_t bi_surface_get_width(int f, int argc, value_t *args, instance_t *self)
{
        return v_real(SCREEN_W);
}

static value_t bi_surface_get_height(int f, int argc, value_t *args, instance_t *self)
{
        return v_real(SCREEN_H);
}

static value_t bi_audio_play_sound(int f, int argc, value_t *args, instance_t *self)
{
        return v_real((gmreal_t)plat_sound_play(ARG_I(0), ARG_I(2) != 0, ARG_R(1)));
}

static value_t bi_audio_stop_sound(int f, int argc, value_t *args, instance_t *self)
{
        plat_sound_stop(ARG_I(0));
        return v_real(0);
}

static value_t bi_audio_stop_all(int f, int argc, value_t *args, instance_t *self)
{
        plat_sound_stop_all();
        return v_real(0);
}

static value_t bi_audio_sound_gain(int f, int argc, value_t *args, instance_t *self)
{
        plat_sound_gain(ARG_I(0), ARG_R(1), ARG_I(2));
        return v_real(0);
}

static value_t bi_audio_sound_pitch(int f, int argc, value_t *args, instance_t *self)
{
        plat_sound_pitch(ARG_I(0), ARG_R(1));
        return v_real(0);
}

static value_t bi_audio_is_playing(int f, int argc, value_t *args, instance_t *self)
{
        return v_real(plat_sound_playing(ARG_I(0)) ? 1 : 0);
}

static value_t bi_audio_pause_sound(int f, int argc, value_t *args, instance_t *self)
{
        plat_sound_pause(ARG_I(0), f == F_audio_pause_sound);
        return v_real(0);
}

static value_t bi_surface_create(int f, int argc, value_t *args, instance_t *self)
{
        return v_real(-1);
}

static value_t bi_date_current_datetime(int f, int argc, value_t *args, instance_t *self)
{
        return v_real((gmreal_t)(plat_time_ms() / 86400000.0 + 25569.0));
}

static value_t bi_variable_global_exists(int f, int argc, value_t *args, instance_t *self)
{
    int g = data_find_global(ARG_S(0));
    return v_real(g >= 0 && globals[g].t != VT_UNDEF ? 1 : 0);
}

static value_t bi_vm_texttable(int f, int argc, value_t *args, instance_t *self)
{
        return v_real(TEXT_MAP_ID);
}

static value_t bi_vm_gettext(int f, int argc, value_t *args, instance_t *self)
{
    /* scr_gettext(key, ...) with a key only known at run time */
    int t = text_find(ARG_S(0));
    value_t r, saved = args[0];
    args[0] = t >= 0 ? text_by_id((uint16_t)t) : v_cstr("");
    r = native_gettext(argc, args);
    v_release(args[0]);
    args[0] = saved;
    return r;
}

static value_t bi_json_encode(int f, int argc, value_t *args, instance_t *self)
{
        return v_cstr("{}");
}

static value_t bi_json_decode(int f, int argc, value_t *args, instance_t *self)
{
        return v_real((gmreal_t)map_create());
}

static value_t bi_default(int f, int argc, value_t *args, instance_t *self)
{
        /* audio, steam, window, surface, buffer and other no-ops */
        return v_real(0);
}

/* the dispatch table: built-in number -> function (in RAM; on the
   calculator far_init points the entries at the far code; not static, or
   the compiler would make it a constant, in flash) */
typedef value_t (*builtin_fn)(int f, int argc, value_t *args, instance_t *self);
builtin_fn builtin_table[F_COUNT] = {
    [F_instance_create] = bi_instance_create,
    [F_action_create_object] = bi_action_create_object,
    [F_instance_destroy] = bi_instance_destroy,
    [F_action_kill_object] = bi_instance_destroy,
    [F_instance_exists] = bi_instance_exists,
    [F_instance_number] = bi_instance_number,
    [F_instance_find] = bi_instance_find,
    [F_instance_position] = bi_instance_position,
    [F_instance_change] = bi_instance_change,
    [F_instance_deactivate_all] = bi_instance_deactivate_all,
    [F_instance_activate_all] = bi_instance_activate_all,
    [F_instance_activate_object] = bi_instance_activate_object,
    [F_event_user] = bi_event_user,
    [F_event_perform] = bi_event_perform,
    [F_event_inherited] = bi_event_inherited,
    [F_script_execute] = bi_script_execute,
    [F_random] = bi_random,
    [F_irandom] = bi_irandom,
    [F_choose] = bi_choose,
    [F_randomize] = bi_randomize,
    [F_floor] = bi_floor,
    [F_ceil] = bi_ceil,
    [F_round] = bi_round,
    [F_abs] = bi_abs,
    [F_sin] = bi_sin,
    [F_cos] = bi_cos,
    [F_sqrt] = bi_sqrt,
    [F_sqr] = bi_sqr,
    [F_power] = bi_power,
    [F_degtorad] = bi_degtorad,
    [F_darctan2] = bi_darctan2,
    [F_min] = bi_min,
    [F_max] = bi_min,
    [F_lengthdir_x] = bi_lengthdir_x,
    [F_lengthdir_y] = bi_lengthdir_y,
    [F_point_direction] = bi_point_direction,
    [F_point_distance] = bi_point_distance,
    [F_distance_to_point] = bi_distance_to_point,
    [F_distance_to_object] = bi_distance_to_object,
    [F_move_towards_point] = bi_move_towards_point,
    [F_move_snap] = bi_move_snap,
    [F_is_undefined] = bi_is_undefined,
    [F_real] = bi_real,
    [F_string] = bi_string,
    [F_string_length] = bi_string_length,
    [F_string_byte_length] = bi_string_byte_length,
    [F_string_char_at] = bi_string_char_at,
    [F_ord] = bi_ord,
    [F_chr] = bi_chr,
    [F_string_pos] = bi_string_pos,
    [F_string_copy] = bi_string_copy,
    [F_string_delete] = bi_string_delete,
    [F_string_replace_all] = bi_string_replace_all,
    [F_string_lower] = bi_string_lower,
    [F_string_upper] = bi_string_lower,
    [F_string_repeat] = bi_string_repeat,
    [F_string_width] = bi_string_width,
    [F_string_height] = bi_string_height,
    [F_draw_set_color] = bi_draw_set_color,
    [F_draw_set_colour] = bi_draw_set_color,
    [F_draw_set_alpha] = bi_draw_set_alpha,
    [F_draw_set_font] = bi_draw_set_font,
    [F_draw_set_halign] = bi_draw_set_halign,
    [F_draw_self] = bi_draw_self,
    [F_draw_sprite] = bi_draw_sprite,
    [F_draw_sprite_ext] = bi_draw_sprite_ext,
    [F_draw_sprite_part] = bi_draw_sprite_part,
    [F_draw_sprite_part_ext] = bi_draw_sprite_part,
    [F_draw_sprite_stretched] = bi_draw_sprite_stretched,
    [F_draw_background] = bi_draw_background,
    [F_draw_background_part_ext] = bi_draw_background_part_ext,
    [F_draw_background_stretched] = bi_draw_background_stretched,
    [F_draw_text] = bi_draw_text,
    [F_draw_text_ext] = bi_draw_text,
    [F_draw_text_transformed] = bi_draw_text_transformed,
    [F_draw_text_transformed_color] = bi_draw_text_transformed_color,
    [F_draw_rectangle] = bi_draw_rectangle,
    [F_draw_rectangle_color] = bi_draw_rectangle_color,
    [F_draw_roundrect] = bi_draw_roundrect,
    [F_draw_circle] = bi_draw_circle,
    [F_draw_ellipse_color] = bi_draw_ellipse_color,
    [F_draw_line] = bi_draw_line,
    [F_draw_line_width] = bi_draw_line_width,
    [F_draw_line_width_color] = bi_draw_line_width_color,
    [F_draw_line_color] = bi_draw_line_color,
    [F_draw_triangle] = bi_draw_triangle,
    [F_draw_triangle_color] = bi_draw_triangle_color,
    [F_draw_point_color] = bi_draw_point_color,
    [F_draw_getpixel] = bi_draw_getpixel,
    [F_make_color_rgb] = bi_make_color_rgb,
    [F_make_color_hsv] = bi_make_color_hsv,
    [F_merge_color] = bi_merge_color,
    [F_sprite_get_width] = bi_sprite_get_width,
    [F_sprite_get_height] = bi_sprite_get_height,
    [F_sprite_exists] = bi_sprite_exists,
    [F_sprite_get_name] = bi_sprite_get_name,
    [F_background_get_width] = bi_background_get_width,
    [F_background_get_height] = bi_background_get_height,
    [F_sprite_create_from_surface] = bi_sprite_create_from_surface,
    [F_collision_rectangle] = bi_collision_rectangle,
    [F_collision_point] = bi_collision_point,
    [F_collision_line] = bi_collision_line,
    [F_collision_circle] = bi_collision_circle,
    [F_room_goto] = bi_room_goto,
    [F_room_goto_next] = bi_room_goto_next,
    [F_room_goto_previous] = bi_room_goto_previous,
    [F_action_previous_room] = bi_room_goto_previous,
    [F_room_restart] = bi_room_restart,
    [F_room_next] = bi_room_next,
    [F_room_previous] = bi_room_previous,
    [F_room_set_persistent] = bi_room_set_persistent,
    [F_room_get_name] = bi_room_get_name,
    [F_game_restart] = bi_game_restart,
    [F_game_end] = bi_game_end,
    [F_keyboard_check] = bi_keyboard_check,
    [F_keyboard_check_direct] = bi_keyboard_check,
    [F_keyboard_check_pressed] = bi_keyboard_check_pressed,
    [F_keyboard_check_released] = bi_keyboard_check_released,
    [F_keyboard_clear] = bi_keyboard_clear,
    [F_keyboard_key_press] = bi_keyboard_key_press,
    [F_keyboard_key_release] = bi_keyboard_key_release,
    [F_gamepad_button_check] = bi_gamepad_button_check,
    [F_gamepad_is_connected] = bi_gamepad_button_check,
    [F_gamepad_get_device_count] = bi_gamepad_button_check,
    [F_gamepad_axis_value] = bi_gamepad_button_check,
    [F_joystick_check_button] = bi_gamepad_button_check,
    [F_joystick_buttons] = bi_gamepad_button_check,
    [F_joystick_exists] = bi_gamepad_button_check,
    [F_joystick_has_pov] = bi_gamepad_button_check,
    [F_joystick_xpos] = bi_gamepad_button_check,
    [F_joystick_ypos] = bi_gamepad_button_check,
    [F_joystick_pov] = bi_gamepad_button_check,
    [F_joystick_direction] = bi_gamepad_button_check,
    [F_action_set_relative] = bi_action_set_relative,
    [F_action_move_to] = bi_action_move_to,
    [F_action_set_alarm] = bi_action_set_alarm,
    [F_action_set_hspeed] = bi_action_set_hspeed,
    [F_action_set_motion] = bi_action_set_motion,
    [F_action_move] = bi_action_move,
    [F_action_move_point] = bi_action_move_point,
    [F_action_set_gravity] = bi_action_set_gravity,
    [F_action_set_friction] = bi_action_set_friction,
    [F_path_start] = bi_path_start,
    [F_path_end] = bi_path_end,
    [F_tile_layer_shift] = bi_tile_layer_shift,
    [F_tile_layer_hide] = bi_tile_layer_hide,
    [F_tile_layer_show] = bi_tile_layer_show,
    [F_ds_map_create] = bi_ds_map_create,
    [F_ds_map_destroy] = bi_ds_map_destroy,
    [F_ds_map_add] = bi_ds_map_add,
    [F_ds_map_set] = bi_ds_map_add,
    [F_ds_map_set_post] = bi_ds_map_add,
    [F_ds_map_find_value] = bi_ds_map_find_value,
    [F_ds_map_delete] = bi_ds_map_delete,
    [F_ini_open] = bi_ini_open,
    [F_ini_open_from_string] = bi_ini_open_from_string,
    [F_ini_close] = bi_ini_close,
    [F_ini_read_real] = bi_ini_read_real,
    [F_ini_read_string] = bi_ini_read_string,
    [F_ini_write_real] = bi_ini_write_real,
    [F_ini_write_string] = bi_ini_write_real,
    [F_ini_section_exists] = bi_ini_section_exists,
    [F_file_exists] = bi_file_exists,
    [F_file_delete] = bi_file_delete,
    [F_file_rename] = bi_file_rename,
    [F_file_text_open_read] = bi_file_text_open_read,
    [F_file_text_open_write] = bi_file_text_open_write,
    [F_file_text_close] = bi_file_text_close,
    [F_file_text_read_string] = bi_file_text_read_string,
    [F_file_text_read_real] = bi_file_text_read_real,
    [F_file_text_readln] = bi_file_text_readln,
    [F_file_text_eof] = bi_file_text_eof,
    [F_file_text_write_string] = bi_file_text_write_string,
    [F_file_text_write_real] = bi_file_text_write_string,
    [F_file_text_writeln] = bi_file_text_writeln,
    [F_os_get_language] = bi_os_get_language,
    [F_os_get_region] = bi_os_get_region,
    [F_window_get_width] = bi_window_get_width,
    [F_window_get_height] = bi_window_get_height,
    [F_surface_get_width] = bi_surface_get_width,
    [F_surface_get_height] = bi_surface_get_height,
    [F_audio_play_sound] = bi_audio_play_sound,
    [F_audio_stop_sound] = bi_audio_stop_sound,
    [F_audio_stop_all] = bi_audio_stop_all,
    [F_audio_sound_gain] = bi_audio_sound_gain,
    [F_audio_sound_pitch] = bi_audio_sound_pitch,
    [F_audio_is_playing] = bi_audio_is_playing,
    [F_audio_pause_sound] = bi_audio_pause_sound,
    [F_audio_resume_sound] = bi_audio_pause_sound,
    [F_surface_create] = bi_surface_create,
    [F_buffer_create] = bi_surface_create,
    [F_get_string_async] = bi_surface_create,
    [F_buffer_load_async] = bi_surface_create,
    [F_buffer_save_async] = bi_surface_create,
    [F_date_current_datetime] = bi_date_current_datetime,
    [F_variable_global_exists] = bi_variable_global_exists,
    [F_vm_texttable] = bi_vm_texttable,
    [F_vm_gettext] = bi_vm_gettext,
    [F_json_encode] = bi_json_encode,
    [F_json_decode] = bi_json_decode,
};

static value_t builtin_dispatch(int f, int argc, value_t *args, instance_t *self)
{
    builtin_fn fn = f >= 0 && f < F_COUNT ? builtin_table[f] : NULL;
    return (fn ? fn : bi_default)(f, argc, args, self);
}

/* ---- built-in variables ---- */

static view_t *view_at(int32_t idx)
{
    if (idx < 0)
    {
        idx = 0;
    }
    return &gs.views[idx % MAX_VIEWS];
}

static background_t *bg_at(int32_t idx)
{
    if (idx < 0)
    {
        idx = 0;
    }
    return &gs.bgs[idx % 8];
}

bool builtin_var_get(instance_t *in, uint16_t id, int32_t idx, value_t *out)
{
    gmreal_t l, t, r, b;
    switch (id)
    {
    /* not tied to an instance */
    case V_room: *out = v_real((gmreal_t)gs.room); return true;
    case V_room_width: *out = v_real((gmreal_t)gs.room_width); return true;
    case V_room_height: *out = v_real((gmreal_t)gs.room_height); return true;
    case V_room_speed: *out = v_real((gmreal_t)gs.room_speed); return true;
    case V_room_first: *out = v_real(room_order(0)); return true;
    case V_room_last: *out = v_real(room_order((int)gd.nroomorder - 1)); return true;
    case V_room_persistent: *out = v_real(room_is_persistent(gs.room) ? 1 : 0); return true;
    case V_view_current: *out = v_real((gmreal_t)gs.view_current); return true;
    case V_view_enabled: *out = v_real(gs.views_enabled ? 1 : 0); return true;
    case V_view_xview: *out = v_real(view_at(idx)->x); return true;
    case V_view_yview: *out = v_real(view_at(idx)->y); return true;
    case V_view_wview: *out = v_real(view_at(idx)->w); return true;
    case V_view_hview: *out = v_real(view_at(idx)->h); return true;
    case V_view_xport: *out = v_real(view_at(idx)->px); return true;
    case V_view_yport: *out = v_real(view_at(idx)->py); return true;
    case V_view_wport: *out = v_real(view_at(idx)->pw); return true;
    case V_view_hport: *out = v_real(view_at(idx)->ph); return true;
    case V_view_visible: *out = v_real(view_at(idx)->visible ? 1 : 0); return true;
    case V_view_object: *out = v_real((gmreal_t)view_at(idx)->follow); return true;
    case V_view_hborder: *out = v_real(view_at(idx)->hborder); return true;
    case V_view_vborder: *out = v_real(view_at(idx)->vborder); return true;
    case V_view_angle: *out = v_real(view_at(idx)->angle); return true;
    case V_background_color: *out = v_real((gmreal_t)gs.background_color); return true;
    case V_background_showcolor: *out = v_real(gs.background_showcolor ? 1 : 0); return true;
    case V_background_visible: *out = v_real(bg_at(idx)->visible ? 1 : 0); return true;
    case V_background_index: *out = v_real((gmreal_t)bg_at(idx)->index); return true;
    case V_background_x: *out = v_real(bg_at(idx)->x); return true;
    case V_background_y: *out = v_real(bg_at(idx)->y); return true;
    case V_background_hspeed: *out = v_real(bg_at(idx)->hspeed); return true;
    case V_background_vspeed: *out = v_real(bg_at(idx)->vspeed); return true;
    case V_background_alpha: *out = v_real(bg_at(idx)->alpha); return true;
    case V_background_blend: *out = v_real((gmreal_t)bg_at(idx)->blend); return true;
    case V_background_htiled: *out = v_real(bg_at(idx)->htiled ? 1 : 0); return true;
    case V_background_vtiled: *out = v_real(bg_at(idx)->vtiled ? 1 : 0); return true;
    case V_background_xscale: *out = v_real(bg_at(idx)->xscale); return true;
    case V_background_yscale: *out = v_real(bg_at(idx)->yscale); return true;
    case V_os_type: *out = v_real(0); return true; /* os_windows */
    case V_current_time: *out = v_real((gmreal_t)plat_time_ms()); return true;
    case V_current_hour: case V_current_minute: case V_current_second: case V_current_day:
    case V_current_month: case V_current_year: case V_current_weekday:
    {
        gmreal_t v = 0;
#ifndef __TICE__
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
#else
        struct tm *tm = NULL; /* the calculator has no calendar clock here */
#endif
        if (tm)
        {
            switch (id)
            {
            case V_current_hour: v = (gmreal_t)tm->tm_hour; break;
            case V_current_minute: v = (gmreal_t)tm->tm_min; break;
            case V_current_second: v = (gmreal_t)tm->tm_sec; break;
            case V_current_day: v = (gmreal_t)tm->tm_mday; break;
            case V_current_month: v = (gmreal_t)(tm->tm_mon + 1); break;
            case V_current_year: v = (gmreal_t)(tm->tm_year + 1900); break;
            default: v = (gmreal_t)tm->tm_wday; break;
            }
        }
        *out = v_real(v);
        return true;
    }
    case V_async_load: *out = v_real(-1); return true;
    case V_application_surface: *out = v_real(-1); return true;
    case V_undefined: *out = v_undef(); return true;
    case V_mouse_x: case V_mouse_y: *out = v_real(0); return true;
    case V_working_directory: *out = v_cstr(""); return true;
    case V_keyboard_lastchar: *out = v_cstr(""); return true;
    case V_keyboard_key: *out = v_real(0); return true;
    case V_fps: *out = v_real(30); return true;
    case V_instance_count: *out = v_real((gmreal_t)ninstances); return true;
    case V_score: case V_lives: case V_health: *out = v_real(0); return true;
    }
    if (!in)
    {
        *out = v_real(0);
        return true;
    }
    switch (id)
    {
    case V_x: *out = v_real(in->x); return true;
    case V_y: *out = v_real(in->y); return true;
    case V_xprevious: *out = v_real(in->xprevious); return true;
    case V_yprevious: *out = v_real(in->yprevious); return true;
    case V_xstart: *out = v_real(in->xstart); return true;
    case V_ystart: *out = v_real(in->ystart); return true;
    case V_hspeed: *out = v_real(in->m->hspeed); return true;
    case V_vspeed: *out = v_real(in->m->vspeed); return true;
    case V_speed: *out = v_real(in->m->speed); return true;
    case V_direction: *out = v_real(in->m->direction); return true;
    case V_friction: *out = v_real(in->m->friction); return true;
    case V_gravity: *out = v_real(in->m->gravity); return true;
    case V_gravity_direction: *out = v_real(in->m->gravity_direction); return true;
    case V_sprite_index: *out = v_real(in->sprite_index); return true;
    case V_image_index: *out = v_real(in->image_index); return true;
    case V_image_speed: *out = v_real(in->image_speed); return true;
    case V_image_xscale: *out = v_real(in->image_xscale); return true;
    case V_image_yscale: *out = v_real(in->image_yscale); return true;
    case V_image_angle: *out = v_real(in->image_angle); return true;
    case V_image_alpha: *out = v_real(in->image_alpha); return true;
    case V_image_blend: *out = v_real((gmreal_t)in->image_blend); return true;
    case V_image_single: *out = v_real(-1); return true;
    case V_image_number:
        *out = v_real(in->sprite_index >= 0 && in->sprite_index < (int)gd.nsprites ? SPR(in->sprite_index)->frames : 0);
        return true;
    case V_sprite_width:
        *out = v_real(in->sprite_index >= 0 ? SPR(in->sprite_index)->w * in->image_xscale : 0);
        return true;
    case V_sprite_height:
        *out = v_real(in->sprite_index >= 0 ? SPR(in->sprite_index)->h * in->image_yscale : 0);
        return true;
    case V_sprite_xoffset: *out = v_real(in->sprite_index >= 0 ? SPR(in->sprite_index)->ox : 0); return true;
    case V_sprite_yoffset: *out = v_real(in->sprite_index >= 0 ? SPR(in->sprite_index)->oy : 0); return true;
    case V_mask_index: *out = v_real(in->mask_index); return true;
    case V_depth: *out = v_real(in->depth); return true;
    case V_visible: *out = v_real(in->visible); return true;
    case V_solid: *out = v_real(in->solid); return true;
    case V_persistent: *out = v_real(in->persistent); return true;
    case V_object_index: *out = v_real(in->obj); return true;
    case V_id: *out = v_real((gmreal_t)in->id); return true;
    case V_alarm: *out = v_real(idx >= 0 && idx < MAX_ALARMS ? in->m->alarm[idx] : -1); return true;
    case V_bbox_left: case V_bbox_right: case V_bbox_top: case V_bbox_bottom:
        if (!inst_bbox(in, &l, &t, &r, &b))
        {
            l = r = in->x;
            t = b = in->y;
        }
        *out = v_real(floorf(id == V_bbox_left ? l : id == V_bbox_right ? r : id == V_bbox_top ? t : b));
        return true;
    case V_path_index: *out = v_real(in->m->path_index); return true;
    case V_path_position: *out = v_real(in->m->path_position); return true;
    case V_path_positionprevious: *out = v_real(in->m->path_positionprevious); return true;
    case V_path_speed: *out = v_real(in->m->path_speed); return true;
    case V_path_scale: *out = v_real(in->m->path_scale); return true;
    case V_path_orientation: *out = v_real(in->m->path_orientation); return true;
    case V_path_endaction: *out = v_real(in->m->path_endaction); return true;
    case V_timeline_index: *out = v_real(-1); return true;
    case V_timeline_position: case V_timeline_speed: case V_timeline_running: *out = v_real(0); return true;
    }
    return false;
}

bool builtin_var_set(instance_t *in, uint16_t id, int32_t idx, value_t v)
{
    gmreal_t r = v_num(v);
    switch (id)
    {
    case V_room: room_goto((int)r); return true;
    case V_room_persistent: room_set_persistent(gs.room, r != 0); return true;
    case V_room_speed: gs.room_speed = (int)r; return true;
    case V_view_xview: view_at(idx)->x = r; return true;
    case V_view_yview: view_at(idx)->y = r; return true;
    case V_view_wview: view_at(idx)->w = r; return true;
    case V_view_hview: view_at(idx)->h = r; return true;
    case V_view_xport: view_at(idx)->px = r; return true;
    case V_view_yport: view_at(idx)->py = r; return true;
    case V_view_wport: view_at(idx)->pw = r; return true;
    case V_view_hport: view_at(idx)->ph = r; return true;
    case V_view_visible: view_at(idx)->visible = r != 0; return true;
    case V_view_object: view_at(idx)->follow = (int)r; return true;
    case V_view_hborder: view_at(idx)->hborder = r; return true;
    case V_view_vborder: view_at(idx)->vborder = r; return true;
    case V_view_angle: view_at(idx)->angle = r; return true;
    case V_view_enabled: gs.views_enabled = r != 0; return true;
    case V_view_current: return true;
    case V_background_color: gs.background_color = (uint32_t)r & 0xffffff; return true;
    case V_background_showcolor: gs.background_showcolor = r != 0; return true;
    case V_background_visible: bg_at(idx)->visible = r != 0; return true;
    case V_background_index: bg_at(idx)->index = (int)r; return true;
    case V_background_x: bg_at(idx)->x = r; return true;
    case V_background_y: bg_at(idx)->y = r; return true;
    case V_background_hspeed: bg_at(idx)->hspeed = r; return true;
    case V_background_vspeed: bg_at(idx)->vspeed = r; return true;
    case V_background_alpha: bg_at(idx)->alpha = r; return true;
    case V_background_blend: bg_at(idx)->blend = (uint32_t)r & 0xffffff; return true;
    case V_background_htiled: bg_at(idx)->htiled = r != 0; return true;
    case V_background_vtiled: bg_at(idx)->vtiled = r != 0; return true;
    case V_background_xscale: bg_at(idx)->xscale = r; return true;
    case V_background_yscale: bg_at(idx)->yscale = r; return true;
    case V_score: case V_lives: case V_health: case V_keyboard_key: case V_keyboard_lastchar:
        return true;
    }
    if (!in)
    {
        return true;
    }
    switch (id)
    {
    case V_x: in->x = r; return true;
    case V_y: in->y = r; return true;
    case V_xprevious: in->xprevious = r; return true;
    case V_yprevious: in->yprevious = r; return true;
    case V_xstart: in->xstart = r; return true;
    case V_ystart: in->ystart = r; return true;
    case V_hspeed: inst_m(in)->hspeed = r; sync_from_hv(in); return true;
    case V_vspeed: inst_m(in)->vspeed = r; sync_from_hv(in); return true;
    case V_speed: set_speed_dir(in, r, in->m->direction); return true;
    case V_direction: set_speed_dir(in, in->m->speed, r); return true;
    case V_friction: inst_m(in)->friction = r; return true;
    case V_gravity: inst_m(in)->gravity = r; return true;
    case V_gravity_direction: inst_m(in)->gravity_direction = r; return true;
    case V_sprite_index: in->sprite_index = (int16_t)r; return true;
    case V_image_index: in->image_index = r; return true;
    case V_image_speed: in->image_speed = r; return true;
    case V_image_xscale: in->image_xscale = r; return true;
    case V_image_yscale: in->image_yscale = r; return true;
    case V_image_angle: in->image_angle = r; return true;
    case V_image_alpha: in->image_alpha = r; return true;
    case V_image_blend: in->image_blend = (uint32_t)r & 0xffffff; return true;
    case V_image_single:
        if (r >= 0)
        {
            in->image_index = r;
            in->image_speed = 0;
        }
        return true;
    case V_mask_index: in->mask_index = (int16_t)r; return true;
    case V_depth: in->depth = r; return true;
    case V_visible: in->visible = r != 0; return true;
    case V_solid: in->solid = r != 0; return true;
    case V_persistent: in->persistent = r != 0; return true;
    case V_alarm:
        if (idx >= 0 && idx < MAX_ALARMS)
        {
            if (in->m != &motion_default || (int16_t)r != -1)
            {
                inst_m(in)->alarm[idx] = (int16_t)r;
            }
        }
        return true;
    case V_path_position: inst_m(in)->path_position = r; return true;
    case V_path_speed: inst_m(in)->path_speed = r; return true;
    case V_path_scale: inst_m(in)->path_scale = r; return true;
    case V_path_orientation: inst_m(in)->path_orientation = r; return true;
    case V_path_endaction: inst_m(in)->path_endaction = (uint8_t)r; return true;
    case V_image_number: case V_sprite_width: case V_sprite_height: case V_object_index: case V_id:
    case V_bbox_left: case V_bbox_right: case V_bbox_top: case V_bbox_bottom:
        return true;
    }
    return false;
}
