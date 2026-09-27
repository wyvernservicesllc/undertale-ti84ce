/*
 * room_introstory and room_introimage (the title), frame for frame from the
 * game's GML: obj_introimage, obj_introfader, obj_introlast, obj_unfader,
 * OBJ_WRITER (typer 11) and obj_titleimage.
 *
 * Fading is done with the palette instead of blending: the picture colors
 * are scaled toward black the same way GameMaker's black fader rectangles
 * darken them, and the text colors only fade with the full-screen unfader.
 * Both rooms draw straight to the screen and only redraw what changed.
 */
#include <string.h>

#include <compression.h>
#include <graphx.h>
#include <sys/lcd.h>

#include "common.h"
#include "gfx/utintro.h"
#include "intro_data.h"

/* obj_introtangle leaves only this window of the 320x240 view visible. */
#define WIN_X 60
#define WIN_Y 30
#define WIN_W 200
#define WIN_H 110

/* OBJ_WRITER at (40, 140), typer 11: fnt_maintext, x+20, y+20, wraps past
   x 290, textspeed 2, 9 px per letter, 18 px per line. */
#define TXT_X 60
#define TXT_Y 160
#define TXT_XEND 290
#define TXT_SPEED 2
#define TXT_SPACING 9
#define TXT_VSPACING 18

#define LAST_H 350 /* spr_introlast height */

static uint8_t panel[WIN_W * WIN_H];

/* The 200x350 final panel is unpacked into the half of VRAM that is not
   being shown. */
static uint8_t *store(void)
{
    uint8_t *vram = (uint8_t *)lcd_Ram;
    return lcd_UpBase == (uint24_t)lcd_Ram ? vram + LCD_WIDTH * LCD_HEIGHT : vram;
}

static void blit_window(const uint8_t *src)
{
    for (uint8_t y = 0; y < WIN_H; y++)
    {
        memcpy(&gfx_vbuffer[WIN_Y + y][WIN_X], src + y * WIN_W, WIN_W);
    }
}

static void clear_text_area(void)
{
    gfx_SetColor(0);
    gfx_FillRectangle_NoClip(0, WIN_Y + WIN_H, LCD_WIDTH, LCD_HEIGHT - (WIN_Y + WIN_H));
}

/* ---- OBJ_WRITER ---- */

static struct
{
    bool alive;
    uint8_t no;          /* stringno */
    const char *s;       /* originalstring */
    uint8_t len;
    uint8_t pos;         /* stringpos */
    int16_t alarm;
    uint8_t drawn;       /* letters of this string already on screen */
} wr;

static uint8_t faceemotion;

static void writer_start_string(uint8_t no)
{
    wr.no = no;
    wr.s = intro_msg[no];
    wr.len = strlen(wr.s);
    wr.pos = 0;
    wr.drawn = 0;
}

static void writer_create(void)
{
    wr.alive = true;
    writer_start_string(0);
    wr.alarm = TXT_SPEED;
}

static void writer_destroy(void)
{
    wr.alive = false;
    clear_text_area();
}

/* obj_base_writer Alarm 0: reveal the next letter, honoring ^n pauses. */
static void writer_advance(void)
{
    const char *s = wr.s;
    uint8_t advance = 1;
    int16_t delay = TXT_SPEED;

    if (wr.pos >= wr.len)
    {
        return;
    }
    while (wr.pos < wr.len && advance > 0)
    {
        char ch = s[wr.pos++];
        if (ch == '^')
        {
            ch = s[wr.pos++];
            if (ch != '0')
            {
                delay = (ch - '0') * 10;
                advance = 1;
            }
        }
        else if (ch == '\\')
        {
            ch = s[wr.pos++];
            if (ch == 'S' || ch == 'E' || ch == 'F' || ch == 'M' || ch == 'T' || ch == '*')
            {
                wr.pos++;
            }
            else if (ch == 'z')
            {
                wr.pos++;
                advance--;
            }
        }
        else if (ch != '/' && ch != '%' && ch != '&')
        {
            advance--;
        }
    }
    wr.alarm = delay;
}

/* obj_base_writer Draw: walk the revealed part of the string, apply its
   commands and draw letters that are not on screen yet. */
static void writer_draw(void)
{
    const char *s = wr.s;
    int x = TXT_X, y = TXT_Y;
    uint8_t letter = 0;

    for (uint8_t n = 0; n < wr.pos; n++)
    {
        char ch = s[n];
        if (ch == '^' && s[n + 1] != '0')
        {
            n++;
        }
        else if (ch == '\\')
        {
            ch = s[++n];
            if (ch == 'E')
            {
                faceemotion = s[++n] - '0';
            }
            else if (ch == 'M' || ch == 'F' || ch == 'S' || ch == 'T' || ch == 'z' || ch == '*')
            {
                n++;
            }
        }
        else if (ch == '&')
        {
            x = TXT_X;
            y += TXT_VSPACING;
        }
        else if (ch == '%')
        {
            if (s[n + 1] == '%')
            {
                writer_destroy();
                return;
            }
            writer_start_string(wr.no + 1);
            wr.alarm = TXT_SPEED;
            clear_text_area();
            return;
        }
        else
        {
            if (x > TXT_XEND)
            {
                x = TXT_X;
                y += TXT_VSPACING;
            }
            if (letter >= wr.drawn && ch > ' ' && ch < 127)
            {
                draw_char(&font_main, ch, x, y, COL_WHITE);
            }
            letter++;
            x += TXT_SPACING;
        }
    }
    wr.drawn = letter;
}

/* ---- room_introstory ---- */

#define MAX_FADERS 4

static struct
{
    /* obj_introimage */
    int16_t alarm[3];
    bool act, skip, dongs, visible;
    uint8_t image_index, shown_index, fadercreator;
    /* obj_introfader instances; alpha in tenths */
    struct { bool alive, over; int8_t alpha; int16_t alarm2, alarm3; } fader[MAX_FADERS];
    /* obj_unfader; alpha in hundredths */
    bool unfader;
    int16_t unfader_alpha;
    /* obj_introlast */
    bool last, last_visible, go;
    int16_t last_alarm0, last_alarm2, h, last_top;
} in;

void intro_begin(void)
{
    memset(&in, 0, sizeof in);
    in.alarm[0] = in.alarm[1] = -1;
    in.alarm[2] = 4;
    in.shown_index = 0xff;
    in.last_top = -1;
    wr.alive = false;
    faceemotion = 0;
    gfx_FillScreen(0);
}

static void create_fader(void)
{
    for (uint8_t i = 0; i < MAX_FADERS; i++)
    {
        if (!in.fader[i].alive)
        {
            in.fader[i].alive = true;
            in.fader[i].over = false;
            in.fader[i].alpha = 0;
            in.fader[i].alarm2 = in.fader[i].alarm3 = -1;
            return;
        }
    }
}

static void start_skip(void)
{
    in.skip = true;
    in.unfader = true;
    in.unfader_alpha = 0;
    in.alarm[1] = 30;
}

/* Returns false when the room is over and the title should start. */
bool intro_step(void)
{
    /* Begin step (obj_introimage Step 1). */
    if (in.act)
    {
        if (!wr.alive && !in.skip)
        {
            start_skip();
        }
        if (faceemotion == 2 && !in.dongs)
        {
            in.dongs = true;
            in.last = true;
            in.h = 10;
            in.last_alarm0 = 150;
            in.last_alarm2 = 20;
            zx0_Decompress(store(), last0_compressed);
            zx0_Decompress(store() + last0_width * last0_height, last1_compressed);
        }
    }

    /* Alarms. */
    if (alarm_tick(&in.alarm[0]))
    {
        if (in.fadercreator != faceemotion)
        {
            create_fader();
        }
        in.alarm[0] = 3;
        in.fadercreator = faceemotion;
    }
    if (alarm_tick(&in.alarm[1]))
    {
        return false;
    }
    if (alarm_tick(&in.alarm[2]))
    {
        in.visible = true;
        in.act = true;
        writer_create();
        in.alarm[0] = 5;
    }
    for (uint8_t i = 0; i < MAX_FADERS; i++)
    {
        if (!in.fader[i].alive)
        {
            continue;
        }
        if (alarm_tick(&in.fader[i].alarm2))
        {
            in.fader[i].alarm3 = 2;
            in.image_index++;
        }
        if (alarm_tick(&in.fader[i].alarm3))
        {
            in.fader[i].alpha--;
            if (in.fader[i].alpha <= 2)
            {
                in.fader[i].alive = false;
            }
            in.fader[i].alarm3 = 2;
        }
    }
    if (wr.alive && alarm_tick(&wr.alarm))
    {
        writer_advance();
    }
    if (in.last)
    {
        if (alarm_tick(&in.last_alarm0))
        {
            in.go = true;
        }
        if (alarm_tick(&in.last_alarm2))
        {
            in.last_visible = true;
        }
    }

    /* Step. */
    if (in.act && (keys_pressed & K_CONFIRM) && !in.skip)
    {
        start_skip();
        if (wr.alive)
        {
            writer_destroy();
        }
    }
    for (uint8_t i = 0; i < MAX_FADERS; i++)
    {
        if (!in.fader[i].alive)
        {
            continue;
        }
        if (in.fader[i].alpha <= 9 && !in.fader[i].over)
        {
            in.fader[i].alpha++;
        }
        if (in.fader[i].alpha > 9 && !in.fader[i].over)
        {
            in.fader[i].over = true;
            in.fader[i].alarm2 = 4;
        }
    }
    if (in.unfader && in.unfader_alpha < 100)
    {
        in.unfader_alpha += 8;
    }

    /* Draw. */
    if (in.visible && in.image_index != in.shown_index)
    {
        in.shown_index = in.image_index;
        /* The 11 intro panels are consecutive AppVar entries. */
        zx0_Decompress(panel, UTINTRO_appvar[UTINTRO_panels_intro0_compressed_index + in.image_index % 11]);
        if (!in.last_visible)
        {
            blit_window(panel);
        }
    }
    if (in.last && in.last_visible)
    {
        /* draw_sprite_part(..., 0, sprite_height - (h + 100), ...) at y 30 */
        int16_t top = LAST_H - (in.h + 100);
        if (top != in.last_top)
        {
            in.last_top = top;
            blit_window(store() + top * WIN_W);
        }
        if (in.go)
        {
            in.h++;
        }
        if (in.h > 240)
        {
            in.h--;
        }
    }
    if (wr.alive)
    {
        writer_draw();
    }

    unsigned pic = 256;
    for (uint8_t i = 0; i < MAX_FADERS; i++)
    {
        if (in.fader[i].alive)
        {
            pic = pic * (10 - in.fader[i].alpha) / 10;
        }
    }
    unsigned all = in.unfader ? (100 - (in.unfader_alpha > 100 ? 100 : in.unfader_alpha)) * 256 / 100 : 256;
    set_palette(pic, all, 0);
    return true;
}

/* ---- room_introimage (title) ---- */

static struct
{
    int16_t alarm0, alarm1;
    bool d, text_drawn;
} ti;

void title_begin(void)
{
    ti.alarm0 = 600;
    ti.alarm1 = 100;
    ti.d = false;
    ti.text_drawn = false;
    gfx_FillScreen(0);
    zx0_Decompress(panel, title_compressed);
    for (uint8_t y = 0; y < title_height; y++)
    {
        memcpy(&gfx_vbuffer[LOGO_Y + y][LOGO_X], panel + y * title_width, title_width);
    }
    set_palette(256, 256, 0);
}

uint8_t title_step(void)
{
    if (alarm_tick(&ti.alarm0))
    {
        return TITLE_TIMEOUT;
    }
    if (alarm_tick(&ti.alarm1))
    {
        ti.d = true;
    }
    if (ti.d && !ti.text_drawn)
    {
        /* title_press_button_pc, with the CE's keys */
        draw_text(&font_small, "[PRESS 2ND OR ENTER]", 120, 180, COL_GRAY);
        ti.text_drawn = true;
    }
    return (keys_pressed & K_CONFIRM) ? TITLE_PROCEED : TITLE_STAY;
}
