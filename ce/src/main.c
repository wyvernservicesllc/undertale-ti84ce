/*
 * Undertale for the TI-84 Plus CE: the platform side of the VM (vm/).
 *
 * The game data is a pack of archived AppVars UTD000, UTD001, ... (see
 * tools/cepack.py), read in place from flash. A RAM AppVar (UTRAM) holds
 * the code cache and a larger stack, since the OS stack is only ~4 KB.
 * Save files are AppVars named UTS + a hash of the file name; they stay in
 * RAM while playing (archiving could move the pack in flash) and are
 * archived on exit.
 */
#include <fileioc.h>
#include <graphx.h>
#include <keypadc.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#include "heap.h"
#include <sys/power.h>
#include <sys/timers.h>
#include <ti/getcsc.h>
#include <ti/screen.h>
#include <sys/util.h>
#include <graphx.h>
#include <math.h>
#include <time.h>

#include "../../vm/vm.h"

#define FPS 30
#define STACK_SIZE (5 * 1024)

void render_init(void);
void run_on_stack(void *stack_top, void (*fn)(void));
/* the C stack while the game runs (the OS's is only ~4 KB) */
static uint8_t c_stack[STACK_SIZE];

/* ---- platform services ---- */

static uint8_t keys[256]; /* held this frame, by virtual key code */

#ifdef CE_TEST
/* `make TEST=<frames>`: a deterministic run (time is the frame count, Z is
   pressed every 25 frames) that stops after that many frames with a hash
   of the game's state in test_result, read by tools/cetest.py. For
   checking that the calculator build computes exactly what it should. */
static uint32_t test_frame;
/* "TEST", hash, frames, stack used, globals hash, instances, then each
   instance's id and hash */
static size_t test_avail;
/* magic, hash, frames, stack used, globals hash, instances; 128 instance
   ids and hashes; VM errors (count, code, message), free RAM, heap peak and
   size; with CE_TEST_INST, that instance's variables */
#ifdef CE_TEST_INST
#define TEST_VARS 600
#else
#define TEST_VARS 0
#endif
volatile uint32_t test_result[275 + TEST_VARS]; /* BSS; test_finish sets the magic */
/* frames and clock ticks (32768 per second) from frame TIME_FROM to the end */
volatile uint32_t test_time[8] = { 0x454d4954 };
static uint8_t *test_stack;

uint32_t plat_time_ms(void)
{
    return test_frame * 33;
}

/* scripted input from the AppVar UTIN (tools/mkinput.py), else Z every
   25 frames */
static const uint8_t *test_in;

static void test_keys(void)
{
    if (!test_in)
    {
        uint8_t h = ti_Open("UTIN", "r");
        static const uint8_t none[12] = { 'U', 'T', 'I', '1', 25, 0, 0, 0, 0, 0, 0, 0 };
        test_in = none;
        if (h)
        {
            test_in = ti_GetDataPtr(h);
            ti_Close(h);
        }
    }
    {
        uint16_t mash = test_in[4] | test_in[5] << 8;
        uint32_t from = test_in[6] | (uint32_t)test_in[7] << 8 | (uint32_t)test_in[8] << 16;
        uint16_t n = test_in[10] | test_in[11] << 8;
        const uint8_t *r = test_in + 12;
        if (mash && test_frame >= from && test_frame % mash < 2)
        {
            keys['Z'] = 1;
        }
        for (uint16_t i = 0; i < n; i++, r += 6)
        {
            uint32_t f = r[0] | (uint32_t)r[1] << 8 | (uint32_t)r[2] << 16;
            if (r[4] >= 251 && r[4] <= 254)
            {
                /* commands: 251/252 go to room hold (+256), 253/254 start
                   battle group hold (+256) (tools/mkinput.py) */
                if (test_frame == f)
                {
                    int arg = r[5] + (r[4] == 252 || r[4] == 254 ? 256 : 0);
                    if (r[4] <= 252)
                    {
                        room_goto(arg);
                    }
                    else
                    {
                        vm_test_battle(arg);
                    }
                }
            }
            else if (test_frame >= f && test_frame < f + r[5])
            {
                keys[r[4]] = 1;
            }
        }
    }
}

static void test_finish(void)
{
#ifdef CE_TIME_FROM
    test_time[1] = (uint32_t)(clock() - test_time[2]);
    test_time[3] = test_frame - CE_TIME_FROM;
    test_time[5] = timer_2_Counter - test_time[4];
#endif
    test_result[0] = 0x54534554;
    test_result[1] = vm_state_hash();
    test_result[4] = vm_part_hash(-1);
    test_result[5] = (uint32_t)ninstances;
    for (int i = 0; i < ninstances && i < 128; i++)
    {
        test_result[6 + 2 * i] = (uint32_t)instances[i]->id;
        test_result[7 + 2 * i] = vm_part_hash(i);
    }
#ifdef CE_TEST_INST
    /* then one instance's variables */
    test_result[274] = (uint32_t)vm_inst_dump(CE_TEST_INST, (uint32_t *)&test_result[275], TEST_VARS);
#endif
    {
        extern uint32_t vm_errs;
        extern int32_t vm_err_code;
        extern char vm_err_msg[36];
        test_result[262] = vm_errs;
        test_result[263] = (uint32_t)vm_err_code;
        memcpy((void *)&test_result[264], vm_err_msg, 28);
        test_result[272] = heap_peak;
        test_result[273] = heap_total;
        test_result[271] = test_avail;
        {
            extern uint32_t test_nimg, test_nimg_skipped;
            extern gmreal_t render_scale, render_ox, render_oy;
            union { gmreal_t f; uint32_t u; } a, b, c;
            a.f = render_scale;
            b.f = render_ox;
            c.f = render_oy;
            test_result[256] = test_nimg;
            test_result[257] = test_nimg_skipped;
            test_result[258] = a.u;
            test_result[259] = b.u;
            test_result[260] = c.u;
            test_result[261] = (uint32_t)gs.views_enabled | (uint32_t)gs.views[0].enabled << 8 |
                               (uint32_t)gs.views[0].visible << 16 | (uint32_t)gs.room << 24;
        }
    }
    test_result[2] = test_frame;
    {
        uint32_t i = 0;
        while (i < STACK_SIZE && test_stack[i] == 0xa5)
        {
            i++;
        }
        test_result[3] = STACK_SIZE - i;
    }
    for (;;)
    {
    }
}
#else
uint32_t plat_time_ms(void)
{
    /* 32768 Hz clock -> ms without overflowing 32 bits */
    return (uint32_t)(clock() / 33);
}
#endif

/* no sound on the calculator */
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

void plat_log(const char *fmt, ...)
{
    (void)fmt;
}

/* "undertale.ini" -> "UTS" + 5 base-32 characters of a hash */
static void save_name(const char *name, char out[9])
{
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345";
    uint32_t h = 2166136261u;
    for (; *name; name++)
    {
        h = (h ^ (uint8_t)*name) * 16777619u;
    }
    out[0] = 'U';
    out[1] = 'T';
    out[2] = 'S';
    for (int i = 0; i < 5; i++)
    {
        out[3 + i] = digits[h & 31];
        h >>= 5;
    }
    out[8] = 0;
}

/* Saves while the game runs live here, in fixed RAM, and become archived
   AppVars when it ends (vfs_flush). Creating or growing a RAM variable can
   move the others, UTRAM with the heap in it among them. */
#define VFS_FILES 12
#define VFS_BYTES 4608 /* file0, file9 (1.7 KB each), undertale.ini and more */
static struct
{
    char var[9];
    uint8_t state; /* 0 unused, 1 data, 2 deleted */
    uint16_t off, len;
} vfs[VFS_FILES];
static uint8_t vfs_buf[VFS_BYTES];
static unsigned vfs_used;

static int vfs_find(const char *var)
{
    for (int i = 0; i < VFS_FILES; i++)
    {
        if (vfs[i].state && !strcmp(vfs[i].var, var))
        {
            return i;
        }
    }
    return -1;
}

/* takes a file's bytes out of the buffer */
static void vfs_drop(int i)
{
    if (vfs[i].state == 1)
    {
        unsigned end = vfs[i].off + vfs[i].len;
        memmove(vfs_buf + vfs[i].off, vfs_buf + end, vfs_used - end);
        vfs_used -= vfs[i].len;
        for (int k = 0; k < VFS_FILES; k++)
        {
            if (vfs[k].state == 1 && vfs[k].off >= end)
            {
                vfs[k].off -= vfs[i].len;
            }
        }
    }
    vfs[i].len = 0;
}

bool plat_file_read(const char *name, char **data, int *len)
{
    char var[9];
    uint8_t h;
    int i;
    save_name(name, var);
    i = vfs_find(var);
    if (i >= 0)
    {
        if (vfs[i].state == 2)
        {
            return false;
        }
        *len = vfs[i].len;
        *data = malloc(*len + 1);
        if (!*data)
        {
            return false;
        }
        memcpy(*data, vfs_buf + vfs[i].off, *len);
        (*data)[*len] = 0;
        return true;
    }
    h = ti_Open(var, "r");
    if (!h)
    {
        return false;
    }
    *len = (int)ti_GetSize(h);
    *data = malloc(*len + 1);
    if (!*data)
    {
        ti_Close(h);
        return false;
    }
    ti_Read(*data, 1, *len, h);
    (*data)[*len] = 0;
    ti_Close(h);
    return true;
}

bool plat_file_write(const char *name, const char *data, int len)
{
    char var[9];
    int i;
    save_name(name, var);
    i = vfs_find(var);
    if (i < 0)
    {
        for (i = 0; i < VFS_FILES && vfs[i].state; i++)
        {
        }
        if (i == VFS_FILES)
        {
            return false;
        }
        strcpy(vfs[i].var, var);
    }
    vfs_drop(i);
    if (vfs_used + (unsigned)len > VFS_BYTES)
    {
        vfs[i].state = 0;
        return false;
    }
    memcpy(vfs_buf + vfs_used, data, len);
    vfs[i].off = (uint16_t)vfs_used;
    vfs[i].len = (uint16_t)len;
    vfs[i].state = 1;
    vfs_used += len;
    return true;
}

bool plat_file_exists(const char *name)
{
    char var[9];
    uint8_t h;
    int i;
    save_name(name, var);
    i = vfs_find(var);
    if (i >= 0)
    {
        return vfs[i].state == 1;
    }
    h = ti_Open(var, "r");
    if (h)
    {
        ti_Close(h);
        return true;
    }
    return false;
}

void plat_file_delete(const char *name)
{
    char var[9];
    int i;
    save_name(name, var);
    i = vfs_find(var);
    if (i < 0)
    {
        for (i = 0; i < VFS_FILES && vfs[i].state; i++)
        {
        }
        if (i == VFS_FILES)
        {
            return;
        }
        strcpy(vfs[i].var, var);
    }
    vfs_drop(i);
    vfs[i].state = 2;
}

/* the saves to AppVars, archived (safe from RAM clears): once the game is
   over and UTRAM gone, so nothing can move under our feet */
static void vfs_flush(void)
{
    for (int i = 0; i < VFS_FILES; i++)
    {
        uint8_t h;
        if (vfs[i].state == 2)
        {
            ti_Delete(vfs[i].var);
            continue;
        }
        if (vfs[i].state != 1)
        {
            continue;
        }
        ti_Delete(vfs[i].var);
        h = ti_Open(vfs[i].var, "w");
        if (!h)
        {
            continue;
        }
        if (vfs[i].len)
        {
            ti_Write(vfs_buf + vfs[i].off, 1, vfs[i].len, h);
        }
        ti_SetArchiveStatus(true, h);
        ti_Close(h);
    }
}

/* ---- input: calculator keys -> GameMaker keys ---- */

static uint8_t keys[256];
static bool quit;

static void read_keys(void)
{
    kb_Scan();
    memset(keys, 0, sizeof keys);
    if (kb_Data[7] & kb_Left) keys[VK_LEFT] = 1;
    if (kb_Data[7] & kb_Right) keys[VK_RIGHT] = 1;
    if (kb_Data[7] & kb_Up) keys[VK_UP] = 1;
    if (kb_Data[7] & kb_Down) keys[VK_DOWN] = 1;
    /* Z / Enter: confirm */
    if (kb_Data[1] & kb_2nd) keys['Z'] = 1;
    if (kb_Data[6] & kb_Enter) keys[VK_ENTER] = 1;
    /* X / Shift: cancel */
    if (kb_Data[2] & kb_Alpha) keys['X'] = 1;
    if (kb_Data[1] & kb_Del) keys[VK_SHIFT] = 1;
    /* C / Ctrl: menu */
    if (kb_Data[1] & kb_Mode) keys['C'] = 1;
    /* Esc (hold to quit, like the PC version) */
    if (kb_Data[6] & kb_Clear) keys[VK_ESCAPE] = 1;
    /* [graph] leaves at once */
    if (kb_Data[1] & kb_Graph) quit = true;
}

/* ---- the game ---- */

#ifdef CE_BENCH
/* Microbenchmarks of the renderer's building blocks, printed as ms. */
#ifdef CE_FTEST
/* Float routine check (make FTEST=1): the UTFT AppVar holds records of
   x, y, x + y, x - y, x * y (5 floats) worked out on the Mac; the results go to
   bench_out: [0] records, [1] mismatches, [2..5] the first bad record's
   x, y, and what we got for x + y, x - y; [6] cycles for all of them;
   [7] what we got for x * y. */
extern volatile uint32_t bench_out[64];
static void float_test(void)
{
    uint8_t h = ti_Open("UTFT", "r");
    const uint32_t *r;
    uint32_t n, bad = 0, t0;
    if (!h)
    {
        return;
    }
    r = ti_GetDataPtr(h);
    n = ti_GetSize(h) / 20;
    ti_Close(h);
    timer_Disable(2);
    timer_Set(2, 0);
    timer_Enable(2, TIMER_CPU, TIMER_NOINT, TIMER_UP);
    t0 = timer_2_Counter;
    for (uint32_t i = 0; i < n; i++, r += 5)
    {
        union { float f; uint32_t u; } x, y, s, d, p;
        x.u = r[0];
        y.u = r[1];
        s.f = x.f + y.f;
        d.f = x.f - y.f;
        p.f = x.f * y.f;
        if (s.u != r[2] || d.u != r[3] || p.u != r[4])
        {
            if (!bad++)
            {
                bench_out[2] = x.u;
                bench_out[3] = y.u;
                bench_out[4] = s.u;
                bench_out[5] = d.u;
                bench_out[7] = p.u;
            }
        }
    }
    bench_out[6] = timer_2_Counter - t0;
    bench_out[0] = n;
    bench_out[1] = bad;
    for (;;)
    {
    }
}
#endif

/* results for tools/cebench.py: label pointer and ms, per line */
volatile uint32_t bench_out[64];
static int bench_n;

static void bench_line(const char *label, uint32_t ms)
{
    if (bench_n < 31)
    {
        bench_out[1 + 2 * bench_n] = (uint32_t)(uintptr_t)label;
        bench_out[2 + 2 * bench_n] = ms;
        bench_n++;
        bench_out[0] = (uint32_t)bench_n;
    }
    char buf[24];
    int i = 0;
    while (*label)
    {
        buf[i++] = *label++;
    }
    buf[i++] = ' ';
    {
        char tmp[12];
        int n = 0;
        do
        {
            tmp[n++] = (char)('0' + ms % 10);
            ms /= 10;
        } while (ms);
        while (n)
        {
            buf[i++] = tmp[--n];
        }
    }
    buf[i] = 0;
    os_PutStrFull(buf);
    os_NewLine();
}

static volatile gmreal_t bench_f = 1.5f, bench_g = 12345.6f;
gmreal_t gm_bench_turn_sin(unsigned t);
unsigned gm_bench_deg_turn(gmreal_t d);
gmreal_t gm_bench_fix(unsigned v);
static volatile int bench_sink;

static void bench(void)
{
    static uint8_t row[320];
    far_t img = 0;
    uint16_t entry = 0;
    uint32_t t;
    /* a full-screen image and a code entry that are in this pack */
    for (uint32_t i = 0; i < gd.nsprites && !img; i++)
    {
        if (SPR(i)->w == 320 && SPR(i)->h == 240)
        {
            img = sprite_image((int)i, 0);
        }
    }
    while (entry < gd.ncode && !code_nblocks(entry))
    {
        entry++;
    }
    if (!img || entry >= gd.ncode)
    {
        return;
    }
    os_ClrHome();
    t = plat_time_ms();
    for (int i = 0; i < 240; i++)
    {
        img_decode_row(img, i, row, 320);
    }
    bench_line("decode240", plat_time_ms() - t);
    t = plat_time_ms();
    for (int k = 0; k < 240; k++)
    {
        static uint8_t line[320];
        uint8_t *dst = line;
        for (int x = 0; x < 320; x++)
        {
            if (row[x])
            {
                dst[x] = row[x];
            }
        }
    }
    bench_line("copy76800", plat_time_ms() - t);
#define BENCH(label, body)                     \
    t = plat_time_ms();                        \
    for (int i = 0; i < 1000; i++)             \
    {                                          \
        body;                                  \
    }                                          \
    bench_line(label, plat_time_ms() - t)
    os_ClrHome();
    {
        /* the VM's building blocks, 1000 runs each: ms = 48 cycles per run */
        static uint16_t ids[24];
        static value_t vals[24];
        static instance_t fake;
        value_t args[2];
        for (int i = 0; i < 24; i++)
        {
            ids[i] = (uint16_t)(V_BUILTIN_COUNT + i * 3);
            vals[i] = v_real((gmreal_t)i);
        }
        fake.var_ids = ids;
        fake.var_vals = vals;
        fake.nvars = fake.capvars = 24;
        args[0] = v_real(3);
        args[1] = v_real(5);
        BENCH("empty", bench_sink++);
        BENCH("v_int", bench_sink = v_int(args[0]));
        BENCH("inst_var", bench_sink = (int)(intptr_t)inst_var(&fake, (uint16_t)(V_BUILTIN_COUNT + 33), false));
        BENCH("v_real", args[1] = v_real(bench_f));
        BENCH("call color", builtin_call(F_draw_set_color, 1, args));
        BENCH("call kbd", builtin_call(F_keyboard_check, 1, args));
        BENCH("call round", builtin_call(F_round, 1, args));
        BENCH("call floor", builtin_call(F_floor, 1, args));
        BENCH("code_block", bench_sink = (int)(intptr_t)code_block(entry, 0));
        BENCH("fadd", bench_f = bench_f + 1.25f);
        BENCH("fmul", bench_f = bench_f * 1.0001f);
        BENCH("fdiv", bench_f = bench_f / 1.0001f);
        BENCH("floorf", bench_f = floorf(bench_f) + 0.5f);
        BENCH("f>i", bench_sink = (int)bench_f);
        BENCH("i>f", bench_f = (gmreal_t)bench_sink);
        BENCH("v_compare", bench_sink = v_compare(args[0], args[1]));
        BENCH("v_equal", bench_sink = v_equal(args[0], args[1]));
        BENCH("call random", builtin_call(F_random, 1, args));
        BENCH("call surfw", builtin_call(F_surface_get_width, 1, args));
        BENCH("gm_dsin", bench_f = gm_dsin(bench_f));

        BENCH("gm_datan2", bench_f = gm_datan2(bench_f, 1.5f));

        BENCH("f>i32", bench_sink = (int)(int32_t)bench_f);
        BENCH("i32>f", bench_f = (gmreal_t)(int32_t)bench_sink);
        BENCH("call sin", builtin_call(F_sin, 1, args));
        BENCH("turn_sin", bench_f = gm_bench_turn_sin((unsigned)bench_sink));
        BENCH("deg_turn", bench_sink = (int)gm_bench_deg_turn(bench_f));
        BENCH("mulK", bench_g = bench_f * 46603.378f);
        BENCH("ftol 1e4", bench_sink = (int)(int32_t)bench_g);
        BENCH("fabsf", bench_f = fabsf(bench_f));
        BENCH("u24>f", bench_f = (gmreal_t)(unsigned)bench_sink);
        BENCH("fneg", bench_f = -bench_f);
    }
    t = plat_time_ms();
    for (int i = 0; i < 100; i++)
    {
        bench_sink += (int)clock();
    }
    bench_line("clock100", plat_time_ms() - t);
}
#endif

extern bool render_skip;

static void game_main(void)
{
    clock_t next = clock();
    int skipped = 0;
#ifdef CE_PROFILE
    vm_prof_reset();
    timer_Disable(2);
    timer_Set(2, 0);
    timer_Enable(2, TIMER_32K, TIMER_NOINT, TIMER_UP);
#endif
    game_start();
    while (!quit && !gs.end_game)
    {
        read_keys();
#ifdef CE_TEST
        memset(keys, 0, sizeof keys);
        test_keys();
        if (test_frame++ == CE_TEST)
        {
            test_finish();
        }
#ifdef CE_TIME_FROM
        test_time[6] = test_frame;
        test_time[7] = (uint32_t)clock();
        if (test_frame == CE_TIME_FROM)
        {
            test_time[2] = (uint32_t)clock();
#ifndef CE_PROFILE
            timer_Disable(2);
            timer_Set(2, 0);
            timer_Enable(2, TIMER_CPU, TIMER_NOINT, TIMER_UP);
#endif
            test_time[4] = timer_2_Counter;
        }
#endif
#if defined(CE_PROFILE) && defined(CE_PROF_FROM)
        /* profile only from that frame on (a PROFILE=1 TEST= build) */
        if (test_frame == CE_PROF_FROM)
        {
            vm_prof_reset();
        }
#endif
#endif
        input_update(keys);
#ifdef CE_NODRAW
        render_skip = true; /* tests: the game logic only */
#endif
        /* behind schedule: skip drawing this frame (at most 3 in a row) */
#if !defined(CE_PROFILE) && !defined(CE_TEST)
        render_skip = (long)(clock() - next) > CLOCKS_PER_SEC / FPS && skipped < 3;
        skipped = render_skip ? skipped + 1 : 0;
#endif
        game_frame();
        next += CLOCKS_PER_SEC / FPS;
#ifdef CE_TEST
        continue;
#endif
        if ((long)(clock() - next) > CLOCKS_PER_SEC)
        {
            next = clock(); /* far behind: don't try to catch up */
        }
        while ((long)(clock() - next) < 0)
        {
        }
    }
}

/* Show two lines until [enter] or [clear] (or 10 seconds pass). */
static int dbg_arch;
static void message(const char *line1, const char *line2)
{
    clock_t end = clock() + 10 * CLOCKS_PER_SEC;
    os_ClrHome();
    os_PutStrFull(line1);
    os_NewLine();
    os_PutStrFull(line2);
    do
    {
        kb_Scan();
    } while ((kb_Data[6] & (kb_Enter | kb_Clear)) && clock() < end);
    do
    {
        kb_Scan();
    } while (!(kb_Data[6] & (kb_Enter | kb_Clear)) && clock() < end);
}

/* ---- code that runs from the archive (tools/farcode.py) ---- */

extern uint8_t far_thunks[];
extern const unsigned far_nthunks, far_nimports;
extern const uint8_t far_imports[];
extern uint8_t farcode_ram[];

/* the far code in UTFAR, linked for this very program? Then point the
   call table at it and set up its variables. */
static bool far_init(const uint8_t *p)
{
    unsigned a = 0, b = 0;
    const unsigned *hd = (const unsigned *)(p + 4);
    const uint8_t *text;
    for (unsigned i = 0; i < far_nimports * 3; i++)
    {
        a = (a + far_imports[i]) & 0xffffff;
        b = (b + a) & 0xffffff;
    }
    if (memcmp(p, "UTFC", 4) || hd[0] != ((a ^ (b << 8)) & 0xffffff) || hd[1] != far_nimports ||
        hd[2] != far_nthunks)
    {
        return false;
    }
    text = (const uint8_t *)(hd + 7 + far_nthunks + hd[6]);
    for (unsigned i = 0; i < far_nthunks; i++)
    {
        *(const uint8_t **)(far_thunks + 4 * i + 1) = text + hd[7 + i];
    }
    memcpy(farcode_ram, text + hd[3], hd[4]);
    memset(farcode_ram + hd[4], 0, hd[5]);
    /* pointers to far code in its variables */
    for (unsigned i = 0; i < hd[6]; i++)
    {
        *(unsigned *)(farcode_ram + hd[7 + far_nthunks + i]) += (unsigned)text;
    }
    return true;
}

int main(void)
{
    uint8_t h;
    uint8_t *ram;
    int nwin = 0;
    const uint8_t *far = NULL;

    boot_Set48MHzMode(); /* full speed, whatever the OS left */
#ifdef CE_FTEST
    float_test();
#endif
    /* the OS's graph buffer is free while a program runs: more heap */
    heap_add((void *)0xD031F6, 8400);

    /* a program sent to RAM exists twice while it runs (the variable and
       the running copy): archive the variable to free that RAM */
    h = ti_OpenVar("UNDERTLE", "r", OS_TYPE_PROT_PRGM);
    if (!h)
    {
        h = ti_OpenVar("UNDERTLE", "r", OS_TYPE_PRGM);
    }
    dbg_arch = h ? 1 : 0;
    if (h)
    {
        if (!ti_IsArchived(h))
        {
            dbg_arch = 2 + (ti_SetArchiveStatus(true, h) ? 1 : 0);
        }
        ti_Close(h);
    }

    /* find the pack; archive any part that arrived in RAM first, since
       archiving can move the others in flash, then take the pointers */
    for (int pass = 0; pass < 2; pass++)
    {
        h = ti_Open("UTFAR", "r");
        if (h)
        {
            if (pass == 0 && !ti_IsArchived(h))
            {
                ti_SetArchiveStatus(true, h);
            }
            far = ti_IsArchived(h) ? ti_GetDataPtr(h) : NULL;
            ti_Close(h);
        }
        nwin = 0;
        for (int w = 0; w < PACK_MAX_WINDOWS; w++)
        {
            char name[9] = "UTD000";
            name[3] = (char)('0' + w / 100);
            name[4] = (char)('0' + w / 10 % 10);
            name[5] = (char)('0' + w % 10);
            h = ti_Open(name, "r");
            if (!h)
            {
                break;
            }
            if (pass == 0 && !ti_IsArchived(h) && !ti_SetArchiveStatus(true, h))
            {
                ti_Close(h);
                message("Not enough archive", "space for the pack.");
                return 1;
            }
            if (pass == 1)
            {
                pack_win[w] = ti_GetDataPtr(h);
            }
            ti_Close(h);
            nwin++;
        }
    }
    if (!far || !far_init(far))
    {
        message("Send UTFAR.8xv from", "this game's version.");
        return 1;
    }
    if (!nwin || !pack_win[0])
    {
        message("Send the game's", "UTD AppVars too.");
        return 1;
    }

    /* Free user RAM goes to malloc through a RAM AppVar (UTRAM), leaving a
       little for the OS and saves. The code cache then comes from malloc:
       a quarter of the extra RAM, at least 2 blocks. */
    ti_Delete("UTRAM");
    {
        void *unused;
        size_t avail = os_MemChk(&unused);
        size_t extra = avail > 1024 ? avail - 512 : 0; /* saves are written after UTRAM is gone */
        int blocks;
#ifdef CE_TEST
        test_avail = avail;
#endif
        if (extra > 65000)
        {
            extra = 65000; /* the AppVar size limit */
        }
        if (extra)
        {
            h = ti_Open("UTRAM", "w");
            if (h && ti_Resize(extra, h) == (int)extra)
            {
                ram = ti_GetDataPtr(h);
                heap_add(ram, extra);
            }
            if (h)
            {
                ti_Close(h);
            }
        }
        blocks = (int)(extra / 4 / CODE_BLOCK);
        if (blocks < 2)
        {
            blocks = 2;
        }
        if (blocks > CODE_CACHE_BLOCKS)
        {
            blocks = CODE_CACHE_BLOCKS;
        }
        pack_cache_mem = malloc((size_t)blocks * CODE_BLOCK);
        if (!pack_cache_mem)
        {
            char kb[24] = "Free some RAM (";
            int n = 15;
            int k = (int)(avail / 1024);
            if (k >= 10)
            {
                kb[n++] = (char)('0' + k / 10 % 10);
            }
            kb[n++] = (char)('0' + k % 10);
            memcpy(kb + n, "K free)", 8);
#ifdef CE_TEST
            test_result[0] = 0x54534554;
            test_result[271] = avail;
            for (;;)
            {
            }
#endif
            ti_Delete("UTRAM");
            message("Not enough RAM.", kb);
            return 1;
        }
        pack_cache_blocks = blocks;
    }

    if (!pack_init())
    {
        char line[20] = "found 000 of 000";
        uint32_t need = pack_win[0][0] == 'U' ? gd.nwindows : 0;
        line[6] = (char)('0' + nwin / 100);
        line[7] = (char)('0' + nwin / 10 % 10);
        line[8] = (char)('0' + nwin % 10);
        line[13] = (char)('0' + need / 100);
        line[14] = (char)('0' + need / 10 % 10);
        line[15] = (char)('0' + need % 10);
        ti_Delete("UTRAM");
        message("The UTD pack is incomplete:", line);
        return 1;
    }

#ifdef CE_BENCH
    bench();
    {
        clock_t end = clock() + 8 * CLOCKS_PER_SEC;
        while (clock() < end)
        {
        }
    }
#endif
    gfx_Begin();
    render_init();
    gfx_SetDrawBuffer();

#ifdef CE_TEST
    /* to see how much of the stack gets used */
    test_stack = c_stack;
    memset(test_stack, 0xa5, STACK_SIZE - 64);
#endif
    run_on_stack(c_stack + STACK_SIZE, game_main);

    gfx_End();
    ti_Delete("UTRAM");
    vfs_flush();
    if (gs.missing_room >= 0)
    {
        message("End of this pack. Send", "the next pack, then load.");
    }
    return 0;
}
