/*
 * A GameMaker: Studio 1.4 runner for Undertale, small enough for the
 * TI-84 Plus CE. It runs the game's own bytecode (compiled by tools/vmc.py)
 * and implements the GameMaker built-ins the game uses.
 *
 * The same C builds on the Mac (host/, for testing) and on the calculator
 * (ce/). Numbers are 32-bit floats everywhere, because that is what
 * `double` is on the CE.
 */
#ifndef VM_H
#define VM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gen_ids.h"

typedef float gmreal_t;

/* fast trigonometry (gmmath.c), degrees unless noted */
gmreal_t gm_dsin(gmreal_t deg);
gmreal_t gm_dcos(gmreal_t deg);
gmreal_t gm_sin(gmreal_t rad);
gmreal_t gm_cos(gmreal_t rad);
gmreal_t gm_datan2(gmreal_t y, gmreal_t x);
gmreal_t gm_atan2(gmreal_t y, gmreal_t x); /* radians */

/* ---- values ---- */

enum { VT_UNDEF, VT_REAL, VT_CSTR, VT_HSTR, VT_ARRAY };

typedef struct gmstr
{
    uint16_t refs;
    uint16_t len;
    char s[];
} gmstr_t;

struct gmarr;

typedef struct value
{
    uint8_t t;
    union
    {
        gmreal_t r;
        const char *cs;     /* VT_CSTR: string table, never freed */
        gmstr_t *hs;        /* VT_HSTR: heap, reference counted */
        struct gmarr *a;    /* VT_ARRAY: reference counted, copy on write */
    } u;
} value_t;

/* 24 bits on the calculator: 2D arrays (i * 32000 + j) still fit */
#ifdef __TICE__
typedef int24_t arr_idx_t;
#else
typedef int32_t arr_idx_t;
#endif
#define ARR_IDX_MAX 0x7fffff

typedef struct arr_entry
{
    arr_idx_t idx;
    value_t v;
} arr_entry_t;

typedef struct gmarr
{
    uint16_t refs;
    uint16_t n, cap;
    arr_entry_t *e; /* sorted by idx */
} gmarr_t;

static inline value_t v_real(gmreal_t r)
{
    value_t v;
    v.t = VT_REAL;
    v.u.r = r;
    return v;
}

static inline value_t v_undef(void)
{
    value_t v;
    v.t = VT_UNDEF;
    v.u.r = 0;
    return v;
}

static inline value_t v_cstr(const char *s)
{
    value_t v;
    v.t = VT_CSTR;
    v.u.cs = s;
    return v;
}

static inline bool v_is_str(value_t v)
{
    return v.t == VT_CSTR || v.t == VT_HSTR;
}

void v_retain(value_t v);
void v_release(value_t v);
value_t v_str(const char *s, int len); /* new heap string */
const char *v_cstring(value_t v);      /* string contents ("" if not a string) */
gmreal_t v_num(value_t v);               /* number value (0 for strings) */
bool v_truthy(value_t v);
int32_t v_int(value_t v);              /* truncated */
int32_t v_round(value_t v);            /* rounded, for bitwise ops etc. */
value_t v_tostring(value_t v);         /* string(), as a new value */
bool v_equal(value_t a, value_t b);
int v_compare(value_t a, value_t b);

/* arrays */
value_t arr_get(value_t arr, int32_t idx);
/* Writes into *slot, converting it to an array (copying it if shared). */
void arr_set(value_t *slot, int32_t idx, value_t v);

/* ---- game data: the pack (tools/cepack.py), read in place ---- */

/* A far address: window (addr >> 16) and offset (addr & 0xffff). The
   windows are the pack's AppVars on the calculator, or files loaded by the
   host. On the CE this is a native 24-bit int, much faster than 32-bit. */
#ifdef __TICE__
typedef unsigned int far_t;
#else
typedef uint32_t far_t;
#endif
#define PACK_MAX_WINDOWS 256
extern const uint8_t *pack_win[PACK_MAX_WINDOWS];

static inline const uint8_t *far_ptr(far_t a)
{
    /* byte loads instead of shifts: shifts are slow on the eZ80 */
    union
    {
        far_t v;
        uint8_t b[4];
        uint16_t lo;
    } u;
    u.v = a;
    return pack_win[u.b[2]] + u.lo;
}

far_t far_elem_big(far_t base, far_t l);

/* byte L of an array that starts at far address base (see tools/cepack.py:
   arrays over 32 KB are cut into 32 KB chunks in consecutive windows) */
static inline far_t far_elem(far_t base, far_t l)
{
    return l < 0x8000 ? base + l : far_elem_big(base, l);
}

static inline far_t rd_far(const uint8_t *p)
{
    return (far_t)p[0] | (far_t)p[1] << 8 | (far_t)p[2] << 16;
}

#define PACKED __attribute__((packed))
#define NOINLINE __attribute__((noinline))

typedef struct PACKED
{
    int16_t sprite, mask, parent;
    uint8_t flags; /* 1 visible, 2 solid, 4 persistent */
    int32_t depth;
    uint16_t nevents;
    uint8_t first_event[3]; /* u24: index of its first event record */
} obj_rec_t;
#define OBJ_FIRST_EVENT(o) ((uint32_t)(o)->first_event[0] | (uint32_t)(o)->first_event[1] << 8 | \
                            (uint32_t)(o)->first_event[2] << 16)

typedef struct PACKED
{
    uint8_t type, pad;
    uint16_t subtype, code;
} event_rec_t;

typedef struct PACKED
{
    uint16_t w, h;
    int16_t ox, oy, left, right, top, bottom;
    uint8_t precise, nmasks; /* nmasks: stored masks, else from the pixels */
    uint16_t frames;
    uint32_t first_frame;
    uint8_t name[3];
    uint8_t masks[3];        /* nmasks masks of h rows of (w + 7) / 8 bytes */
} sprite_rec_t;

typedef struct PACKED
{
    uint16_t w, h;
    uint8_t img[3], pad;
    uint8_t name[3];
} bg_rec_t;

typedef struct PACKED
{
    uint16_t first, last, nglyphs, pad;
    uint32_t first_glyph;
    uint8_t name[3];
} font_rec_t;

typedef struct PACKED
{
    uint16_t ch;
    uint8_t w, h;
    int8_t shift, offset;
    uint8_t pad;
    uint8_t bitmap[3]; /* 1 bpp rows, MSB first */
} glyph_rec_t;

typedef struct PACKED
{
    uint8_t smooth, closed;
    uint16_t npoints;
    uint32_t first_point;
} path_rec_t;

typedef struct PACKED
{
    float x, y, speed;
} path_point_t;

typedef struct PACKED
{
    uint8_t enabled, fg, htile, vtile;
    int16_t bg, x, y;
    int8_t hspeed, vspeed;
} room_bg_t;

typedef struct PACKED
{
    uint8_t enabled, pad;
    int16_t x, y, w, h, px, py, pw, ph, bx, by, sx, sy, follow;
} room_view_t;

typedef struct PACKED
{
    uint16_t w, h, speed, creation;
    uint8_t persistent, flags;
    uint16_t pad;             /* slot in room_bgs and room_views */
    uint32_t color;
    uint16_t ninst, ntiles;
    uint32_t first_inst, first_tile;
    uint8_t name[3];
} room_rec_t;

typedef struct PACKED
{
    int16_t obj, x, y;
    uint16_t creation, idoff; /* id - 100000 */
    int16_t xscale, yscale;   /* 8.8 fixed point */
} inst_rec_t;

typedef struct PACKED
{
    uint8_t bg, depth;        /* depth: index into the tile depth table */
    int16_t x, y;
    uint16_t sx, sy, w, h;
} tile_rec_t;

typedef struct
{
    uint32_t nwindows, ncode, nblocks, nscripts, nglobals, ninstvars, ntext, ntextblocks, ntexthashes;
    uint32_t nobjects, nsprites, nbgs, nfonts, npaths, nrooms, nroomorder;
    far_t code_entries, code_blocks, scripts, global_names, text_index, text_blocks;
    far_t text_hashes, text_ids, objects, events, sprites, frames, bgs, fonts, glyphs;
    far_t paths, points, rooms, room_bgs, room_views, insts, tiles, tile_depths, roomorder;
    far_t palette;
} gamedata_t;

extern gamedata_t gd;

bool pack_init(void);
#ifndef CODE_CACHE_BLOCKS
#define CODE_CACHE_BLOCKS 32
#endif
extern uint8_t *pack_cache_mem; /* pack_cache_blocks * CODE_BLOCK bytes, set before use */
extern int pack_cache_blocks;   /* 1 to CODE_CACHE_BLOCKS */
#define OBJ(i) ((const obj_rec_t *)far_ptr(far_elem(gd.objects, (far_t)(i) * 16)))
#define EVENT(i) ((const event_rec_t *)far_ptr(far_elem(gd.events, (far_t)(i) * 8)))
#define SPR(i) ((const sprite_rec_t *)far_ptr(far_elem(gd.sprites, (far_t)(i) * 32)))
#define BG(i) ((const bg_rec_t *)far_ptr(far_elem(gd.bgs, (far_t)(i) * 16)))
#define FONT(i) ((const font_rec_t *)far_ptr(far_elem(gd.fonts, (far_t)(i) * 16)))
#define GLYPH(i) ((const glyph_rec_t *)far_ptr(far_elem(gd.glyphs, (far_t)(i) * 16)))
#define PATH(i) ((const path_rec_t *)far_ptr(far_elem(gd.paths, (far_t)(i) * 8)))
#define POINT(i) ((const path_point_t *)far_ptr(far_elem(gd.points, (far_t)(i) * 16)))
#define ROOM(i) ((const room_rec_t *)far_ptr(far_elem(gd.rooms, (far_t)(i) * 32)))
#define ROOMBG(r, k) ((const room_bg_t *)far_ptr(far_elem(gd.room_bgs, ((far_t)ROOM(r)->pad * 8 + (k)) * 16)))
#define ROOMVIEW(r, k) ((const room_view_t *)far_ptr(far_elem(gd.room_views, ((far_t)ROOM(r)->pad * 8 + (k)) * 32)))
#define INST(i) ((const inst_rec_t *)far_ptr(far_elem(gd.insts, (far_t)(i) * 16)))
#define TILE(i) ((const tile_rec_t *)far_ptr(far_elem(gd.tiles, (far_t)(i) * 16)))
int32_t tile_depth(const tile_rec_t *t);
int room_order(int i);
const char *far_name(const uint8_t *name3); /* "" if none */

/* code: a 1 KB block of a code entry, decompressed into the block cache */
const uint8_t *code_block(uint16_t entry, uint16_t block);
uint16_t code_nlocals(uint16_t entry);
uint16_t code_nblocks(uint16_t entry);
#define CODE_BLOCK 1024
#define CODE_BLOCK_SHIFT 10
uint16_t script_code(int script); /* 0xffff if none */

/* text (textdata_en): by id or by key; a new heap string, or undefined */
value_t text_by_id(uint16_t id);
int text_find(const char *key); /* id or -1 */

/* images: RLE rows (see tools/cepack.py); 0 = not in this pack */
far_t sprite_image(int sprite, int frame);
far_t bg_image(int bg);
/* RLE data of row y and its end (tokens: see tools/cepack.py rle_row) */
const uint8_t *img_row(far_t img, int y, const uint8_t **end);
/* decode row y of an image of width w into out (0 = transparent) */
void img_decode_row(far_t img, int y, uint8_t *out, int w);
bool img_pixel(far_t img, int w, int x, int y);
extern uint8_t vm_rowbuf[1024]; /* scratch row for masks and img_pixel */

int data_find_object(const char *name);
int data_find_global(const char *name);
const char *code_name(uint16_t entry);

/* ds_map id that stands for the text table */
#define TEXT_MAP_ID 1000000

/* ---- instances ---- */

#define MAX_ALARMS 12

typedef struct var
{
    uint16_t id;
    value_t v;
} ivar_t;

/* Speeds, paths and alarms: most instances (walls, markers, text) never move by
   themselves, so these live apart. m points to motion_default (read only)
   until something sets one of them through inst_m(). */
typedef struct
{
    gmreal_t hspeed, vspeed, speed, direction, friction, gravity, gravity_direction;
    int16_t path_index;
    gmreal_t path_position, path_positionprevious, path_speed, path_scale, path_orientation;
    gmreal_t path_xstart, path_ystart;
    uint8_t path_endaction;
    int16_t alarm[MAX_ALARMS];
} motion_t;

typedef struct instance
{
    int32_t id;
    int16_t obj;
    uint8_t alive, visible, solid, persistent;
    gmreal_t x, y, xprevious, yprevious, xstart, ystart;
    int16_t sprite_index, mask_index;
    gmreal_t image_index, image_speed, image_xscale, image_yscale, image_angle, image_alpha;
    uint32_t image_blend;
    gmreal_t depth;
    uint8_t outside, at_boundary; /* for the outside room / boundary events */
    motion_t *m;
    /* variables: ids sorted, values alongside (separate arrays so lookups
       don't multiply by the 5-byte value size on the eZ80) */
    uint16_t *var_ids;
    value_t *var_vals;
    uint16_t nvars, capvars;
} instance_t;

extern const motion_t motion_default;
motion_t *inst_m(instance_t *in); /* in's own motion_t, to change it */

/* All live instances in creation order. */
extern instance_t **instances;
extern int ninstances;

instance_t *inst_create(int obj, gmreal_t x, gmreal_t y);
void inst_destroy(instance_t *inst, bool run_event);
instance_t *inst_find_id(int32_t id);
bool inst_is_a(const instance_t *inst, int obj); /* obj or a child of it */
/* Instances matching a GameMaker target (id, object index, all). */
int inst_select(int32_t target, instance_t **out, int max);

value_t *inst_var(instance_t *inst, uint16_t id, bool create);
value_t inst_get(instance_t *inst, uint16_t id);
void inst_set(instance_t *inst, uint16_t id, value_t v); /* takes ownership */
value_t inst_get_arr(instance_t *inst, uint16_t id, int32_t idx);
void inst_set_arr(instance_t *inst, uint16_t id, int32_t idx, value_t v);

/* bounding box in room coordinates; false if the instance has no mask */
bool inst_bbox(instance_t *inst, gmreal_t *l, gmreal_t *t, gmreal_t *r, gmreal_t *b);
bool inst_collide(instance_t *a, instance_t *b);
bool inst_collide_at(instance_t *a, gmreal_t x, gmreal_t y, instance_t *b);
bool inst_point(instance_t *inst, gmreal_t px, gmreal_t py, bool precise);
bool inst_rect(instance_t *inst, gmreal_t x1, gmreal_t y1, gmreal_t x2, gmreal_t y2, bool precise);

/* ---- events ---- */

enum
{
    EV_CREATE, EV_DESTROY, EV_ALARM, EV_STEP, EV_COLLISION, EV_KEYBOARD,
    EV_MOUSE, EV_OTHER, EV_DRAW, EV_KEYPRESS, EV_KEYRELEASE,
};
enum { EV_STEP_NORMAL, EV_STEP_BEGIN, EV_STEP_END };
enum
{
    EV_OTHER_OUTSIDE = 0, EV_OTHER_GAME_START = 2, EV_OTHER_GAME_END = 3,
    EV_OTHER_ROOM_START = 4, EV_OTHER_ROOM_END = 5, EV_OTHER_ANIMATION_END = 7,
    EV_OTHER_PATH_END = 8, EV_OTHER_USER0 = 10,
};

/* code id of an object's event, looking through parents; 0xffff if none.
   *owner gets the object that defines it. */
uint16_t event_find(int obj, int type, int sub, int *owner);
bool event_run(instance_t *inst, instance_t *other, int type, int sub);

/* ---- interpreter ---- */

value_t vm_call(uint16_t code, instance_t *self, instance_t *other, int argc, value_t *args);
value_t vm_call_script(int script, instance_t *self, instance_t *other, int argc, value_t *args);
extern instance_t *vm_self, *vm_other;
extern int vm_ev_obj, vm_ev_type, vm_ev_sub; /* event being run, for event_inherited */
extern value_t *globals;
void vm_error(const char *fmt, ...);

/* built-in functions */
value_t builtin_call(int f, int argc, value_t *args);
/* built-in variables that aren't per instance (room, view_xview, ...) */
bool builtin_var_get(instance_t *inst, uint16_t id, int32_t idx, value_t *out);
bool builtin_var_set(instance_t *inst, uint16_t id, int32_t idx, value_t v);

/* ---- runtime ---- */

#define MAX_VIEWS 8

typedef struct
{
    bool enabled, visible;
    gmreal_t x, y, w, h, px, py, pw, ph, hborder, vborder, hspeed, vspeed, angle;
    int follow;
} view_t;

typedef struct
{
    bool visible, foreground, htiled, vtiled;
    int index;
    gmreal_t x, y, hspeed, vspeed, xscale, yscale, alpha;
    uint32_t blend;
} background_t;

typedef struct
{
    int room;                 /* current room */
    int pending_room;         /* room_goto target, -1 none */
    int missing_room;         /* a room that isn't in this pack, -1 none */
    bool restart_game, end_game;
    int room_width, room_height, room_speed;
    uint32_t background_color;
    bool background_showcolor;
    view_t views[MAX_VIEWS];
    bool views_enabled;
    int view_current;
    background_t bgs[8];
    int32_t next_id;
    uint32_t frame;
    /* drawing state */
    uint32_t draw_color;
    gmreal_t draw_alpha;
    int draw_font, halign, valign;
} gamestate_t;

extern gamestate_t gs;

/* room flag: this room's contents are in another pack */
#define ROOM_NOT_IN_PACK 0x80
void game_start(void);
uint32_t vm_state_hash(void);
/* native versions of hot code (vm/native.c) */
extern bool native_enabled;
bool native_run(uint16_t code, instance_t *self, instance_t *other);
gmreal_t vm_random(gmreal_t range); /* GML random() */
void draw_text_full(gmreal_t x, gmreal_t y, const char *s, gmreal_t xs, gmreal_t ys, gmreal_t angle,
                    uint32_t color, gmreal_t alpha);
void vm_prof_reset(void); /* to compare runs */
uint32_t vm_part_hash(int part);
int vm_inst_dump(int32_t id, uint32_t *out, int max);
void game_frame(void); /* one step: events, motion, collisions, draw */
void room_goto(int room);
void vm_test_battle(int group); /* test hook */

/* ---- input (GameMaker virtual key codes) ---- */

enum
{
    VK_LEFT = 37, VK_UP = 38, VK_RIGHT = 39, VK_DOWN = 40, VK_ENTER = 13,
    VK_ESCAPE = 27, VK_SPACE = 32, VK_SHIFT = 16, VK_CONTROL = 17,
    VK_BACKSPACE = 8, VK_F4 = 115, VK_ANYKEY = 1, VK_NOKEY = 0,
};
extern uint8_t key_down[256], key_pressed[256], key_released[256];
void input_update(const uint8_t *now); /* now[256]: held this frame */

/* ---- rendering (implemented per platform) ---- */

#define SCREEN_W 320
#define SCREEN_H 240

void render_begin(uint32_t clear_color, bool clear);
void render_end(void);
/* RLE image (far address) of size w x h; draws its (sx, sy, sw, sh) part */
void render_image(far_t img, int w, int h, int sx, int sy, int sw, int sh,
                  gmreal_t x, gmreal_t y, gmreal_t xscale, gmreal_t yscale, gmreal_t angle,
                  gmreal_t ox, gmreal_t oy, uint32_t blend, gmreal_t alpha);
void render_rect(gmreal_t x1, gmreal_t y1, gmreal_t x2, gmreal_t y2, uint32_t color, gmreal_t alpha, bool outline);
void render_line(gmreal_t x1, gmreal_t y1, gmreal_t x2, gmreal_t y2, gmreal_t width, uint32_t color, gmreal_t alpha);
void render_circle(gmreal_t x, gmreal_t y, gmreal_t r, uint32_t color, gmreal_t alpha, bool outline);
void render_triangle(gmreal_t x1, gmreal_t y1, gmreal_t x2, gmreal_t y2, gmreal_t x3, gmreal_t y3, uint32_t color, gmreal_t alpha, bool outline);
/* 1 bpp glyph bitmap (far address), gw x gh */
void render_glyph(far_t bitmap, int gw, int gh, gmreal_t x, gmreal_t y, gmreal_t xscale, gmreal_t yscale,
                  gmreal_t angle, uint32_t color, gmreal_t alpha);
/* room -> screen: screen = (room + render_o) * render_scale. Views bigger
   than the screen (battles are 640x480) are scaled down to fit. */
extern gmreal_t render_ox, render_oy, render_scale;
/* when the view is 1:1 at whole pixels, its offset as ints (fast paths
   that avoid float math, which is very slow on the calculator) */
extern bool render_int;
extern int render_iox, render_ioy;
/* a 1:1, upright glyph at screen position (x, y) */
void render_glyph_screen(far_t bitmap, int gw, int gh, int x, int y, uint32_t color, gmreal_t alpha);
/* a glyph stretched to the screen rectangle (left, top, dw, dh) */
void render_glyph_px(far_t bitmap, int gw, int gh, int left, int top, int dw, int dh, uint32_t color, gmreal_t alpha);

/* ---- profiling (calculator builds with CE_PROFILE) ---- */

#ifdef CE_PROFILE
enum { PROF_BEGIN_STEP = 48, PROF_ALARMS, PROF_KEYS, PROF_STEP, PROF_MOTION, PROF_END_STEP, PROF_DRAW };
void prof_phase(uint8_t slot);
uint32_t prof_now(void);
void prof_add(uint16_t fslot, uint32_t cycles);
#else
#define prof_phase(slot) ((void)0)
#endif

/* ---- tracing (host builds record which assets a playthrough touches) ---- */

enum { TRACE_SPRITE, TRACE_BG, TRACE_FONT, TRACE_ROOM, TRACE_CODE };
#ifdef VM_TRACE
void trace_use(int kind, int index);
extern uint32_t trace_ops[8192]; /* instructions run per code entry */
#else
#define trace_use(kind, index) ((void)0)
#endif

/* ---- platform services ---- */

uint32_t plat_time_ms(void);
/* persistent files (ini/text saves); return NULL / false if missing */
bool plat_file_read(const char *name, char **data, int *len);
bool plat_file_write(const char *name, const char *data, int len);
bool plat_file_exists(const char *name);
void plat_file_delete(const char *name);
void plat_log(const char *fmt, ...);
/* sound (none on the calculator): play returns a handle, or -1 */
int plat_sound_play(int sound, bool loop, gmreal_t priority);
void plat_sound_stop(int handle_or_sound);
void plat_sound_stop_all(void);
void plat_sound_gain(int handle_or_sound, gmreal_t gain, int ms);
void plat_sound_pitch(int handle_or_sound, gmreal_t pitch);
bool plat_sound_playing(int handle_or_sound);
void plat_sound_pause(int handle_or_sound, bool pause);

/* GameMaker colors are 0xBBGGRR */
#define GM_R(c) ((c) & 0xff)
#define GM_G(c) (((c) >> 8) & 0xff)
#define GM_B(c) (((c) >> 16) & 0xff)

#endif
