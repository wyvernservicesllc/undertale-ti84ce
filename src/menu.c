/*
 * room_intromenu for a new game: the instruction screen, "Name the fallen
 * human.", the name check with its special reactions, and the white fade
 * into the game. Follows obj_intromenu and scr_namingscreen, with naming
 * 3 = instructions, 1 = letter grid, 2 = "Is this name correct?", 4/5 =
 * chosen. The screen is redrawn every frame into the back buffer, like
 * GameMaker does.
 *
 * Not ported: the Continue/Reset screen (it needs an in-game save, which
 * does not exist yet) and the Settings room.
 */
#include <stdlib.h>
#include <string.h>

#include <fileioc.h>
#include <graphx.h>

#include "common.h"
#include "intro_data.h"

#define NAME_MAX 6

/* scr_namingscreen_setup, English layout */
#define ROWS 8
#define COLS 7
#define TITLE_Y 30
#define NAME_X 140
#define NAME_Y 55
#define MENU_Y 200
static const int menu_x[3] = { 60, 120, 220 }; /* Quit, Backspace, Done */

#define SAVE_NAME "UTSAVE"

static struct
{
    uint8_t naming;
    uint8_t selected3; /* instructions: 0 Begin Game, 1 Settings */
    int8_t row, col;   /* letter grid; row -1 is Quit/Backspace/Done */
    uint8_t selected2; /* confirm: 0 No, 1 Yes */
    char name[NAME_MAX + 1];
    uint8_t q;         /* grows the name on the confirm screen */
    uint16_t alerm;
    const char *spec_m;
    bool allow;
    bool hardmode;
    unsigned white;    /* obj_whitefader alpha, in thousandths */
} m;

static char charmap(int8_t row, int8_t col)
{
    uint8_t i = (row % 4) * COLS + col;

    if (i >= 26)
    {
        return 0;
    }
    return (row < 4 ? 'A' : 'a') + i;
}

static int grid_x(int8_t col)
{
    return 60 + col * 32;
}

static int grid_y(int8_t row)
{
    return row < 4 ? 75 + row * 14 : 135 + (row - 4) * 14;
}

static bool str_ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
    {
        char ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
        if (ca != *b)
        {
            return false;
        }
    }
    return *a == *b;
}

/* scr_hardmodename */
static bool hardmode_name(const char *name)
{
    return str_ieq(name, "frisk");
}

/* scr_namingscreen_check. Returns false for GASTER, which restarts the
   game. */
static bool check_name(const char *name)
{
    for (uint8_t i = 0; i < sizeof special_names / sizeof special_names[0]; i++)
    {
        if (str_ieq(name, special_names[i].name))
        {
            m.allow = special_names[i].allow;
            m.spec_m = special_names[i].msg;
            return true;
        }
    }
    if (str_ieq(name, "gaster"))
    {
        return false;
    }
    m.allow = true;
    m.spec_m = STR_name_entry_confirm;
    return true;
}

void menu_begin(void)
{
    memset(&m, 0, sizeof m);
    m.naming = 3;
    gfx_SetDrawBuffer();
    set_palette(256, 256, 0);
}

static void save_name(void)
{
    uint8_t h = ti_Open(SAVE_NAME, "w");

    if (h)
    {
        /* Name, then the "fun" value and hard mode flag from the ini. */
        uint8_t fun = rand() % 100 + 1;
        ti_Write(m.name, sizeof m.name, 1, h);
        ti_Write(&fun, 1, 1, h);
        ti_Write(&m.hardmode, 1, 1, h);
        ti_SetArchiveStatus(true, h);
        ti_Close(h);
    }
}

/* The name growing and shaking on the confirm screen. */
static void draw_growing_name(void)
{
    int rotation_limit;
    int rotation;

    if (m.q < 120)
    {
        m.q++;
    }
    /* scr_namingscreen uses random(r * 2) for the position and
       random_ranger(-r*q/60, r*q/60) for rotation, with r = 0.5. */
    rotation_limit = m.q * 4 / 120; /* about one degree at q = 120 */
    rotation = rotation_limit ? rand() % (rotation_limit * 2 + 1) - rotation_limit : 0;
    draw_text_transformed(&font_main, m.name,
                          NAME_X - m.q / 3 + rand() % 2,
                          m.q / 2 + NAME_Y + rand() % 2,
                          256 + m.q * 256 / 50, rotation, COL_WHITE);
}

static void naming_chosen(void)
{
    /* naming 4 */
    if (m.naming == 4)
    {
        m.hardmode = hardmode_name(m.name);
        m.alerm = 0;
        m.naming = 5;
    }
}

/* Letter grid movement: the do/until loop of scr_namingscreen, which keeps
   applying the same key until it lands on a letter. */
static void grid_move(void)
{
    uint8_t k = keys_pressed;
    int8_t old_col = m.col;

    do
    {
        if (k & K_RIGHT)
        {
            m.col++;
            if (m.row == -1)
            {
                if (m.col > 2)
                {
                    m.col = 0;
                }
            }
            else if (m.col >= COLS)
            {
                if (m.row == ROWS - 1)
                {
                    m.col = old_col;
                    break;
                }
                m.col = 0;
                m.row++;
            }
        }
        if (k & K_LEFT)
        {
            m.col--;
            if (m.col < 0)
            {
                if (m.row == 0)
                {
                    m.col = 0;
                }
                else if (m.row > 0)
                {
                    m.col = COLS - 1;
                    m.row--;
                }
                else
                {
                    m.col = 2;
                }
            }
        }
        if (k & K_DOWN)
        {
            if (m.row == -1)
            {
                /* Closest grid column to the selected menu item. */
                int xx = menu_x[m.col];
                int8_t best = 0;
                int bestdiff = abs(grid_x(0) - xx);
                for (int8_t i = 1; i < COLS; i++)
                {
                    int diff = abs(grid_x(i) - xx);
                    if (diff < bestdiff)
                    {
                        best = i;
                        bestdiff = diff;
                    }
                }
                m.row = 0;
                m.col = best;
            }
            else if (++m.row >= ROWS)
            {
                int xx = grid_x(m.col);
                m.row = -1;
                m.col = xx >= menu_x[2] - 10 ? 2 : xx >= menu_x[1] - 10 ? 1 : 0;
            }
        }
        if (k & K_UP)
        {
            if (m.row == -1)
            {
                m.row = ROWS - 1;
                if (m.col > 0)
                {
                    int xx = menu_x[m.col];
                    int8_t best = 0;
                    int bestdiff = abs(grid_x(0) - xx);
                    for (int8_t i = 1; i < COLS; i++)
                    {
                        int diff = abs(grid_x(i) - xx);
                        if (diff < bestdiff)
                        {
                            best = i;
                            bestdiff = diff;
                        }
                    }
                    m.col = best;
                }
            }
            else if (--m.row == -1)
            {
                int xx = grid_x(m.col);
                m.col = xx >= menu_x[2] - 10 ? 2 : xx >= menu_x[1] - 10 ? 1 : 0;
            }
        }
    } while (!(m.col < 0 || m.row < 0 || charmap(m.row, m.col)));
}

/* naming 1: "Name the fallen human." */
static void letter_grid(void)
{
    uint8_t len = strlen(m.name);
    bool backspace = false;

    m.q = 0;
    for (int8_t row = 0; row < ROWS; row++)
    {
        for (int8_t col = 0; col < COLS; col++)
        {
            char c = charmap(row, col);
            if (c)
            {
                uint8_t color = m.row == row && m.col == col ? COL_YELLOW : COL_WHITE;
                /* scr_namingscreen draws every letter at
                   (x + random(0.5), y + random(0.5)). The CE's 320x240
                   screen rounds this to a one-pixel jitter. */
                draw_char(&font_main, c, grid_x(col) + (rand() & 1),
                          grid_y(row) + (rand() & 1), color);
            }
        }
    }
    draw_text(&font_main, STR_name_entry_quit, menu_x[0], MENU_Y, m.row == -1 && m.col == 0 ? COL_YELLOW : COL_WHITE);
    draw_text(&font_main, STR_name_entry_backspace, menu_x[1], MENU_Y, m.row == -1 && m.col == 1 ? COL_YELLOW : COL_WHITE);
    draw_text(&font_main, STR_name_entry_done, menu_x[2], MENU_Y, m.row == -1 && m.col == 2 ? COL_YELLOW : COL_WHITE);

    grid_move();

    if (keys_pressed & K_CONFIRM)
    {
        if (m.row == -1)
        {
            if (m.col == 0)
            {
                m.naming = 3;
            }
            if (m.col == 1)
            {
                backspace = true;
            }
            if (m.col == 2 && len > 0)
            {
                m.naming = 2;
                m.selected2 = 0;
            }
            keys_pressed &= ~K_CONFIRM; /* control_clear(0) */
        }
        else
        {
            if (len == NAME_MAX)
            {
                len--;
            }
            m.name[len++] = charmap(m.row, m.col);
            m.name[len] = 0;
        }
    }
    if ((keys_pressed & K_CANCEL) || backspace)
    {
        if (len > 0)
        {
            m.name[--len] = 0;
        }
        keys_pressed &= ~K_CANCEL;
    }
    draw_text(&font_main, m.name, NAME_X, NAME_Y, COL_WHITE);
    draw_text_centered(&font_main, STR_name_entry_title, 160, TITLE_Y, COL_WHITE);
}

/* naming 2: "Is this name correct?" Returns false when nothing is drawn
   this frame (the script exits right after a choice). */
static bool confirm_screen(void)
{
    if (m.name[0] == 0)
    {
        m.spec_m = STR_name_entry_missing;
        m.allow = false;
    }
    else if (hardmode_name(m.name))
    {
        m.spec_m = STR_name_entry_hardmode;
        m.allow = true;
    }
    else
    {
        check_name(m.name);
    }

    if (keys_pressed & K_CONFIRM)
    {
        if (m.allow && m.selected2 == 1 && m.name[0])
        {
            m.naming = 4;
        }
        if (m.selected2 == 0)
        {
            m.naming = 1;
        }
        return false;
    }

    draw_growing_name();
    draw_text(&font_main, m.spec_m, 90, 30, COL_WHITE);
    if (m.allow)
    {
        draw_text_centered(&font_main, STR_no, 80, 200, m.selected2 == 0 ? COL_YELLOW : COL_WHITE);
        draw_text_centered(&font_main, STR_yes, 240, 200, m.selected2 == 1 ? COL_YELLOW : COL_WHITE);
        if (keys_pressed & (K_LEFT | K_RIGHT))
        {
            m.selected2 = !m.selected2;
        }
    }
    else
    {
        draw_text_centered(&font_main, STR_name_entry_goback, 80, 200, COL_YELLOW);
    }
    return true;
}

/* naming 3 with no save: the instruction screen. */
static void instructions(void)
{
    /* The PC version lists its keys here; these are the calculator's. */
    static const char *const lines[] = {
        "[2nd or ENTER] - " STR_instructions_confirm_label,
        "[ALPHA or DEL] - " STR_instructions_cancel_label,
        "[MODE] - " STR_instructions_menu_label,
        "[CLEAR] - " STR_instructions_quit_label,
    };

    draw_text(&font_main, STR_instructions_title, 85, 20, COL_LTGRAY);
    for (uint8_t i = 0; i < 4; i++)
    {
        draw_text(&font_main, lines[i], 85, 50 + i * 18, COL_LTGRAY);
    }
    draw_text(&font_main, STR_instructions_hp0, 85, 140, COL_LTGRAY);
    draw_text(&font_main, STR_instructions_begin, 85, 172, m.selected3 == 0 ? COL_YELLOW : COL_WHITE);
    if (keys_pressed & K_DOWN)
    {
        m.selected3 = 1;
    }
    if (keys_pressed & K_UP)
    {
        m.selected3 = 0;
    }
    draw_text(&font_main, STR_settings_name, 85, 192, m.selected3 == 1 ? COL_YELLOW : COL_WHITE);
    if ((keys_pressed & K_CONFIRM) && m.selected3 == 0)
    {
        m.naming = 1;
        keys_pressed &= ~K_CANCEL; /* control_clear(1) */
    }
    /* Settings (room_settings) is not ported. */
}

uint8_t menu_step(void)
{
    /* obj_intromenu Step */
    if ((m.naming == 1 || m.naming == 2) && !check_name(m.name))
    {
        return MENU_RESTART;
    }

    /* obj_intromenu Draw -> scr_namingscreen */
    gfx_FillScreen(0);
    naming_chosen();
    if (m.naming == 5)
    {
        m.alerm++;
        draw_growing_name();
        if (m.alerm > 179)
        {
            save_name();
            return MENU_DONE;
        }
    }
    if (m.naming == 2)
    {
        confirm_screen();
    }
    else if (m.naming == 1)
    {
        letter_grid();
    }
    if (m.naming == 3)
    {
        instructions();
        draw_text_centered(&font_small, "UNDERTALE v1.08 (C) Toby Fox 2015-2017", 160, 232, COL_GRAY);
    }

    /* obj_whitefader: alpha += 0.006 per step */
    if (m.naming == 5 && m.white < 1000)
    {
        m.white += 6;
    }
    set_palette(256, 256, (m.white > 1000 ? 1000 : m.white) * 256 / 1000);
    gfx_SwapDraw();
    return MENU_STAY;
}
