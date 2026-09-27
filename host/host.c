/*
 * Mac/Linux test host for the Undertale VM: runs frames headless with
 * scripted input and saves chosen frames as PPM images.
 *
 *   host <data dir> <frames> [-i input.txt] [-o outdir] [-s every] [-f frame,...]
 *        [-d savedir] [-m mash_period] [-M mash_from_frame]
 *
 * input.txt lines: "<frame> <gm keycode> <frames held>", e.g. "120 13 2"
 * holds Enter for two frames starting at frame 120. Keycodes: Z=90 X=88
 * C=67 Enter=13 arrows 37-40.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "../vm/vm.h"

static uint8_t fb[SCREEN_H][SCREEN_W][3];
static const char *save_dir = "save";

/* ---- platform ---- */

/* with HASH set, time is the frame count, as in the calculator's test
   builds, so runs can be compared */
static uint32_t fake_frame;

uint32_t plat_time_ms(void)
{
    struct timeval tv;
    if (getenv("HASH"))
    {
        return fake_frame * 33;
    }
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static void save_path(char *buf, size_t n, const char *name)
{
    snprintf(buf, n, "%s/%s", save_dir, name);
}

bool plat_file_read(const char *name, char **data, int *len)
{
    char path[512];
    FILE *f;
    long sz;
    save_path(path, sizeof path, name);
    f = fopen(path, "rb");
    if (!f)
    {
        return false;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    *data = malloc(sz + 1);
    *len = (int)fread(*data, 1, sz, f);
    (*data)[*len] = 0;
    fclose(f);
    return true;
}

bool plat_file_write(const char *name, const char *data, int len)
{
    if (getenv("LOGFILES"))
    {
        fprintf(stderr, "write %s (%d bytes) at frame %u\n", name, len, gs.frame);
    }
    char path[512];
    FILE *f;
    mkdir(save_dir, 0755);
    save_path(path, sizeof path, name);
    f = fopen(path, "wb");
    if (!f)
    {
        return false;
    }
    fwrite(data, 1, len, f);
    fclose(f);
    return true;
}

bool plat_file_exists(const char *name)
{
    char path[512];
    save_path(path, sizeof path, name);
    return access(path, F_OK) == 0;
}

void plat_file_delete(const char *name)
{
    char path[512];
    save_path(path, sizeof path, name);
    unlink(path);
}

static int log_count;

void plat_log(const char *fmt, ...)
{
    va_list ap;
    if (log_count++ > 2000)
    {
        return;
    }
    va_start(ap, fmt);
    fprintf(stderr, "[%u] ", gs.frame);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/* ---- asset trace: -t file appends "kind index" lines on exit ---- */

#ifdef VM_TRACE
static uint8_t *trace_seen[5];
static const int trace_max[5] = { 4096, 1024, 64, 1024, 8192 };
uint32_t trace_ops[8192];

void trace_use(int kind, int index)
{
    if (!trace_seen[kind])
    {
        trace_seen[kind] = calloc(trace_max[kind], 1);
    }
    if (index >= 0 && index < trace_max[kind])
    {
        trace_seen[kind][index] = 1;
    }
}

static void trace_write(const char *path)
{
    FILE *f = fopen(path, "a");
    static const char *kinds[] = { "sprite", "bg", "font", "room", "code" };
    if (!f)
    {
        return;
    }
    for (int k = 0; k < 5; k++)
    {
        for (int i = 0; trace_seen[k] && i < trace_max[k]; i++)
        {
            if (trace_seen[k][i])
            {
                fprintf(f, "%s %d\n", kinds[k], i);
            }
        }
    }
    /* how hot each code entry is (cepack.py --hot keeps the hottest code
       uncompressed, run straight from flash) */
    for (int i = 0; i < 8192; i++)
    {
        if (trace_ops[i])
        {
            fprintf(f, "ops %d %u\n", i, trace_ops[i]);
        }
    }
    fclose(f);
}
#endif

/* ---- software renderer ---- */

static inline void blend_px(int x, int y, int r, int g, int b, gmreal_t a)
{
    uint8_t *p;
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H)
    {
        return;
    }
    p = fb[y][x];
    if (a >= 1)
    {
        p[0] = (uint8_t)r;
        p[1] = (uint8_t)g;
        p[2] = (uint8_t)b;
    }
    else if (a > 0)
    {
        p[0] = (uint8_t)(p[0] + (r - p[0]) * a);
        p[1] = (uint8_t)(p[1] + (g - p[1]) * a);
        p[2] = (uint8_t)(p[2] + (b - p[2]) * a);
    }
}

void render_begin(uint32_t clear_color, bool clear)
{
    uint32_t c = clear ? clear_color : 0;
    for (int y = 0; y < SCREEN_H; y++)
    {
        for (int x = 0; x < SCREEN_W; x++)
        {
            fb[y][x][0] = GM_R(c);
            fb[y][x][1] = GM_G(c);
            fb[y][x][2] = GM_B(c);
        }
    }
}

void render_end(void)
{
}

/* Map screen pixel centers back into a transformed image; calls plot()
   for each covered source pixel. */
typedef void (*plot_fn)(int dx, int dy, int u, int v, void *ctx);

static void transform_blit(int sw, int sh, gmreal_t x, gmreal_t y, gmreal_t xs, gmreal_t ys, gmreal_t angle,
                           gmreal_t ox, gmreal_t oy, plot_fn plot, void *ctx)
{
    gmreal_t a = angle * 3.14159265f / 180, c = cosf(a), s = sinf(a);
    gmreal_t cx[4], cy[4];
    gmreal_t minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    gmreal_t lx[4] = { -ox * xs, (sw - ox) * xs, -ox * xs, (sw - ox) * xs };
    gmreal_t ly[4] = { -oy * ys, -oy * ys, (sh - oy) * ys, (sh - oy) * ys };
    if (xs == 0 || ys == 0)
    {
        return;
    }
    x = (x + render_ox) * render_scale;
    y = (y + render_oy) * render_scale;
    xs *= render_scale;
    ys *= render_scale;
    for (int i = 0; i < 4; i++)
    {
        cx[i] = x + lx[i] * c + ly[i] * s;
        cy[i] = y - lx[i] * s + ly[i] * c;
        if (cx[i] < minx) minx = cx[i];
        if (cx[i] > maxx) maxx = cx[i];
        if (cy[i] < miny) miny = cy[i];
        if (cy[i] > maxy) maxy = cy[i];
    }
    {
        int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > SCREEN_W) x1 = SCREEN_W;
        if (y1 > SCREEN_H) y1 = SCREEN_H;
        for (int py = y0; py < y1; py++)
        {
            for (int px = x0; px < x1; px++)
            {
                gmreal_t dx = px + 0.5f - x, dy = py + 0.5f - y;
                gmreal_t rx = dx * c - dy * s, ry = dx * s + dy * c;
                gmreal_t u = rx / xs + ox, v = ry / ys + oy;
                if (u >= 0 && v >= 0 && u < sw && v < sh)
                {
                    plot(px, py, (int)u, (int)v, ctx);
                }
            }
        }
    }
}

typedef struct
{
    far_t img;
    int w, sx, sy;
    uint32_t blend;
    gmreal_t alpha;
    int row_y;
    uint8_t row[2048];
} blit_ctx_t;

static void plot_image(int dx, int dy, int u, int v, void *p)
{
    blit_ctx_t *c = p;
    uint8_t idx;
    const uint8_t *rgb;
    if (c->row_y != c->sy + v)
    {
        c->row_y = c->sy + v;
        img_decode_row(c->img, c->row_y, c->row, c->w);
    }
    idx = c->row[c->sx + u];
    if (!idx)
    {
        return;
    }
    rgb = far_ptr(gd.palette) + idx * 3;
    blend_px(dx, dy, rgb[0] * GM_R(c->blend) / 255, rgb[1] * GM_G(c->blend) / 255,
             rgb[2] * GM_B(c->blend) / 255, c->alpha);
}

void render_image(far_t img, int w, int h, int sx, int sy, int sw, int sh,
                  gmreal_t x, gmreal_t y, gmreal_t xscale, gmreal_t yscale, gmreal_t angle,
                  gmreal_t ox, gmreal_t oy, uint32_t blend, gmreal_t alpha)
{
    static blit_ctx_t c;
    (void)h;
    if (getenv("LOGIMG") && gs.frame >= (unsigned)atoi(getenv("LOGIMG")) && gs.frame < (unsigned)atoi(getenv("LOGIMG")) + 2)
    {
        fprintf(stderr, "img f%u %dx%d src %d,%d %dx%d at %.1f,%.1f scale %.2f,%.2f ang %.1f blend %06x alpha %.2f\n",
                gs.frame, w, h, sx, sy, sw, sh, x, y, xscale, yscale, angle, blend, alpha);
    }
    if (w > 2048)
    {
        return;
    }
    c.img = img;
    c.w = w;
    c.sx = sx;
    c.sy = sy;
    c.blend = blend;
    c.alpha = alpha;
    c.row_y = -1;
    transform_blit(sw, sh, x, y, xscale, yscale, angle, ox, oy, plot_image, &c);
}

typedef struct
{
    const uint8_t *bits;
    int rowb;
    uint32_t color;
    gmreal_t alpha;
} glyph_ctx_t;

static void plot_glyph(int dx, int dy, int u, int v, void *p)
{
    glyph_ctx_t *c = p;
    if (c->bits[v * c->rowb + (u >> 3)] & (0x80 >> (u & 7)))
    {
        blend_px(dx, dy, GM_R(c->color), GM_G(c->color), GM_B(c->color), c->alpha);
    }
}

void render_glyph(far_t bitmap, int gw, int gh, gmreal_t x, gmreal_t y, gmreal_t xscale, gmreal_t yscale,
                  gmreal_t angle, uint32_t color, gmreal_t alpha)
{
    glyph_ctx_t c = { far_ptr(bitmap), (gw + 7) / 8, color, alpha };
    if (getenv("LOGIMG") && gs.frame == (unsigned)atoi(getenv("LOGIMG")))
    {
        fprintf(stderr, "glyph %dx%d at %.2f,%.2f scale %.2f,%.2f (render %.2f) ang %.1f alpha %.2f\n", gw, gh, x, y, xscale, yscale,
                render_scale, angle, alpha);
    }
    transform_blit(gw, gh, x, y, xscale, yscale, angle, 0, 0, plot_glyph, &c);

}

void render_glyph_px(far_t bitmap, int gw, int gh, int left, int top, int dw, int dh, uint32_t color, gmreal_t alpha)
{
    gmreal_t ox = render_ox, oy = render_oy, sc = render_scale;
    render_ox = render_oy = 0;
    render_scale = 1;
    render_glyph(bitmap, gw, gh, (gmreal_t)left, (gmreal_t)top, (gmreal_t)dw / gw, (gmreal_t)dh / gh, 0, color, alpha);
    render_ox = ox;
    render_oy = oy;
    render_scale = sc;
}

void render_glyph_screen(far_t bitmap, int gw, int gh, int x, int y, uint32_t color, gmreal_t alpha)
{
    gmreal_t ox = render_ox, oy = render_oy, sc = render_scale;
    render_ox = render_oy = 0;
    render_scale = 1;
    render_glyph(bitmap, gw, gh, (gmreal_t)x, (gmreal_t)y, 1, 1, 0, color, alpha);
    render_ox = ox;
    render_oy = oy;
    render_scale = sc;
}

void render_rect(gmreal_t x1, gmreal_t y1, gmreal_t x2, gmreal_t y2, uint32_t color, gmreal_t alpha, bool outline)
{
    if (getenv("LOGIMG") && gs.frame == (unsigned)atoi(getenv("LOGIMG")))
    {
        fprintf(stderr, "rect %.1f,%.1f-%.1f,%.1f color %06x alpha %.2f (render %.2f ox %.1f,%.1f)\n", x1, y1, x2, y2,
                color, alpha, render_scale, render_ox, render_oy);
    }
    int a, b, c, d;
    if (x1 > x2) { gmreal_t t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { gmreal_t t = y1; y1 = y2; y2 = t; }
    a = (int)floorf((x1 + render_ox) * render_scale);
    b = (int)floorf((y1 + render_oy) * render_scale);
    c = (int)floorf((x2 + 1 + render_ox) * render_scale) - 1;
    d = (int)floorf((y2 + 1 + render_oy) * render_scale) - 1;
    if (c < a) c = a;
    if (d < b) d = b;
    for (int y = b; y <= d; y++)
    {
        if (y < 0 || y >= SCREEN_H)
        {
            continue;
        }
        for (int x = a; x <= c; x++)
        {
            if (outline && y != b && y != d && x != a && x != c)
            {
                continue;
            }
            blend_px(x, y, GM_R(color), GM_G(color), GM_B(color), alpha);
        }
    }
}

void render_line(gmreal_t x1, gmreal_t y1, gmreal_t x2, gmreal_t y2, gmreal_t width, uint32_t color, gmreal_t alpha)
{
    gmreal_t dx, dy;
    int steps, hw;
    x1 = (x1 + render_ox) * render_scale;
    y1 = (y1 + render_oy) * render_scale;
    x2 = (x2 + render_ox) * render_scale;
    y2 = (y2 + render_oy) * render_scale;
    width *= render_scale;
    dx = x2 - x1;
    dy = y2 - y1;
    steps = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy)) + 1;
    hw = (int)(width / 2);
    for (int i = 0; i <= steps; i++)
    {
        gmreal_t f = (gmreal_t)i / steps;
        int px = (int)floorf(x1 + dx * f), py = (int)floorf(y1 + dy * f);
        for (int oy = -hw; oy <= hw; oy++)
        {
            for (int ox = -hw; ox <= hw; ox++)
            {
                blend_px(px + ox, py + oy, GM_R(color), GM_G(color), GM_B(color), alpha);
            }
        }
    }
}

void render_circle(gmreal_t x, gmreal_t y, gmreal_t r, uint32_t color, gmreal_t alpha, bool outline)
{
    int x0, x1, y0, y1;
    x = (x + render_ox) * render_scale;
    y = (y + render_oy) * render_scale;
    r *= render_scale;
    x0 = (int)floorf(x - r);
    x1 = (int)ceilf(x + r);
    y0 = (int)floorf(y - r);
    y1 = (int)ceilf(y + r);
    for (int py = y0; py <= y1; py++)
    {
        for (int px = x0; px <= x1; px++)
        {
            gmreal_t dx = px + 0.5f - x, dy = py + 0.5f - y;
            gmreal_t d = sqrtf(dx * dx + dy * dy);
            if (d <= r && (!outline || d >= r - 1))
            {
                blend_px(px, py, GM_R(color), GM_G(color), GM_B(color), alpha);
            }
        }
    }
}

void render_triangle(gmreal_t x1, gmreal_t y1, gmreal_t x2, gmreal_t y2, gmreal_t x3, gmreal_t y3, uint32_t color,
                     gmreal_t alpha, bool outline)
{
    if (outline)
    {
        render_line(x1, y1, x2, y2, 1, color, alpha);
        render_line(x2, y2, x3, y3, 1, color, alpha);
        render_line(x3, y3, x1, y1, 1, color, alpha);
        return;
    }
    x1 = (x1 + render_ox) * render_scale; y1 = (y1 + render_oy) * render_scale;
    x2 = (x2 + render_ox) * render_scale; y2 = (y2 + render_oy) * render_scale;
    x3 = (x3 + render_ox) * render_scale; y3 = (y3 + render_oy) * render_scale;
    {
        gmreal_t minx = fminf(x1, fminf(x2, x3)), maxx = fmaxf(x1, fmaxf(x2, x3));
        gmreal_t miny = fminf(y1, fminf(y2, y3)), maxy = fmaxf(y1, fmaxf(y2, y3));
        for (int py = (int)floorf(miny); py <= (int)ceilf(maxy); py++)
        {
            for (int px = (int)floorf(minx); px <= (int)ceilf(maxx); px++)
            {
                gmreal_t qx = px + 0.5f, qy = py + 0.5f;
                gmreal_t d1 = (qx - x2) * (y1 - y2) - (x1 - x2) * (qy - y2);
                gmreal_t d2 = (qx - x3) * (y2 - y3) - (x2 - x3) * (qy - y3);
                gmreal_t d3 = (qx - x1) * (y3 - y1) - (x3 - x1) * (qy - y1);
                bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
                if (!(neg && pos))
                {
                    blend_px(px, py, GM_R(color), GM_G(color), GM_B(color), alpha);
                }
            }
        }
    }
}

/* ---- main ---- */

static uint8_t *load_file(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    long sz;
    uint8_t *buf;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (!f)
    {
        fprintf(stderr, "can't open %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(sz);
    if (fread(buf, 1, sz, f) != (size_t)sz)
    {
        exit(1);
    }
    fclose(f);
    return buf;
}

static void save_frame(const char *dir, uint32_t n)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/f%05u.ppm", dir, n);
    f = fopen(path, "wb");
    if (!f)
    {
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    fwrite(fb, 1, sizeof fb, f);
    fclose(f);
}

typedef struct
{
    uint32_t frame;
    int key, hold;
} press_t;


/* ---- sound and a window to play in (make play: -DHOST_SDL) ---- */

#ifdef HOST_SDL
#include <SDL.h>
#include <SDL_mixer.h>

#define MAX_SOUNDS 1024
#define CHANNELS 32
static Mix_Chunk *chunks[MAX_SOUNDS];
static char *sound_files[MAX_SOUNDS];
static int chan_handle[CHANNELS], chan_sound[CHANNELS], next_handle = 1000000;
static bool audio_on;
static const char *sound_dir = "../data/sounds";

static void sound_init(void)
{
    char path[600];
    FILE *f;
    int i;
    char name[256];
    if (Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 1024) < 0)
    {
        fprintf(stderr, "no audio: %s\n", Mix_GetError());
        return;
    }
    Mix_AllocateChannels(CHANNELS);
    snprintf(path, sizeof path, "%s/index.txt", sound_dir);
    f = fopen(path, "r");
    if (!f)
    {
        fprintf(stderr, "no sounds in %s\n", sound_dir);
        return;
    }
    while (fscanf(f, "%d %255s", &i, name) == 2)
    {
        if (i >= 0 && i < MAX_SOUNDS)
        {
            sound_files[i] = strdup(name);
        }
    }
    fclose(f);
    audio_on = true;
}

int plat_sound_play(int sound, bool loop, gmreal_t priority)
{
    int ch;
    (void)priority;
    if (!audio_on || sound < 0 || sound >= MAX_SOUNDS || !sound_files[sound])
    {
        return -1;
    }
    if (!chunks[sound])
    {
        char path[600];
        snprintf(path, sizeof path, "%s/%s", sound_dir, sound_files[sound]);
        chunks[sound] = Mix_LoadWAV(path);
        if (!chunks[sound])
        {
            return -1;
        }
    }
    ch = Mix_PlayChannel(-1, chunks[sound], loop ? -1 : 0);
    if (ch < 0)
    {
        return -1;
    }
    Mix_Volume(ch, MIX_MAX_VOLUME);
    chan_handle[ch] = next_handle++;
    chan_sound[ch] = sound;
    return chan_handle[ch];
}

/* the channels a handle (one playing sound) or a sound index (all its
   playing copies) means */
static bool chan_match(int ch, int h)
{
    return Mix_Playing(ch) && (h >= 1000000 ? chan_handle[ch] == h : chan_sound[ch] == h);
}

void plat_sound_stop(int h)
{
    for (int ch = 0; audio_on && ch < CHANNELS; ch++)
    {
        if (chan_match(ch, h))
        {
            Mix_HaltChannel(ch);
        }
    }
}

void plat_sound_stop_all(void)
{
    if (audio_on)
    {
        Mix_HaltChannel(-1);
    }
}

void plat_sound_gain(int h, gmreal_t gain, int ms)
{
    (void)ms;
    for (int ch = 0; audio_on && ch < CHANNELS; ch++)
    {
        if (chan_match(ch, h))
        {
            Mix_Volume(ch, (int)(gain < 0 ? 0 : gain > 1 ? MIX_MAX_VOLUME : gain * MIX_MAX_VOLUME));
        }
    }
}

void plat_sound_pitch(int h, gmreal_t pitch)
{
    (void)h;
    (void)pitch;
}

bool plat_sound_playing(int h)
{
    for (int ch = 0; audio_on && ch < CHANNELS; ch++)
    {
        if (chan_match(ch, h))
        {
            return true;
        }
    }
    return false;
}

void plat_sound_pause(int h, bool pause)
{
    for (int ch = 0; audio_on && ch < CHANNELS; ch++)
    {
        if (chan_match(ch, h))
        {
            if (pause)
            {
                Mix_Pause(ch);
            }
            else
            {
                Mix_Resume(ch);
            }
        }
    }
}

/* GameMaker key codes for the keys Undertale reads */
static int vk_of(SDL_Keycode k)
{
    switch (k)
    {
    case SDLK_LEFT: return 37;
    case SDLK_UP: return 38;
    case SDLK_RIGHT: return 39;
    case SDLK_DOWN: return 40;
    case SDLK_RETURN: return 13;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return 16;
    case SDLK_LCTRL: case SDLK_RCTRL: return 17;
    case SDLK_ESCAPE: return 27;
    case SDLK_BACKSPACE: return 8;
    case SDLK_SPACE: return 32;
    default:
        if (k >= 'a' && k <= 'z')
        {
            return (int)(k - 'a' + 'A');
        }
        if (k >= '0' && k <= '9')
        {
            return (int)k;
        }
        return -1;
    }
}

/* plays in a window at 30 frames a second until it is closed */
static int play_loop(void)
{
    SDL_Window *win = SDL_CreateWindow("Undertale (VM)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W * 3,
                                       SCREEN_H * 3, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *r = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    static uint8_t held[256];
    uint32_t next = SDL_GetTicks();
    SDL_RenderSetLogicalSize(r, SCREEN_W, SCREEN_H);
    while (!gs.end_game)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)
            {
                return 0;
            }
            if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && !e.key.repeat)
            {
                int vk = vk_of(e.key.keysym.sym);
                if (vk >= 0)
                {
                    held[vk] = e.type == SDL_KEYDOWN;
                }
            }
        }
        input_update(held);
        game_frame();
        SDL_UpdateTexture(tex, NULL, fb, SCREEN_W * 3);
        SDL_RenderClear(r);
        SDL_RenderCopy(r, tex, NULL, NULL);
        SDL_RenderPresent(r);
        next += 1000 / 30;
        {
            uint32_t now = SDL_GetTicks();
            if ((int32_t)(next - now) > 0)
            {
                SDL_Delay(next - now);
            }
            else
            {
                next = now;
            }
        }
    }
    return 0;
}
#else
int plat_sound_play(int sound, bool loop, gmreal_t priority)
{
    (void)sound;
    (void)loop;
    (void)priority;
    return -1;
}
void plat_sound_stop(int h) { (void)h; }
void plat_sound_stop_all(void) {}
void plat_sound_gain(int h, gmreal_t g, int ms) { (void)h; (void)g; (void)ms; }
void plat_sound_pitch(int h, gmreal_t p) { (void)h; (void)p; }
bool plat_sound_playing(int h) { (void)h; return false; }
void plat_sound_pause(int h, bool p) { (void)h; (void)p; }
#endif

/* Test hooks: WARP=frame:room_name goes to that room at that frame;
   BATTLE=frame:group starts that battle group (as the game's encounter
   code does: global.battlegroup, then an obj_battler). */
static void trace_clear(void)
{
    for (int k = 0; k < 5; k++)
    {
        if (trace_seen[k])
        {
            memset(trace_seen[k], 0, trace_max[k]);
        }
    }
}

static void test_hooks(uint32_t n)
{
    const char *w = getenv("WARP"), *b = getenv("BATTLE"), *l = getenv("LIST"), *sv = getenv("SAVEAT");
    unsigned f;
    if (sv)
    {
        /* SAVEAT=frame:script: run the game's save script (scr_save's
           index) then, as a save point does */
        int script;
        if (sscanf(sv, "%u:%d", &f, &script) == 2 && f == n && ninstances)
        {
            v_release(vm_call_script(script, instances[0], NULL, 0, NULL));
            fprintf(stderr, "saved at frame %u in %s\n", n, far_name(ROOM(gs.room)->name));
        }
    }
    if (l && ((unsigned)atoi(l) == n || (strchr(l, '-') && n >= (unsigned)atoi(l) && n <= (unsigned)atoi(strchr(l, '-') + 1))))
    {
        int hg = data_find_global("hurtanim"), dg = data_find_global("damagetimer");
        fprintf(stderr, "frame %u: hurtanim[0] %g damagetimer %g\n", n, v_num(arr_get(globals[hg], 0)), v_num(globals[dg]));
        /* LIST=frame: the instances then */
        for (int i = 0; i < ninstances; i++)
        {
            fprintf(stderr, "  %d obj%d (%.1f,%.1f) alarms", instances[i]->id, instances[i]->obj, instances[i]->x,
                    instances[i]->y);
            for (int a = 0; a < MAX_ALARMS; a++)
            {
                if (instances[i]->m->alarm[a] >= 0)
                {
                    fprintf(stderr, " %d:%d", a, instances[i]->m->alarm[a]);
                }
            }
            fprintf(stderr, "\n");
        }
    }
    if (w)
    {
        char name[64];
        if (sscanf(w, "%u:%63s", &f, name) == 2 && f == n)
        {
            for (uint32_t r = 0; r < gd.nrooms; r++)
            {
                if (!strcmp(far_name(ROOM(r)->name), name))
                {
                    room_goto((int)r);
                    trace_clear(); /* traces: only what that room uses */
                    fprintf(stderr, "warp to %s at frame %u\n", name, n);
                }
            }
        }
    }
    if (b)
    {
        int group;
        if (sscanf(b, "%u:%d", &f, &group) == 2 && f == n)
        {
            trace_clear();
            vm_test_battle(group);
            fprintf(stderr, "battle group %d at frame %u\n", group, n);
        }
    }
}

int main(int argc, char **argv)
{
    native_enabled = !getenv("NONATIVE");
    const char *dir, *outdir = "frames", *input = NULL, *frames_list = NULL, *trace_path = NULL;
    uint32_t nframes, every = 0, mash = 0, mash_from = 0;
    press_t presses[4096];
    int npress = 0;
    uint8_t held[256];

    if (argc < 3)
    {
        fprintf(stderr, "usage: host <data dir> <frames> [-i input] [-o outdir] [-s every] [-f a,b,c] [-d savedir]\n");
        return 1;
    }
    dir = argv[1];
    nframes = (uint32_t)atoi(argv[2]);
    for (int i = 3; i + 1 < argc; i += 2)
    {
        if (!strcmp(argv[i], "-i")) input = argv[i + 1];
        else if (!strcmp(argv[i], "-o")) outdir = argv[i + 1];
        else if (!strcmp(argv[i], "-s")) every = (uint32_t)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-f")) frames_list = argv[i + 1];
        else if (!strcmp(argv[i], "-d")) save_dir = argv[i + 1];
        else if (!strcmp(argv[i], "-m")) mash = (uint32_t)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-t")) trace_path = argv[i + 1];
        else if (!strcmp(argv[i], "-M")) mash_from = (uint32_t)atoi(argv[i + 1]);
    }
    if (input)
    {
        FILE *f = fopen(input, "r");
        unsigned fr;
        int k, h;
        while (f && npress < 4096 && fscanf(f, "%u %d %d", &fr, &k, &h) == 3)
        {
            presses[npress].frame = fr;
            presses[npress].key = k;
            presses[npress].hold = h;
            npress++;
        }
        if (f)
        {
            fclose(f);
        }
    }
    mkdir(outdir, 0755);

    /* the pack: <dir>/<prefix>000.bin ... */
    {
        const char *prefix = getenv("PACK") ? getenv("PACK") : "UTD";
        for (int w = 0; w < PACK_MAX_WINDOWS; w++)
        {
            char name[64], path[600];
            FILE *f;
            snprintf(name, sizeof name, "%s%03d.bin", prefix, w);
            snprintf(path, sizeof path, "%s/%s", dir, name);
            f = fopen(path, "rb");
            if (!f)
            {
                break;
            }
            fclose(f);
            pack_win[w] = load_file(dir, name);
        }
        pack_cache_mem = malloc(CODE_CACHE_BLOCKS * CODE_BLOCK);
        if (!pack_init())
        {
            fprintf(stderr, "bad or incomplete pack in %s\n", dir);
            return 1;
        }
    }
#ifdef HOST_SDL
    if (getenv("PLAY") || nframes == 0)
    {
        /* host <pack dir> 0: play in a window */
        if (getenv("SOUNDS"))
        {
            sound_dir = getenv("SOUNDS");
        }
        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
        sound_init();
        game_start();
        return play_loop();
    }
#endif
    game_start();
    for (uint32_t n = 0; n < nframes && !gs.end_game; n++)
    {
        memset(held, 0, sizeof held);
        for (int i = 0; i < npress; i++)
        {
            if (n >= presses[i].frame && n < presses[i].frame + (uint32_t)presses[i].hold)
            {
                held[presses[i].key & 255] = 1;
            }
        }
        /* -m N: press Z for 2 frames every N frames (after frame -M) */
        if (mash && n >= mash_from && n % mash < 2)
        {
            held['Z'] = 1;
        }
        input_update(held);
        {
            extern unsigned vm_ops;
            static uint32_t maxops, total;
            static uint32_t hist[8];
            uint32_t before = vm_ops;
            if (getenv("HASH"))
            {
                /* HASH=a,b,...: the state hash before those frames (as the
                   calculator's `make TEST=<frame>` reports it) */
                char key[16];
                snprintf(key, sizeof key, ",%u,", n);
                char list[256];
                snprintf(list, sizeof list, ",%s,", getenv("HASH"));
                if (strstr(list, key))
                {
                    if (getenv("DETAIL"))
                    {
                        printf("globals %08x\n", vm_part_hash(-1));
                        for (int i = 0; i < ninstances; i++)
                        {
                            printf("inst %d %08x\n", instances[i]->id, vm_part_hash(i));
                        }
                        if (getenv("DETAIL_INST"))
                        {
                            static uint32_t d[600];
                            int nd = vm_inst_dump(atoi(getenv("DETAIL_INST")), d, 600);
                            for (int k = 0; k < nd; k += 3)
                            {
                                printf("var %u %u %08x\n", d[k], d[k + 1], d[k + 2]);
                            }
                        }
                    }
                    printf("hash %08x at frame %u\n", vm_state_hash(), n);
                    if (getenv("MEM")) { void vm_mem_report(void); vm_mem_report(); }
                }
            }
            fake_frame = n + 1;
            test_hooks(n);
            game_frame();
            if (getenv("PROFILE"))
            {
                uint32_t d = vm_ops - before;
                total += d;
                if (d > maxops) maxops = d;
                hist[d < 1000 ? 0 : d < 3000 ? 1 : d < 10000 ? 2 : d < 30000 ? 3 : d < 100000 ? 4 : 5]++;
                if (n % 500 == 499 || n + 1 == nframes)
                {
                    printf("frames %u-%u: avg %u ops/frame, max %u; <1k %u <3k %u <10k %u <30k %u <100k %u more %u  room %s\n",
                           n - 499, n, total / 500, maxops, hist[0], hist[1], hist[2], hist[3], hist[4], hist[5],
                           far_name(ROOM(gs.room)->name));
                    total = maxops = 0;
                    memset(hist, 0, sizeof hist);
                }
            }
        }
        {
            bool dump = every && n % every == 0;
            if (frames_list)
            {
                char buf[16];
                snprintf(buf, sizeof buf, "%u", n);
                for (const char *p = frames_list; p && *p;)
                {
                    size_t l = strcspn(p, ",");
                    if (l == strlen(buf) && !strncmp(p, buf, l))
                    {
                        dump = true;
                    }
                    p += l;
                    if (*p == ',')
                    {
                        p++;
                    }
                }
            }
            if (dump)
            {
                save_frame(outdir, n);
            }
        }
    }
    if (gs.missing_room >= 0)
    {
        printf("stopped: room %d is not in this pack\n", gs.missing_room);
    }
#ifdef VM_TRACE
    if (trace_path)
    {
        trace_write(trace_path);
    }
#else
    (void)trace_path;
#endif
    if (getenv("VMSTAT"))
    {
        extern int vm_hw_stack, vm_hw_args, vm_hw_with;
        printf("vm stack %d, args %d, with %d\n", vm_hw_stack, vm_hw_args, vm_hw_with);
    }
    printf("ran %u frames, room %s, %d instances\n", gs.frame, far_name(ROOM(gs.room)->name), ninstances);
    if (getenv("DUMP_INST"))
    {
        for (int i = 0; i < ninstances; i++)
        {
            instance_t *in = instances[i];
            printf("  %d %s x=%.1f y=%.1f depth=%.0f vis=%d spr=%d nvars=%d\n", in->id,
                   "obj", in->x, in->y, in->depth, in->visible, in->sprite_index, in->nvars);
            if (getenv("DUMP_VARS") && atoi(getenv("DUMP_VARS")) == in->obj)
            {
                for (int v = 0; v < in->nvars; v++)
                {
                    value_t val = in->var_vals[v];
                    int id = in->var_ids[v];
                    char name[16];
                    snprintf(name, sizeof name, "var%d", id);
                    if (val.t == VT_REAL) printf("      %s = %g\n", name, val.u.r);
                    else if (v_is_str(val)) printf("      %s = \"%s\"\n", name, v_cstring(val));
                    else if (val.t == VT_ARRAY) printf("      %s = array[%d]\n", name, val.u.a->n);
                    else printf("      %s = undefined\n", name);
                }
            }
        }
    }
    return 0;
}
