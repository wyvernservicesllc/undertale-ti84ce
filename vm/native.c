/* Native versions of the game's hottest code, run instead of the bytecode.
 *
 * obj_base_writer's Draw event (every line of dialogue in the game) walks
 * the whole visible message each frame, a few hundred VM instructions per
 * letter; in a battle that is most of the frame. native_writer_draw() does
 * the same in C: same variables, same order of random() calls and float
 * operations, the same scripts called. It handles English text; anything
 * else (Japanese, vertical text, the \z \* \> codes, shake modes above 38)
 * returns false and the bytecode runs as before.
 *
 * Ids come from vm/gen_native.h (tools/vmc.py). */
#include <stdlib.h>
#include <string.h>

#include "vm.h"
#include "gen_native.h"

#ifdef CE_PROFILE
#define NPROF_START uint32_t np0 = prof_now()
#define NPROF_END(k) prof_add(k, prof_now() - np0)
#else
#define NPROF_START
#define NPROF_END(k)
#endif

#ifndef NOINLINE
#define NOINLINE __attribute__((noinline))
#endif

bool native_enabled = true;

/* ---- helpers ---- */

/* The instance's variables looked up so far (a lookup costs ~4000 cycles
   on the calculator): valid until the variable arrays can move, when
   anything else runs or a variable gets created. */
#define WC_MAX 16
static uint16_t wc_ids[WC_MAX];
static value_t *wc_ptr[WC_MAX];
static int wc_n;

static void wc_reset(void)
{
    wc_n = 0;
}

static NOINLINE value_t *ivp(instance_t *in, int id)
{
    value_t *p;
    for (int i = 0; i < wc_n; i++)
    {
        if (wc_ids[i] == (uint16_t)id)
        {
            return wc_ptr[i];
        }
    }
    p = inst_var(in, (uint16_t)id, false);
    if (p && wc_n < WC_MAX)
    {
        wc_ids[wc_n] = (uint16_t)id;
        wc_ptr[wc_n++] = p;
    }
    return p;
}

static NOINLINE value_t ivv(instance_t *in, int id)
{
    value_t *p = ivp(in, id);
    return p ? *p : v_real(0);
}

static NOINLINE gmreal_t ivr(instance_t *in, int id)
{
    return v_num(ivv(in, id));
}

static NOINLINE void ivset_v(instance_t *in, int id, value_t v)
{
    value_t *p = ivp(in, id);
    if (p)
    {
        v_release(*p);
        *p = v;
        return;
    }
    inst_set(in, (uint16_t)id, v); /* a new variable: the arrays may move */
    wc_reset();
}

static NOINLINE void ivset(instance_t *in, int id, gmreal_t r)
{
    ivset_v(in, id, v_real(r));
}

static NOINLINE const char *ivs(instance_t *in, int id)
{
    return v_cstring(ivv(in, id));
}

static NOINLINE void gset(int g, value_t v)
{
    v_release(globals[g]);
    globals[g] = v;
}

/* string_char_at(s, n) (1-based, UTF-8): start and length in bytes */
static NOINLINE int char_at(const char *s, int n, const char **p)
{
    int i = n - 1, a = 0, b;
    if (i < 0)
    {
        i = 0;
    }
    while (s[a] && i > 0)
    {
        a++;
        while ((s[a] & 0xc0) == 0x80)
        {
            a++;
        }
        i--;
    }
    b = a;
    if (s[b])
    {
        b++;
        while ((s[b] & 0xc0) == 0x80)
        {
            b++;
        }
    }
    *p = s + a;
    return b - a;
}

/* string_char_at(s, n) == c, for an ASCII c */
static NOINLINE bool char_is(const char *s, int n, char c)
{
    const char *p;
    return char_at(s, n, &p) == 1 && *p == c;
}

/* real(string_char_at(s, n)) */
static NOINLINE value_t char_real(const char *s, int n)
{
    const char *p;
    int len = char_at(s, n, &p);
    char buf[8];
    if (len > 7)
    {
        len = 7;
    }
    memcpy(buf, p, len);
    buf[len] = 0;
    return v_real((gmreal_t)atof(buf));
}

static NOINLINE gmreal_t call_round(gmreal_t x)
{
    value_t a = v_real(x);
    return v_num(builtin_call(F_round, 1, &a));
}

/* GML's a > b, a == b on reals (with GameMaker's epsilon) */
static NOINLINE bool gt(gmreal_t a, gmreal_t b)
{
    return v_compare(v_real(a), v_real(b)) > 0;
}

static NOINLINE bool eq(gmreal_t a, gmreal_t b)
{
    return v_equal(v_real(a), v_real(b));
}

/* SCR_NEWLINE (horizontal text: vertical text never gets here) */
static NOINLINE void newline(instance_t *in)
{
    ivset(in, IV_myx, ivr(in, IV_writingx));
    ivset(in, IV_myy, ivr(in, IV_myy) + ivr(in, IV_vspacing));
}

/* ---- obj_base_writer, Draw ---- */

/* the codes after a backslash that only the bytecode handles */
static bool writer_supported(instance_t *in, const char *s)
{
    int stringpos = v_int(inst_get(in, IV_stringpos));
    const char *g = v_cstring(globals[GV_language]);
    if (strcmp(g, "en") || v_truthy(inst_get(in, IV_vtext)) || gt(ivr(in, IV_shake), 38))
    {
        return false;
    }
    for (int n = 1; n <= stringpos + 1 && s[0]; n++)
    {
        const char *p;
        if (char_at(s, n, &p) == 1 && *p == '\\')
        {
            if (char_at(s, n + 1, &p) == 1 && (*p == 'z' || *p == '*' || *p == '>'))
            {
                return false;
            }
        }
    }
    return true;
}

static bool writer_draw(instance_t *in)
{
    const char *s = ivs(in, IV_originalstring);
    int halfsize = 0;

    if (!writer_supported(in, s))
    {
        return false;
    }
    wc_reset();
    ivset(in, IV_myx, ivr(in, IV_writingx));
    ivset(in, IV_myy, ivr(in, IV_writingy));

    for (int n = 1; !gt((gmreal_t)n, ivr(in, IV_stringpos)); n++) /* n <= stringpos */
    {
        const char *p;
        int len;
        s = ivs(in, IV_originalstring);
        len = char_at(s, n, &p);
        if (len == 1 && *p == '^' && !char_is(s, n + 1, '0'))
        {
            n++;
        }
        else if (len == 1 && *p == '\\')
        {
            char c;
            n++;
            len = char_at(s, n, &p);
            c = len == 1 ? *p : 0;
            switch (c)
            {
            case 'R': ivset(in, IV_mycolor, 255); break;
            case 'G': ivset(in, IV_mycolor, 65280); break;
            case 'W': ivset(in, IV_mycolor, 16777215); break;
            case 'Y': ivset(in, IV_mycolor, 65535); break;
            case 'X': ivset(in, IV_mycolor, 0); break;
            case 'B': ivset(in, IV_mycolor, 16711680); break;
            case 'O': ivset(in, IV_mycolor, 4235519); break;
            case 'L': ivset(in, IV_mycolor, 16629774); break;
            case 'P': ivset(in, IV_mycolor, 16711935); break;
            case 'p': ivset(in, IV_mycolor, 13941759); break;
            case 'C':
            {
                value_t a = v_real(1);
                v_release(builtin_call(F_event_user, 1, &a));
                wc_reset();
                break;
            }
            case 'M':
                n++;
                arr_set(&globals[GV_flag], 20, char_real(s, n));
                break;
            case 'E':
                n++;
                gset(GV_faceemotion, char_real(s, n));
                break;
            case 'F':
                n++;
                gset(GV_facechoice, char_real(s, n));
                gset(GV_facechange, v_real(1));
                break;
            case 'S':
                n++;
                break;
            case 'T':
            {
                char t;
                n++;
                len = char_at(s, n, &p);
                t = len == 1 ? *p : 0;
                if (t == '-')
                {
                    halfsize = 1;
                }
                else if (t == '+')
                {
                    halfsize = 0;
                }
                else
                {
                    static const char codes[] = "Tt0SFsPMUAaR";
                    static const uint8_t typers[] = { 4, 48, 5, 10, 16, 17, 18, 27, 37, 47, 60, 76 };
                    const char *k = t ? strchr(codes, t) : NULL;
                    value_t a;
                    if (k)
                    {
                        gset(GV_typer, v_real(typers[k - codes]));
                    }
                    a = globals[GV_typer];
                    v_retain(a);
                    v_release(vm_call(CODE_gml_Script_SCR_TEXTTYPE, in, vm_other, 1, &a));
                    wc_reset();
                    v_release(a);
                    gset(GV_facechange, v_real(1));
                }
                break;
            }
            default:
                break;
            }
        }
        else if (len == 1 && *p == '&')
        {
            newline(in);
        }
        else if (len == 1 && *p == '/')
        {
            ivset(in, IV_halt, 1);
            if (char_is(s, n + 1, '%'))
            {
                ivset(in, IV_halt, 2);
            }
            else if (char_is(s, n + 1, '^') && !char_is(s, n + 2, '0'))
            {
                ivset(in, IV_halt, 4);
            }
            else if (char_is(s, n + 1, '*'))
            {
                ivset(in, IV_halt, 6);
            }
            break;
        }
        else if (len == 1 && *p == '%')
        {
            value_t a, r;
            if (char_is(s, n + 1, '%'))
            {
                v_release(builtin_call(F_instance_destroy, 0, NULL));
                break;
            }
            ivset(in, IV_stringno, ivr(in, IV_stringno) + 1);
            a = inst_get_arr(in, IV_mystring, v_int(ivv(in, IV_stringno)));
            v_retain(a);
            r = vm_call(CODE_gml_Script_scr_replace_buttons_pc, in, vm_other, 1, &a);
            wc_reset();
            v_release(a);
            ivset_v(in, IV_originalstring, r);
            ivset(in, IV_stringpos, 0);
            ivset(in, IV_myx, ivr(in, IV_writingx));
            ivset(in, IV_myy, ivr(in, IV_writingy));
            builtin_var_set(in, V_alarm, 0, v_real(ivr(in, IV_textspeed)));
            wc_reset();
            break;
        }
        else
        {
            /* a letter */
            const char *letter;
            int llen = char_at(s, n, &letter);
            char lc = llen == 1 ? letter[0] : 0;
            char lbuf[8];
            gmreal_t letterx, offsetx = 0, offsety = 0, halfscale = 1, shake, finalx, finaly;
            int font;
            if (llen > 7)
            {
                llen = 7;
            }
            memcpy(lbuf, letter, llen);
            lbuf[llen] = 0;
            if (lc == '^')
            {
                n++;
            }
            if (gt(ivr(in, IV_myx), ivr(in, IV_writingxend)))
            {
                newline(in);
            }
            letterx = ivr(in, IV_myx);
            if (halfsize)
            {
                halfscale = 0.5f;
                offsety += ivr(in, IV_vspacing) * 0.33f;
            }
            if (eq(v_num(globals[GV_typer]), 18))
            {
                switch (lc)
                {
                case 'l': case 'i': case 'I': case '!': case '.': case '?':
                    letterx += 2;
                    break;
                case 'S': case 'D': case 'A': case '\'':
                    letterx += 1;
                    break;
                default:
                    break;
                }
            }
            /* scr_setfont (English): draw_set_font */
            font = v_int(ivv(in, IV_myfont));
            gs.draw_font = font;
            gs.draw_color = (uint32_t)v_int(ivv(in, IV_mycolor)) & 0xffffff;
            {
                /* the shake: random() as many times as the bytecode, but the
                   position only matters for drawing, so no rounding to the
                   display grid here (the text drawing rounds anyway) */
                NPROF_START;
                gmreal_t r;
                shake = ivr(in, IV_shake);
                r = vm_random(shake);
                if (shake != 0)
                {
                    offsetx += r - shake / 2;
                }
                shake = ivr(in, IV_shake);
                r = vm_random(shake);
                if (shake != 0)
                {
                    offsety += r - shake / 2;
                }
                finalx = offsetx != 0 ? letterx + offsetx : letterx;
                finaly = offsety != 0 ? ivr(in, IV_myy) + offsety : ivr(in, IV_myy);
                NPROF_END(244);
            }
            {
                NPROF_START;
                gmreal_t hs = ivr(in, IV_htextscale), vs = ivr(in, IV_vtextscale);
                if (halfsize)
                {
                    hs *= halfscale;
                    vs *= halfscale;
                }
                draw_text_full(finalx, finaly, lbuf, hs, vs, 0, gs.draw_color, gs.draw_alpha);
                NPROF_END(243);
            }
            letterx += ivr(in, IV_spacing);
            if (font == FONT_fnt_comicsans)
            {
                switch (lc)
                {
                case 'w': case 'm': letterx += 2; break;
                case 'i': case 'l': letterx -= 2; break;
                case 's': case 'j': letterx -= 1; break;
                default: break;
                }
            }
            else if (font == FONT_fnt_papyrus)
            {
                switch (lc)
                {
                case 'D': case 'M': case 'C': case 'A': case 'H': case 'B': case 'G': letterx += 1; break;
                case 'Q': letterx += 3; break;
                case 'L': case 'K': case 'T': case 'F': case 'J': letterx -= 1; break;
                case '.': case '!': case '?': letterx -= 3; break;
                case 'O': case 'W': letterx += 2; break;
                case 'I': case '\'': letterx -= 6; break;
                case 'P': case 'R': letterx -= 2; break;
                default: break;
                }
            }
            if (halfsize)
            {
                gmreal_t myx = ivr(in, IV_myx);
                ivset(in, IV_myx, call_round(myx + (letterx - myx) / 2));
            }
            else
            {
                ivset(in, IV_myx, letterx);
            }
        }
    }
    return true;
}

/* Runs code natively when there is a native version: true if it did. */
bool native_run(uint16_t code, instance_t *self, instance_t *other)
{
    bool done = false;
    instance_t *saved_self = vm_self, *saved_other = vm_other;
    if (!native_enabled || !self)
    {
        return false;
    }
    vm_self = self;
    vm_other = other;
    if (code == CODE_gml_Script_scr_dogcheck && plat_file_exists("ut_packjump"))
    {
        /* Loading where a pack change saved: the game only lets saves
           load in rooms with save points (the rest get the dog), but a
           pack can end anywhere */
        ivset(self, IV_dogcheck, 1);
        done = true;
    }
    else if (code == CODE_gml_Object_obj_base_writer_Draw_0)
    {
        NPROF_START;
        done = writer_draw(self);
        NPROF_END(242);
    }
    vm_self = saved_self;
    vm_other = saved_other;
    wc_reset();
    return done;
}
