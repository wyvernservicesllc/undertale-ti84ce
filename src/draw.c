/* Palette fades and GameMaker-style text drawing. */
#include <graphx.h>

#include "common.h"
#include "font.h"
#include "gfx/utintro.h"

struct font
{
    const uint8_t *bits;
    const glyph_t *glyphs;
    uint8_t line_h; /* line height for '#' */
};

const font_t font_main = { fnt_maintext_bits, fnt_maintext_glyphs, 16 };
const font_t font_small = { fnt_small_bits, fnt_small_glyphs, 7 };

bool alarm_tick(int16_t *a)
{
    if (*a > 0 && --*a == 0)
    {
        *a = -1;
        return true;
    }
    return false;
}

/* Scale a 1555 color by level (0-256), then wash it toward white. */
static uint16_t shade(uint16_t c, unsigned level, unsigned white)
{
    unsigned r = ((c >> 10) & 31) * level >> 8;
    unsigned g = ((c >> 5) & 31) * level >> 8;
    unsigned b = (c & 31) * level >> 8;

    r += (31 - r) * white >> 8;
    g += (31 - g) * white >> 8;
    b += (31 - b) * white >> 8;
    return (uint16_t)(r << 10 | g << 5 | b);
}

void set_palette(unsigned pic, unsigned all, unsigned white)
{
    const uint16_t *base = (const uint16_t *)global_palette;
    unsigned pic_level = pic * all >> 8;

    for (uint8_t i = 0; i < sizeof_global_palette / 2; i++)
    {
        gfx_palette[i] = shade(base[i], pic_level, white);
    }
    gfx_palette[COL_YELLOW] = shade(gfx_RGBTo1555(255, 255, 0), all, white);
    gfx_palette[COL_BLUE] = shade(gfx_RGBTo1555(0, 162, 232), all, white);
    gfx_palette[COL_RED] = shade(gfx_RGBTo1555(237, 28, 36), all, white);
    gfx_palette[COL_LTGRAY] = shade(gfx_RGBTo1555(192, 192, 192), all, white);
    gfx_palette[COL_GRAY] = shade(gfx_RGBTo1555(128, 128, 128), all, white);
    gfx_palette[COL_WHITE] = shade(gfx_RGBTo1555(255, 255, 255), all, white);
}

static const glyph_t *glyph(const font_t *f, char c)
{
    if (c < 32 || c > 126)
    {
        c = ' ';
    }
    return &f->glyphs[c - 32];
}

void draw_char(const font_t *f, char c, int x, int y, uint8_t color)
{
    const glyph_t *g = glyph(f, c);
    const uint8_t *row = f->bits + g->start;

    x += g->xoff;
    for (uint8_t j = 0; j < g->h; j++, row++)
    {
        uint8_t v = *row;
        uint8_t *dst = &gfx_vbuffer[y + j][x];
        for (uint8_t i = 0; v; i++, v <<= 1)
        {
            if (v & 0x80)
            {
                dst[i] = color;
            }
        }
    }
}

void draw_text(const font_t *f, const char *s, int x, int y, uint8_t color)
{
    int x0 = x;

    for (; *s; s++)
    {
        if (*s == '#')
        {
            x = x0;
            y += f->line_h;
            continue;
        }
        draw_char(f, *s, x, y, color);
        x += glyph(f, *s)->shift;
    }
}

int text_width(const font_t *f, const char *s)
{
    int w = 0, best = 0;

    for (; *s; s++)
    {
        if (*s == '#')
        {
            w = 0;
            continue;
        }
        w += glyph(f, *s)->shift;
        if (w > best)
        {
            best = w;
        }
    }
    return best;
}

void draw_text_centered(const font_t *f, const char *s, int x, int y, uint8_t color)
{
    draw_text(f, s, x - text_width(f, s) / 2, y, color);
}

void draw_text_transformed(const font_t *f, const char *s, int x, int y,
                           unsigned scale, int rotation, uint8_t color)
{
    unsigned pen = 0; /* 8.8 */

    gfx_SetColor(color);
    for (; *s; s++)
    {
        const glyph_t *g = glyph(f, *s);
        const uint8_t *row = f->bits + g->start;
        int gx = x + (int)(pen >> 8);

        for (uint8_t j = 0; j < g->h; j++, row++)
        {
            int y0 = y + (int)(j * scale >> 8);
            int y1 = y + (int)((j + 1) * scale >> 8);
            uint8_t v = *row;
            for (uint8_t i = 0; v; i++, v <<= 1)
            {
                if (v & 0x80)
                {
                    int x0 = gx + (int)((g->xoff + i) * scale >> 8);
                    int x1 = gx + (int)((g->xoff + i + 1) * scale >> 8);
                    /* Small-angle rotation around (x, y). The original
                       naming animation rotates by at most one degree. */
                    int rx = x0 - ((y0 - y) * rotation >> 8);
                    int ry = y0 + ((x0 - x) * rotation >> 8);
                    gfx_FillRectangle(rx, ry, x1 - x0, y1 - y0);
                }
            }
        }
        pen += g->shift * scale;
    }
}
