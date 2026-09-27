/*
 * Undertale's opening for the TI-84 Plus CE: the story intro, the title
 * screen, the new-game menu (instructions and naming) and the first room of
 * the Ruins, recreated from the game's own GML. The game runs at 30 steps per second on a 320x240
 * view, which is the CE's screen size, so coordinates and timings are the
 * original ones.
 */
#include <time.h>

#include <graphx.h>
#include <keypadc.h>
#include <ti/getcsc.h>
#include <ti/screen.h>

#include "common.h"
#include "gfx/utarea1.h"
#include "gfx/utintro.h"
#undef global_palette
#include "gfx/utflow0.h"
#undef global_palette
#include "gfx/utflow1.h"
#undef global_palette
#include "gfx/utflow2.h"
#undef global_palette
#include "gfx/utflow3.h"
#undef global_palette
#include "gfx/utflow4.h"

#ifdef CE_AUTOPLAY
#ifdef CE_FLOWEY_TEST
#include "autoplay_flowey.h"
#else
#include "autoplay.h"
#endif
#endif

uint8_t keys_pressed;
uint8_t keys_held;
static bool quit;

static void read_keys(void)
{
    uint8_t now = 0;

    kb_Scan();
    if ((kb_Data[1] & kb_2nd) || (kb_Data[6] & kb_Enter))
    {
        now |= K_CONFIRM;
    }
    if ((kb_Data[2] & kb_Alpha) || (kb_Data[1] & kb_Del))
    {
        now |= K_CANCEL;
    }
    if (kb_Data[7] & kb_Up)
    {
        now |= K_UP;
    }
    if (kb_Data[7] & kb_Down)
    {
        now |= K_DOWN;
    }
    if (kb_Data[7] & kb_Left)
    {
        now |= K_LEFT;
    }
    if (kb_Data[7] & kb_Right)
    {
        now |= K_RIGHT;
    }
#ifdef CE_AUTOPLAY
    {
        static uint8_t step, wait, hold;
        const uint8_t n = sizeof autoplay / sizeof autoplay[0];
        now = 0;
        if (hold)
        {
            now = autoplay[step - 1].keys;
            hold--;
        }
        else if (step < n && ++wait >= autoplay[step].wait)
        {
            now = autoplay[step].keys;
            hold = autoplay[step].hold ? autoplay[step].hold - 1 : 0;
            step++;
            wait = 0;
        }
    }
#endif
    keys_pressed = now & ~keys_held;
    keys_held = now;
    if (kb_Data[6] & kb_Clear)
    {
        quit = true;
    }
}

int main(void)
{
    enum { ROOM_INTRO, ROOM_TITLE, ROOM_MENU, ROOM_AREA1, ROOM_FLOWEY } room = ROOM_INTRO;
    clock_t next;

    if (!UTINTRO_init() || !UTAREA1_init() || !UTFLOW0_init() ||
        !UTFLOW1_init() || !UTFLOW2_init() || !UTFLOW3_init() || !UTFLOW4_init())
    {
        os_ClrHome();
        os_PutStrFull("Send all UTINTRO, UTAREA1 and UTFLOW AppVars.");
        while (!os_GetCSC())
        {
        }
        return 1;
    }

    gfx_Begin();
    gfx_SetDrawScreen();
    set_palette(256, 256, 0);
#ifdef CE_FLOWEY_TEST
    room = ROOM_FLOWEY;
    flowey_begin();
#else
    intro_begin();
#endif

    next = clock();
    while (!quit)
    {
        read_keys();
        if (room == ROOM_INTRO)
        {
            if (!intro_step())
            {
                room = ROOM_TITLE;
                title_begin();
            }
        }
        else if (room == ROOM_TITLE)
        {
            uint8_t r = title_step();
            if (r == TITLE_TIMEOUT)
            {
                room = ROOM_INTRO;
                intro_begin();
            }
            else if (r == TITLE_PROCEED)
            {
                room = ROOM_MENU;
                menu_begin();
            }
        }
        else if (room == ROOM_MENU)
        {
            uint8_t r = menu_step();
            if (r == MENU_RESTART)
            {
                /* game_restart() */
                gfx_SetDrawScreen();
                room = ROOM_INTRO;
                intro_begin();
            }
            else if (r == MENU_DONE)
            {
                room = ROOM_AREA1;
                area1_begin();
            }
        }
        else if (room == ROOM_AREA1)
        {
            if (!area1_step()) {
                room = ROOM_FLOWEY;
                flowey_begin();
            }
        }
        else if (!flowey_step())
        {
            break; /* the doorway to room_ruins1 */
        }

        next += CLOCKS_PER_SEC / FPS;
        while ((long)(clock() - next) < 0)
        {
        }
    }

    gfx_End();
    return 0;
}
