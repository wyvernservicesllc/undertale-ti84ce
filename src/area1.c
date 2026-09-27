/*
 * room_area1: the flower bed where the human lands, the first room you walk
 * around in. Follows obj_mainchara: its Step event (3 px per step, facing
 * rules), its collision events with obj_solidparent and the four diagonal
 * walls (obj_sdr, obj_sdl, obj_sur, obj_sul), its End Step (walk animation,
 * camera), and obj_doorA, which fades out and leaves for room_area1_2.
 *
 * Nothing in this room can be interacted with on the first visit: the
 * flower bed text (obj_readable_flowers1) and Toriel (obj_torinteractable7)
 * destroy themselves until later in the story.
 *
 * The background is a 20x20 tilemap drawn straight from the archived
 * UTAREA1 AppVar into the back buffer every frame.
 */
#include <graphx.h>
#include <sys/lcd.h>

#include "common.h"
#include "gfx/utarea1.h"

enum { WALL_SOLID, WALL_SDR, WALL_SDL, WALL_SUR, WALL_SUL };

typedef struct
{
    uint8_t kind;
    int16_t x, y, w, h;
} wall_t;

#include "area1_data.h"

#define NUM_WALLS (sizeof area1_walls / sizeof area1_walls[0])

enum { FACE_DOWN, FACE_RIGHT, FACE_UP, FACE_LEFT }; /* global.facing */

/* Frisk's frames in UTAREA1, per facing: first entry and frame count. */
static const uint8_t frisk_first[4] = {
    UTAREA1_frisk_frisk_d0_index, UTAREA1_frisk_frisk_r0_index,
    UTAREA1_frisk_frisk_u0_index, UTAREA1_frisk_frisk_l0_index,
};
static const uint8_t frisk_frames[4] = { 4, 2, 4, 2 };

static gfx_tilemap_t tilemap;

static struct
{
    int x, y, xprev, yprev;
    uint8_t facing;
    uint8_t sprite;           /* facing the sprite was chosen for */
    bool moving, movement;
    uint8_t image_index;      /* in fifths: image_speed 0.2 = 1 */
    uint8_t image_speed;
    uint8_t interact;         /* global.interact; 3 = going through a door */
    int16_t door_alarm;
    int16_t fade_in;          /* obj_persistentfader alpha, hundredths */
    int16_t fade_out;         /* obj_unfader alpha, hundredths */
} p;

/* obj_mainchara's bounding box: spr_mainchara* mask rows 19-29, all 20
   columns. */
#define BB_LEFT (p.x)
#define BB_RIGHT (p.x + 19)
#define BB_TOP (p.y + 19)
#define BB_BOTTOM (p.y + 29)

/* collision_rectangle / collision_point / collision_line against
   obj_solidparent, bounding boxes only. The diagonal walls are not
   obj_solidparent children, so they never count here. */
static bool solid_in_rect(int x1, int y1, int x2, int y2)
{
    if (x1 > x2)
    {
        int t = x1; x1 = x2; x2 = t;
    }
    if (y1 > y2)
    {
        int t = y1; y1 = y2; y2 = t;
    }
    for (uint8_t i = 0; i < NUM_WALLS; i++)
    {
        const wall_t *w = &area1_walls[i];
        if (w->kind == WALL_SOLID &&
            x1 < w->x + w->w && x2 >= w->x && y1 < w->y + w->h && y2 >= w->y)
        {
            return true;
        }
    }
    return false;
}

static bool solid_at(int x, int y)
{
    return solid_in_rect(x, y, x, y);
}

/* Does the player's box touch this wall? Diagonal walls use their precise
   masks. */
static bool touches(const wall_t *w)
{
    static const uint8_t *const masks[] = { 0, mask_sdr, mask_sdl, mask_sur, mask_sul };
    int x1 = BB_LEFT > w->x ? BB_LEFT : w->x;
    int y1 = BB_TOP > w->y ? BB_TOP : w->y;
    int x2 = BB_RIGHT < w->x + w->w - 1 ? BB_RIGHT : w->x + w->w - 1;
    int y2 = BB_BOTTOM < w->y + w->h - 1 ? BB_BOTTOM : w->y + w->h - 1;

    if (x1 > x2 || y1 > y2)
    {
        return false;
    }
    if (w->kind == WALL_SOLID)
    {
        return true;
    }
    for (int y = y1; y <= y2; y++)
    {
        const uint8_t *row = masks[w->kind] + (y - w->y) * 3;
        for (int x = x1; x <= x2; x++)
        {
            int mx = x - w->x;
            if (row[mx >> 3] & (0x80 >> (mx & 7)))
            {
                return true;
            }
        }
    }
    return false;
}

static bool held(uint8_t k)
{
    return (keys_held & k) != 0;
}

/* obj_mainchara Collision with obj_solidparent */
static void hit_solid(void)
{
    p.x = p.xprev;
    p.y = p.yprev;
    if (p.interact == 0)
    {
        if (held(K_UP))
        {
            if (solid_in_rect(p.x + 2, p.y + 15, p.x + 18, p.y + 19))
            {
                if (held(K_LEFT) && !solid_in_rect(BB_LEFT - 3, BB_TOP, BB_LEFT, BB_TOP))
                {
                    p.x -= 3;
                    p.facing = FACE_LEFT;
                }
                if (held(K_RIGHT) && !solid_in_rect(BB_RIGHT + 3, BB_TOP, BB_RIGHT, BB_TOP))
                {
                    p.x += 3;
                    p.facing = FACE_RIGHT;
                }
            }
            else
            {
                p.y -= 3;
                p.facing = FACE_UP;
            }
        }
        if (held(K_DOWN))
        {
            if (solid_in_rect(p.x + 2, p.y + 30, p.x + 18, p.y + 33))
            {
                if (held(K_LEFT) && !solid_in_rect(BB_LEFT - 3, BB_BOTTOM, BB_LEFT, BB_BOTTOM))
                {
                    p.x -= 3;
                    p.facing = FACE_LEFT;
                }
                if (held(K_RIGHT) && !solid_in_rect(BB_RIGHT + 3, BB_BOTTOM, BB_RIGHT, BB_BOTTOM))
                {
                    p.x += 3;
                    p.facing = FACE_RIGHT;
                }
            }
            else
            {
                p.y += 3;
                p.facing = FACE_DOWN;
            }
        }
    }
    p.moving = false;
}

/* obj_mainchara Collision with the diagonal walls (events 10-13). Walking
   into a slope slides along it; the corner checks stop the slide when a
   solid wall is in the way. */
static void hit_diagonal(uint8_t kind)
{
    if (p.interact == 0)
    {
        int xp = p.xprev, yp = p.yprev;

        switch (kind)
        {
        case WALL_SDR: /* obj_sdr */
            if (p.facing == FACE_RIGHT)
            {
                if (!solid_at(BB_RIGHT + 2, BB_TOP - 2)) { p.x = xp + 3; p.y = yp - 3; }
                else p.x = xp;
            }
            if (p.facing == FACE_DOWN)
            {
                if (!solid_at(BB_LEFT - 3, BB_BOTTOM + 3)) { p.x = xp - 3; p.y = yp + 3; }
                else p.y = yp;
            }
            if (p.facing == FACE_UP) { p.x = xp; p.y = yp - 3; }
            if (p.facing == FACE_LEFT) { p.y = yp; p.x = xp - 3; }
            if (held(K_DOWN) && held(K_RIGHT)) { p.x = xp; p.y = yp; }
            if (p.x % 3) p.x--;
            if (p.y % 3) p.y--;
            break;
        case WALL_SDL: /* obj_sdl */
            if (p.facing == FACE_LEFT)
            {
                if (!solid_at(BB_LEFT - 2, BB_TOP - 2)) { p.x = xp - 3; p.y = yp - 3; }
                else p.x = xp;
            }
            if (p.facing == FACE_DOWN)
            {
                if (!solid_at(BB_RIGHT + 3, BB_BOTTOM + 3)) { p.x = xp + 3; p.y = yp + 3; }
                else p.y = yp;
            }
            if (p.facing == FACE_UP) { p.x = xp; p.y = yp - 3; }
            if (p.facing == FACE_RIGHT) { p.y = yp; p.x = xp + 3; }
            if (held(K_DOWN) && held(K_LEFT)) { p.x = xp; p.y = yp; }
            if (p.x % 3) p.x++;
            if (p.y % 3) p.y--;
            break;
        case WALL_SUR: /* obj_sur */
            if (p.facing == FACE_RIGHT)
            {
                if (!solid_at(BB_RIGHT + 3, BB_BOTTOM + 3)) { p.x = xp + 3; p.y = yp + 3; }
                else p.x = xp;
            }
            if (p.facing == FACE_UP)
            {
                if (!solid_at(BB_LEFT - 3, BB_TOP - 3)) { p.x = xp - 3; p.y = yp - 3; }
                else p.y = yp;
            }
            if (p.facing == FACE_DOWN) { p.x = xp; p.y = yp + 3; }
            if (p.facing == FACE_LEFT) { p.y = yp; p.x = xp - 3; }
            if (held(K_UP) && held(K_RIGHT)) { p.x = xp; p.y = yp; }
            if (p.x % 3) p.x--;
            if (p.y % 3) p.y++;
            break;
        case WALL_SUL: /* obj_sul */
            if (p.facing == FACE_LEFT)
            {
                if (!solid_at(BB_LEFT - 3, BB_BOTTOM + 3)) { p.x = xp - 3; p.y = yp + 3; }
                else p.x = xp;
            }
            if (p.facing == FACE_UP)
            {
                if (!solid_at(BB_RIGHT + 3, BB_TOP - 3)) { p.x = xp + 3; p.y = yp - 3; }
                else p.y = yp;
            }
            if (p.facing == FACE_DOWN) { p.x = xp; p.y = yp + 3; }
            if (p.facing == FACE_RIGHT) { p.y = yp; p.x = xp + 3; }
            if (held(K_UP) && held(K_LEFT)) { p.x = xp; p.y = yp; }
            if (p.x % 3) p.x++;
            if (p.y % 3) p.y++;
            break;
        }
        p.moving = false;
    }
    if (p.interact == 3)
    {
        p.x = p.xprev;
        p.y = p.yprev;
    }
}

/* Collision events, in obj_mainchara's event order: obj_solidparent, then
   obj_sul, obj_sur, obj_sdl, obj_sdr. */
static void collisions(void)
{
    static const uint8_t order[] = { WALL_SOLID, WALL_SUL, WALL_SUR, WALL_SDL, WALL_SDR };

    for (uint8_t k = 0; k < sizeof order; k++)
    {
        for (uint8_t i = 0; i < NUM_WALLS; i++)
        {
            const wall_t *w = &area1_walls[i];
            if (w->kind == order[k] && touches(w))
            {
                if (w->kind == WALL_SOLID)
                {
                    hit_solid();
                }
                else
                {
                    hit_diagonal(w->kind);
                }
            }
        }
    }
}

/* obj_mainchara Step: arrow keys move 3 px and turn the player. */
static void walk(void)
{
    bool turned;

    if (!p.movement)
    {
        return;
    }
    if (held(K_LEFT))
    {
        turned = true;
        p.x -= 3;
        if (!p.moving)
        {
            p.image_index = 5;
        }
        p.moving = true;
        p.image_speed = 1;
        if ((held(K_UP) && p.facing == FACE_UP) || (held(K_DOWN) && p.facing == FACE_DOWN))
        {
            turned = false;
        }
        if (turned)
        {
            p.facing = FACE_LEFT;
        }
    }
    if (held(K_UP))
    {
        turned = true;
        p.y -= 3;
        if (!p.moving)
        {
            p.image_index = 5;
        }
        p.moving = true;
        p.image_speed = 1;
        if ((held(K_RIGHT) && p.facing == FACE_RIGHT) || (held(K_LEFT) && p.facing == FACE_LEFT))
        {
            turned = false;
        }
        if (turned)
        {
            p.facing = FACE_UP;
        }
    }
    if (held(K_RIGHT) && !held(K_LEFT))
    {
        turned = true;
        p.x += 3;
        /* The original sets moving before checking it, so walking right
           never restarts the animation on frame 1. */
        p.moving = true;
        p.image_speed = 1;
        if ((held(K_UP) && p.facing == FACE_UP) || (held(K_DOWN) && p.facing == FACE_DOWN))
        {
            turned = false;
        }
        if (turned)
        {
            p.facing = FACE_RIGHT;
        }
    }
    if (held(K_DOWN) && !held(K_UP))
    {
        turned = true;
        p.y += 3;
        if (!p.moving)
        {
            p.image_index = 5;
        }
        p.moving = true;
        p.image_speed = 1;
        if ((held(K_RIGHT) && p.facing == FACE_RIGHT) || (held(K_LEFT) && p.facing == FACE_LEFT))
        {
            turned = false;
        }
        if (turned)
        {
            p.facing = FACE_DOWN;
        }
    }
}

void area1_begin(void)
{
    p.x = AREA1_START_X;
    p.y = AREA1_START_Y;
    /* obj_mainchara Create snaps the position to the 3 px grid. */
    if (p.x % 3 == 2) p.x++;
    if (p.x % 3 == 1) p.x--;
    if (p.y % 3 == 2) p.y++;
    if (p.y % 3 == 1) p.y--;
    p.facing = FACE_DOWN;
    p.sprite = FACE_DOWN;
    p.moving = false;
    p.movement = true;
    p.image_index = 0;
    p.image_speed = 0;
    p.interact = 0;
    p.door_alarm = -1;
    p.fade_in = 100; /* obj_persistentfader from the naming screen */
    p.fade_out = -1;

    tilemap.map = area1_tilemap;
    tilemap.tiles = area1_tiles_tiles;
    tilemap.type_width = gfx_tile_no_pow2;
    tilemap.type_height = gfx_tile_no_pow2;
    tilemap.tile_width = 20;
    tilemap.tile_height = 20;
    tilemap.draw_width = LCD_WIDTH / 20 + 1;
    tilemap.draw_height = LCD_HEIGHT / 20 + 1;
    tilemap.width = AREA1_TILES_W;
    tilemap.height = AREA1_TILES_H;
    tilemap.x_loc = 0;
    tilemap.y_loc = 0;

    gfx_SetDrawBuffer();
    gfx_SetClipRegion(0, 0, LCD_WIDTH, LCD_HEIGHT);
    gfx_SetTransparentColor(SPRITE_TRANSPARENT);
}

bool area1_step(void)
{
    int vx, vy;

    /* Step */
    p.xprev = p.x;
    p.yprev = p.y;
    p.sprite = p.facing;
    walk();
    if (p.interact == 0 &&
        BB_LEFT < AREA1_DOOR_X + 20 && BB_RIGHT >= AREA1_DOOR_X &&
        BB_TOP < AREA1_DOOR_Y + 20 && BB_BOTTOM >= AREA1_DOOR_Y)
    {
        /* obj_doorA user event 9 */
        p.interact = 3;
        p.fade_out = 0;
        p.door_alarm = 8;
    }

    collisions();

    /* End Step */
    if (!held(K_LEFT | K_RIGHT | K_UP | K_DOWN))
    {
        p.moving = false;
    }
    if (p.interact > 0)
    {
        p.moving = false;
        p.movement = false;
    }
    else
    {
        p.movement = true;
    }
    if (p.x != p.xprev || p.y != p.yprev)
    {
        p.moving = true;
    }
    if (!p.moving)
    {
        p.image_speed = 0;
        p.image_index = 0;
    }
    p.image_index += p.image_speed;

    if (alarm_tick(&p.door_alarm))
    {
        return false; /* room_goto(room_area1_2) */
    }

    /* The camera centers on the player and stays inside the room. */
    vx = p.x - LCD_WIDTH / 2 + 10;
    vy = p.y - LCD_HEIGHT / 2 + 10;
    vx = vx < 0 ? 0 : vx > AREA1_W - LCD_WIDTH ? AREA1_W - LCD_WIDTH : vx;
    vy = vy < 0 ? 0 : vy > AREA1_H - LCD_HEIGHT ? AREA1_H - LCD_HEIGHT : vy;

    /* Draw */
    gfx_Tilemap(&tilemap, vx, vy);
    {
        uint8_t frame = (p.image_index / 5) % frisk_frames[p.sprite];
        gfx_TransparentSprite((gfx_sprite_t *)UTAREA1_appvar[frisk_first[p.sprite] + frame],
                              p.x - vx, p.y - vy);
    }

    /* obj_persistentfader (black, fading out) and obj_unfader (black,
       fading in at the door) */
    {
        unsigned level = 256;
        if (p.fade_in > 0)
        {
            level = (100 - p.fade_in) * 256 / 100;
            p.fade_in -= 8;
        }
        if (p.fade_out >= 0)
        {
            level = level * (100 - (p.fade_out > 100 ? 100 : p.fade_out)) / 100;
            p.fade_out += 8;
        }
        set_palette(256, level, 0);
    }
    gfx_SwapDraw();
    return true;
}
