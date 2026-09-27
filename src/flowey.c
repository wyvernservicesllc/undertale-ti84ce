/* room_area1_2 and its first Flowey encounter, using art and text from data.win. */
#include <graphx.h>
#include <sys/lcd.h>
#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "flowey_data.h"
#include "flowey_text.h"
#include "gfx/utarea1.h"
#include "gfx/utflow0.h"
#undef global_palette
#include "gfx/utflow1.h"
#undef global_palette
#include "gfx/utflow2.h"
#undef global_palette
#include "gfx/utflow3.h"
#undef global_palette
#include "gfx/utflow4.h"

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define BLACK 0

enum {
    APPROACH, OPENING, TO_BATTLE, SOUL, PELLET_TALK, PELLET_WAVE,
    MISSED, LAST_CHANCE, KNOW, HIT, DIE, RADIAL, RESCUE,
    BACK_TO_ROOM, TORIEL_ROOM, FOLLOW_TORIEL, LEAVE_ROOM
};
enum { FACE_DOWN, FACE_RIGHT, FACE_UP, FACE_LEFT };
enum {
    NICE, NICE_SIDE, NICE_SIDE_UM, SASSY, EVIL, GRIN,
    LAUGH, PISSED, WINK, SIDE, SIDE_SHOCK, HURT
};

static gfx_tilemap_t map;
static struct {
    uint8_t phase, face, page, retries, hp, invincible, facing;
    uint16_t tick, letters;
    int16_t x, y, toriel_y, heart_x, heart_y;
    const char *const *dialog;
    uint8_t dialog_count;
    int16_t pellet_x[5], pellet_y[5];
    uint8_t pellet_active[5];
    int16_t ring_x[24], ring_y[24];
} f;

static const uint8_t frisk_first[4] = {
    UTAREA1_frisk_frisk_d0_index, UTAREA1_frisk_frisk_r0_index,
    UTAREA1_frisk_frisk_u0_index, UTAREA1_frisk_frisk_l0_index
};
static const uint8_t frisk_count[4] = {4, 2, 4, 2};
static const uint8_t face_index[] = {
    UTFLOW3_flowey_battle_a_floweynice0_index, UTFLOW3_flowey_battle_a_floweyniceside0_index,
    UTFLOW3_flowey_battle_a_floweynicesideum0_index, UTFLOW3_flowey_battle_a_floweysassy0_index,
    UTFLOW3_flowey_battle_a_floweyevil0_index, UTFLOW3_flowey_battle_a_floweygrin0_index,
    UTFLOW3_flowey_battle_a_floweylaugh0_index, UTFLOW3_flowey_battle_a_floweypissed0_index,
    UTFLOW3_flowey_battle_a_floweywink0_index, UTFLOW3_flowey_battle_a_floweyside0_index,
    UTFLOW3_flowey_battle_a_floweysideshock0_index,
    UTFLOW3_flowey_battle_a_floweyhurt0_index
};
static const uint8_t face_count[] = {2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1};

static void phase(uint8_t p)
{
    f.phase = p;
    f.tick = 0;
    f.page = 0;
    f.letters = 0;
    f.dialog = 0;
    f.dialog_count = 0;
}

static void dialogue(uint8_t p, const char *const *pages, uint8_t count)
{
    phase(p);
    f.dialog = pages;
    f.dialog_count = count;
}

/* textdata_en uses & for a line, ^n for pauses, and backslash codes for
   portrait/color changes. Keep the original text and consume those codes. */
static uint16_t visible_count(const char *s)
{
    uint16_t n = 0;
    while (*s && *s != '/' && *s != '%') {
        if (*s == '\\' && s[1]) {
            s += 2;
            if (s[-1] == 'E' && *s >= '0' && *s <= '9') s++;
            continue;
        }
        if (*s == '^' && s[1]) { s += 2; continue; }
        n++;
        s++;
    }
    return n;
}

static void draw_markup(const char *s, int x, int y, uint16_t limit, bool battle)
{
    uint16_t n = 0;
    int start = x;
    uint8_t color = battle ? BLACK : COL_WHITE;
    const font_t *font = battle ? &font_small : &font_main;
    while (*s && *s != '/' && *s != '%' && n < limit) {
        if (*s == '\\' && s[1]) {
            char code = s[1];
            s += 2;
            if (code == 'E' && *s >= '0' && *s <= '9') s++;
            if (!battle) {
                if (code == 'Y') color = COL_YELLOW;
                else if (code == 'B') color = COL_BLUE;
                else if (code == 'R') color = COL_RED;
                else if (code == 'W' || code == 'X') color = COL_WHITE;
            }
            continue;
        }
        if (*s == '^' && s[1]) { s += 2; continue; }
        if (*s == '&') {
            x = start;
            y += battle ? 7 : 16;
        } else {
            char one[2] = {*s, 0};
            draw_char(font, *s, x, y, color);
            x += text_width(font, one);
        }
        n++;
        s++;
    }
}

/* True once the player has completed all pages. Confirm first completes a
   typing page, then advances it, as with OBJ_WRITER. */
static bool dialogue_step(void)
{
    uint16_t length;
    if (!f.dialog || f.page >= f.dialog_count) return true;
    length = visible_count(f.dialog[f.page]);
    if (keys_pressed & K_CONFIRM) {
        if (f.letters < length) f.letters = length;
        else {
            f.page++;
            f.letters = 0;
            if (f.page >= f.dialog_count) return true;
        }
    } else if (f.letters < length && (f.tick & 1) == 0) {
        f.letters++;
    }
    return false;
}

static void draw_dialogue(bool battle)
{
    int x = battle ? 181 : 84;
    int y = battle ? 72 : 20;
    if (!f.dialog || f.page >= f.dialog_count) return;
    gfx_SetColor(COL_WHITE);
    if (battle) {
        gfx_TransparentSprite((gfx_sprite_t *)UTFLOW4_appvar[
            UTFLOW4_flowey_battle_b_bubble0_index], 161, 67);
    } else {
        gfx_FillRectangle(16, 5, 288, 75);
        gfx_SetColor(BLACK);
        gfx_FillRectangle(19, 8, 282, 69);
        if (f.phase == TORIEL_ROOM)
            gfx_TransparentSprite((gfx_sprite_t *)UTFLOW4_appvar[
                UTFLOW4_flowey_battle_b_face_torielhappytalk0_index + ((f.tick / 7) & 1)],
                34, 24);
        else
            gfx_TransparentSprite((gfx_sprite_t *)UTFLOW3_appvar[
                UTFLOW3_flowey_battle_a_floweynice0_index + ((f.tick / 7) & 1)],
                32, 20);
    }
    draw_markup(f.dialog[f.page], x, y, f.letters, battle);
    if (f.letters >= visible_count(f.dialog[f.page]))
        draw_text(battle ? &font_small : &font_main, "v",
                  battle ? 267 : 280, battle ? 110 : 65,
                  battle ? BLACK : COL_WHITE);
}

static bool wall_at(int x, int y)
{
    for (uint8_t i = 0; i < COUNT(flowey_walls); i++) {
        const int16_t *w = flowey_walls[i];
        if (x + 19 >= w[0] && x < w[0] + w[2] &&
            y + 29 >= w[1] && y + 19 < w[1] + w[3]) return true;
    }
    return false;
}

static void walk(void)
{
    int x = f.x, y = f.y;
    if (keys_held & K_LEFT) { x -= 3; f.facing = FACE_LEFT; }
    if (keys_held & K_RIGHT) { x += 3; f.facing = FACE_RIGHT; }
    if (keys_held & K_UP) { y -= 3; f.facing = FACE_UP; }
    if (keys_held & K_DOWN) { y += 3; f.facing = FACE_DOWN; }
    if (x >= 0 && x <= 300 && !wall_at(x, f.y)) f.x = x;
    if (y >= 0 && y <= 390 && !wall_at(f.x, y)) f.y = y;
}

static void clipped_sprite(const gfx_sprite_t *sprite, int x, int y)
{
    for (int j = 0; j < sprite->height; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= 240) continue;
        for (int i = 0; i < sprite->width; i++) {
            int dx = x + i;
            uint8_t c = sprite->data[j * sprite->width + i];
            if (dx >= 0 && dx < 320 && c != 14) gfx_vbuffer[dy][dx] = c;
        }
    }
}

static void draw_room(void)
{
    int vy = f.y - 110;
    if (vy < 0) vy = 0;
    if (vy > 180) vy = 180;
    gfx_SetColor(BLACK);
    gfx_FillRectangle(0, 0, 320, 240);
    gfx_Tilemap(&map, 0, vy);
    if (f.phase != FOLLOW_TORIEL && f.phase != LEAVE_ROOM && f.phase != BACK_TO_ROOM &&
        f.phase != TORIEL_ROOM) {
        gfx_TransparentSprite((gfx_sprite_t *)UTFLOW1_appvar[UTFLOW1_flowey_room_a_flowey0_index + ((f.tick / 18) & 1)],
                              148, 285 - vy);
    }
    if (f.phase == BACK_TO_ROOM || f.phase == TORIEL_ROOM ||
        f.phase == FOLLOW_TORIEL || f.phase == LEAVE_ROOM) {
        if (f.toriel_y >= vy - 58 && f.toriel_y < vy + 240)
            clipped_sprite((gfx_sprite_t *)UTFLOW1_appvar[
                UTFLOW1_flowey_room_a_toriel_d0_index + ((f.tick / 4) & 3)],
                146, f.toriel_y - vy);
    }
    gfx_TransparentSprite((gfx_sprite_t *)UTAREA1_appvar[
        frisk_first[f.facing] + ((keys_held & (K_UP | K_DOWN | K_LEFT | K_RIGHT))
        ? (f.tick / 5) % frisk_count[f.facing] : 0)], f.x, f.y - vy);
    if (f.dialog) draw_dialogue(false);
}

static void draw_face(void)
{
    uint8_t frame = (f.tick / 5) % face_count[f.face];
    gfx_TransparentSprite((gfx_sprite_t *)UTFLOW3_appvar[face_index[f.face] + frame], 140, 67);
}

static void draw_battle(void)
{
    gfx_SetColor(BLACK);
    gfx_FillRectangle(0, 0, 320, 240);
    gfx_SetColor(COL_WHITE);
    gfx_Rectangle(108, 126, 105, 66);
    if (f.phase == RESCUE) {
        gfx_TransparentSprite((gfx_sprite_t *)UTFLOW4_appvar[
            UTFLOW4_flowey_battle_b_torielside10_index + ((f.tick / 8) & 1)], 137, 48);
    } else {
        draw_face();
    }
    if (f.phase == PELLET_WAVE) {
        for (uint8_t i = 0; i < 5; i++) if (f.pellet_active[i])
            gfx_TransparentSprite((gfx_sprite_t *)UTFLOW4_appvar[
                UTFLOW4_flowey_battle_b_battlebullet0_index + ((f.tick / 3) & 1)],
                f.pellet_x[i], f.pellet_y[i]);
    }
    if (f.phase == RADIAL) {
        for (uint8_t i = 0; i < 24; i++)
            gfx_TransparentSprite((gfx_sprite_t *)UTFLOW4_appvar[
                UTFLOW4_flowey_battle_b_battlebullet0_index + ((f.tick / 3) & 1)],
                f.ring_x[i], f.ring_y[i]);
    }
    if (f.phase == RADIAL && f.tick > 55 && f.tick < 85)
        gfx_TransparentSprite((gfx_sprite_t *)UTFLOW4_appvar[
            UTFLOW4_flowey_battle_b_torielflame0_index + ((f.tick / 3) & 3)], 220 - (f.tick - 55) * 3, 70);
    gfx_TransparentSprite((gfx_sprite_t *)UTFLOW4_appvar[
        UTFLOW4_flowey_battle_b_battleheart0_index + ((f.invincible && (f.tick & 2)) ? 1 : 0)],
        f.heart_x, f.heart_y);
    if (f.dialog) draw_dialogue(true);
    gfx_SetColor(COL_WHITE);
    draw_text(&font_main, "LV 1", 26, 209, COL_WHITE);
    draw_text(&font_main, "HP", 128, 209, COL_WHITE);
    gfx_SetColor(COL_YELLOW);
    gfx_FillRectangle(157, 207, f.hp * 2, 10);
    gfx_SetColor(COL_WHITE);
    draw_text(&font_main, f.hp == 1 ? "01 / 20" : "20 / 20", 204, 209, COL_WHITE);
}

static void spawn_pellets(void)
{
    static const int8_t dx[5] = {-25, -12, 0, 12, 25};
    for (uint8_t i = 0; i < 5; i++) {
        f.pellet_x[i] = 156 + dx[i];
        f.pellet_y[i] = 106 + (i & 1) * 5;
        f.pellet_active[i] = 1;
    }
}

static void spawn_radial(void)
{
    static const int8_t x[24] = {
        70, 68, 61, 49, 35, 18, 0, -18, -35, -49, -61, -68,
        -70, -68, -61, -49, -35, -18, 0, 18, 35, 49, 61, 68
    };
    static const int8_t y[24] = {
        0, 18, 35, 49, 61, 68, 70, 68, 61, 49, 35, 18,
        0, -18, -35, -49, -61, -68, -70, -68, -61, -49, -35, -18
    };
    for (uint8_t i = 0; i < 24; i++) {
        f.ring_x[i] = 156 + x[i];
        f.ring_y[i] = 158 + y[i];
    }
}

static void heart_move(void)
{
    if (keys_held & K_LEFT) f.heart_x -= 2;
    if (keys_held & K_RIGHT) f.heart_x += 2;
    if (keys_held & K_UP) f.heart_y -= 2;
    if (keys_held & K_DOWN) f.heart_y += 2;
    if (f.heart_x < 110) f.heart_x = 110;
    if (f.heart_x > 202) f.heart_x = 202;
    if (f.heart_y < 128) f.heart_y = 128;
    if (f.heart_y > 182) f.heart_y = 182;
}

void flowey_begin(void)
{
    f.x = 171;
    f.y = 360;
    f.facing = FACE_UP;
    f.retries = 0;
    f.hp = 20;
    f.heart_x = 154;
    f.heart_y = 156;
    f.toriel_y = -60;
    f.face = NICE;
    f.invincible = 0;
    phase(APPROACH);
    map.map = flowey_tilemap;
    map.tiles = flowey_tiles_tiles;
    map.type_width = gfx_tile_no_pow2;
    map.type_height = gfx_tile_no_pow2;
    map.tile_width = map.tile_height = 20;
    map.draw_width = 17;
    map.draw_height = 13;
    map.width = FLOWEY_TILES_W;
    map.height = FLOWEY_TILES_H;
    map.x_loc = map.y_loc = 0;
    gfx_SetDrawBuffer();
    gfx_SetClipRegion(0, 0, LCD_WIDTH, LCD_HEIGHT);
    gfx_SetTransparentColor(14);
    set_palette(256, 0, 0);
}

bool flowey_step(void)
{
    f.tick++;
    if (f.invincible) f.invincible--;
    switch (f.phase) {
    case APPROACH:
        walk();
        if (f.y <= 330) dialogue(OPENING, flowey_opening, COUNT(flowey_opening));
        break;
    case OPENING:
        if (dialogue_step()) phase(TO_BATTLE);
        break;
    case TO_BATTLE:
        if (f.tick >= 42) dialogue(SOUL, flowey_soul, COUNT(flowey_soul));
        break;
    case SOUL:
        if (f.page >= 2) f.face = SASSY;
        if (dialogue_step()) {
            f.face = NICE_SIDE;
            dialogue(PELLET_TALK, flowey_pellets, COUNT(flowey_pellets));
        }
        break;
    case PELLET_TALK:
        if (dialogue_step()) { spawn_pellets(); phase(PELLET_WAVE); }
        break;
    case PELLET_WAVE:
        heart_move();
        for (uint8_t i = 0; i < 5; i++) if (f.pellet_active[i]) {
            int dx = f.heart_x - f.pellet_x[i];
            int dy = f.heart_y - f.pellet_y[i];
            f.pellet_x[i] += dx > 0 ? 1 : dx < 0 ? -1 : 0;
            f.pellet_y[i] += dy > 0 ? 1 : dy < 0 ? -1 : 0;
            if (abs(dx) < 7 && abs(dy) < 7 && !f.invincible) {
                f.hp = 1;
                f.invincible = 30;
                f.face = EVIL;
                dialogue(HIT, flowey_hit, COUNT(flowey_hit));
                break;
            }
        }
        if (f.phase == PELLET_WAVE && f.tick > 100) {
            f.retries++;
            f.face = f.retries > 1 ? PISSED : SASSY;
            if (f.retries == 1) dialogue(MISSED, flowey_missed, COUNT(flowey_missed));
            else if (f.retries == 2) dialogue(LAST_CHANCE, flowey_lastchance, COUNT(flowey_lastchance));
            else dialogue(KNOW, flowey_know, COUNT(flowey_know));
        }
        break;
    case MISSED:
        if (dialogue_step()) {
            f.face = NICE;
            spawn_pellets();
            phase(PELLET_WAVE);
        }
        break;
    case LAST_CHANCE:
        if (dialogue_step()) { f.face = GRIN; spawn_pellets(); phase(PELLET_WAVE); }
        break;
    case KNOW:
        if (dialogue_step()) { f.face = EVIL; dialogue(DIE, flowey_die, COUNT(flowey_die)); }
        break;
    case HIT:
        if (dialogue_step()) { f.face = EVIL; dialogue(DIE, flowey_die, COUNT(flowey_die)); }
        break;
    case DIE:
        if (dialogue_step()) { spawn_radial(); phase(RADIAL); }
        break;
    case RADIAL:
        heart_move();
        for (uint8_t i = 0; i < 24; i++) {
            f.ring_x[i] += (f.heart_x - f.ring_x[i]) / 14;
            f.ring_y[i] += (f.heart_y - f.ring_y[i]) / 14;
        }
        if (f.tick >= 65) f.face = HURT;
        if (f.tick >= 92) {
            f.hp = 20;
            dialogue(RESCUE, toriel_rescue, COUNT(toriel_rescue));
        }
        break;
    case RESCUE:
        if (dialogue_step()) { phase(BACK_TO_ROOM); f.toriel_y = 120; }
        break;
    case BACK_TO_ROOM:
        if (f.toriel_y < 258) f.toriel_y += 2;
        else dialogue(TORIEL_ROOM, toriel_room, COUNT(toriel_room));
        break;
    case TORIEL_ROOM:
        if (dialogue_step()) phase(FOLLOW_TORIEL);
        break;
    case FOLLOW_TORIEL:
        if (f.toriel_y > 150) f.toriel_y -= 2;
        walk();
        if (f.y < 185 && f.x > 130 && f.x < 180) phase(LEAVE_ROOM);
        break;
    case LEAVE_ROOM:
        if (f.tick >= 15) return false;
        break;
    }
    gfx_Wait();
    if (f.phase == APPROACH || f.phase == OPENING || f.phase == BACK_TO_ROOM ||
        f.phase == TORIEL_ROOM || f.phase == FOLLOW_TORIEL || f.phase == LEAVE_ROOM)
        draw_room();
    else draw_battle();
    if (f.phase == TO_BATTLE) set_palette(256,
        f.tick < 15 ? 256 - f.tick * 17 : f.tick < 30 ? (f.tick - 15) * 17 : 256, 0);
    else if (f.phase == LEAVE_ROOM) set_palette(256, 256 - f.tick * 17, 0);
    else set_palette(256, 256, 0);
#ifdef CE_FLOWEY_TEST
    draw_char(&font_main, 'A' + f.phase, 2, 2, COL_WHITE);
    draw_char(&font_main, f.dialog ? 'D' : '-', 12, 2, COL_WHITE);
#endif
    gfx_SwapDraw();
    return true;
}
