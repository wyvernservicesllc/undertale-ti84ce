/* The bytecode interpreter (opcodes in gen_ids.h, from tools/vmc.py). */
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"

/* the interpreter's state; shared with the assembly fast path on the
   calculator (ce/src/vmfast.s), hence global names */
#define pc vm_pc
#define base vm_base
#define blk vm_blk
#define vsp vm_sp
#define cur vm_cur
#define stack vm_stack

enum { SC_SELF, SC_OTHER, SC_GLOBAL, SC_LOCAL, SC_STACKTOP, SC_INST, SC_ALL, SC_NOONE };
#define SC_ARRAY 0x80
#define SC_SWAP 0x40

/* GameMaker's special instance targets */
#define T_SELF (-1)
#define T_OTHER (-2)
#define T_ALL (-3)
#define T_NOONE (-4)
#define T_GLOBAL (-5)
#define T_LOCAL (-7)

#define STACK_SIZE 256 /* values; the game needs under 70 */
#define MAX_WITH 16

instance_t *vm_self, *vm_other;
int vm_ev_obj = -1, vm_ev_type = -1, vm_ev_sub = -1;
value_t *globals;

/* the value stack; vsp points past the top (a pointer, not an index: on
   the eZ80, indexing 5-byte values costs a library multiply) */
value_t stack[STACK_SIZE];
value_t *vsp = stack;
#define STACK_END (stack + STACK_SIZE)

/* the running call, for arguments and error messages */
typedef struct frame
{
    uint16_t code;
    int argc;
    value_t *args;
    value_t *locals;
} frame_t;

frame_t *cur;
static int depth;
unsigned vm_ops; /* instructions executed, for profiling (wraps) */

#ifdef CE_PROFILE
/* CPU cycles and counts per opcode (slot PROF_OUTSIDE: time outside the
   interpreter loop), read from RAM by the test tools */
#include <sys/timers.h>
#define PROF_SLOTS 64
#define PROF_OUTSIDE 55
struct
{
    char magic[4];
    uint32_t cycles[PROF_SLOTS];
    uint32_t count[PROF_SLOTS];
    uint32_t fcycles[288]; /* per built-in function (inclusive), then extras */
    uint32_t fcount[288];
} vm_prof; /* in the BSS: not in the program's RAM image */
static uint32_t prof_last;

void vm_prof_reset(void)
{
    memset(&vm_prof, 0, sizeof vm_prof);
    memcpy(vm_prof.magic, "PROF", 4);
}
uint8_t prof_prev = PROF_OUTSIDE;
#define PROF_OP(op)                                  \
    do                                               \
    {                                                \
        uint32_t n_ = timer_2_Counter;               \
        vm_prof.cycles[prof_prev] += n_ - prof_last; \
        vm_prof.count[prof_prev]++;                  \
        prof_last = n_;                              \
        prof_prev = (op);                            \
    } while (0)
void prof_phase(uint8_t slot)
{
    PROF_OP(slot);
}
uint32_t prof_now(void)
{
    return timer_2_Counter;
}
void prof_add(uint16_t fslot, uint32_t cycles)
{
    vm_prof.fcycles[fslot] += cycles;
    vm_prof.fcount[fslot]++;
}
#else
#define PROF_OP(op) ((void)0)
#endif

#if defined(__TICE__) && defined(CE_TEST)
uint32_t vm_errs;
int32_t vm_err_code;
char vm_err_msg[36];
#endif

void vm_error(const char *fmt, ...)
{
#ifdef __TICE__
    (void)fmt; /* no console on the calculator */
#ifdef CE_TEST
    /* tests report the first error (tools/cetest.py) */
    if (!vm_errs++)
    {
        vm_err_code = cur ? cur->code : -1;
        strncpy(vm_err_msg, fmt, sizeof vm_err_msg - 1);
    }
#endif
#else
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    plat_log("VM error in code %d: %s", cur ? cur->code : -1, buf);
#endif
}

#ifndef __TICE__
int vm_hw_stack, vm_hw_args, vm_hw_with; /* high-water marks (host) */
#define HW(m, v) ((v) > (m) ? (void)((m) = (v)) : (void)0)
#else
#define HW(m, v) ((void)0)
#endif

static inline void push(value_t v)
{
    HW(vm_hw_stack, (int)(vsp - stack) + 1);
    if (vsp >= STACK_END)
    {
        vm_error("stack overflow");
        v_release(v);
        return;
    }
    *vsp++ = v;
}

static inline value_t pop(void)
{
    if (vsp <= stack)
    {
        vm_error("stack underflow");
        return v_real(0);
    }
    return *--vsp;
}

static inline uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static inline unsigned rd24(const uint8_t *p)
{
#ifdef __TICE__
    return *(const unsigned *)p; /* native 24-bit load */
#else
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16;
#endif
}

static inline int32_t rd32s(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24);
}

/* ---- variable access ---- */

static bool is_arg_var(uint16_t id)
{
    return id == V_argument || id == V_argument_count || (id >= V_argument0 && id <= V_argument15);
}

static value_t arg_get(uint16_t id, int32_t idx)
{
    int n;
    if (id == V_argument_count)
    {
        return v_real((gmreal_t)(cur ? cur->argc : 0));
    }
    n = id == V_argument ? idx : id - V_argument0;
    if (!cur || n < 0 || n >= cur->argc)
    {
        return v_real(0);
    }
    return cur->args[n];
}

static void arg_set(uint16_t id, int32_t idx, value_t v)
{
    int n = id == V_argument ? idx : id - V_argument0;
    if (cur && n >= 0 && n < cur->argc)
    {
        v_release(cur->args[n]);
        cur->args[n] = v;
    }
    else
    {
        v_release(v);
    }
}

/* Instance for reading a variable of `target`. */
static instance_t *read_target(int32_t target)
{
    instance_t *list[1];
    if (target == T_SELF)
    {
        return vm_self;
    }
    if (target == T_OTHER)
    {
        return vm_other;
    }
    if (inst_select(target, list, 1) > 0)
    {
        return list[0];
    }
    return NULL;
}

static value_t get_var(int32_t target, uint16_t id, bool array, int32_t idx)
{
    instance_t *inst;
    value_t out;

    if (target == T_GLOBAL)
    {
        if (id >= gd.nglobals)
        {
            return v_real(0);
        }
        return array ? arr_get(globals[id], idx) : globals[id];
    }
    if (target == T_LOCAL)
    {
        if (!cur)
        {
            return v_real(0);
        }
        return array ? arr_get(cur->locals[id], idx) : cur->locals[id];
    }
    if (is_arg_var(id))
    {
        return arg_get(id, idx);
    }
    inst = read_target(target);
    if (id < V_BUILTIN_COUNT && builtin_var_get(inst, id, array ? idx : -1, &out))
    {
        return out;
    }
    if (!inst)
    {
        return v_real(0);
    }
    if (id < V_BUILTIN_COUNT)
    {
        return v_real(0);
    }
    return array ? inst_get_arr(inst, id, idx) : inst_get(inst, id);
}

static void set_one(instance_t *inst, uint16_t id, bool array, int32_t idx, value_t v)
{
    if (id < V_BUILTIN_COUNT)
    {
        if (!builtin_var_set(inst, id, array ? idx : -1, v))
        {
            vm_error("can't set built-in %d", id);
        }
        v_release(v);
        return;
    }
    if (!inst)
    {
        v_release(v);
        return;
    }
    if (array)
    {
        inst_set_arr(inst, id, idx, v);
    }
    else
    {
        inst_set(inst, id, v);
    }
}

/* takes ownership of v */
static void set_var(int32_t target, uint16_t id, bool array, int32_t idx, value_t v)
{
    if (target == T_GLOBAL)
    {
        if (id >= gd.nglobals)
        {
            v_release(v);
            return;
        }
        if (array)
        {
            arr_set(&globals[id], idx, v);
        }
        else
        {
            v_release(globals[id]);
            globals[id] = v;
        }
        return;
    }
    if (target == T_LOCAL)
    {
        if (!cur)
        {
            v_release(v);
            return;
        }
        if (array)
        {
            arr_set(&cur->locals[id], idx, v);
        }
        else
        {
            v_release(cur->locals[id]);
            cur->locals[id] = v;
        }
        return;
    }
    if (is_arg_var(id))
    {
        arg_set(id, idx, v);
        return;
    }
    if (target == T_SELF || target == T_OTHER)
    {
        set_one(target == T_SELF ? vm_self : vm_other, id, array, idx, v);
        return;
    }
    {
        /* obj.var = value sets it on every instance of obj (as
           inst_select picks them; setting a variable creates or destroys
           no instance, so no list is needed) */
        int n = 0;
        if (target >= 100000)
        {
            instance_t *in = inst_find_id(target);
            if (in)
            {
                v_retain(v);
                set_one(in, id, array, idx, v);
                n++;
            }
        }
        else if (target == -3 || target >= 0)
        {
            for (int i = 0; i < ninstances; i++)
            {
                instance_t *in = instances[i];
                if (in->alive && (target == -3 || inst_is_a(in, target)))
                {
                    v_retain(v);
                    set_one(in, id, array, idx, v);
                    n++;
                }
            }
        }
        if (id < V_BUILTIN_COUNT && n == 0)
        {
            set_one(NULL, id, array, idx, v);
            return;
        }
        v_release(v);
    }
}

static int32_t scope_target(uint8_t scope, const uint8_t **pc)
{
    switch (scope & 15)
    {
    case SC_SELF: return T_SELF;
    case SC_OTHER: return T_OTHER;
    case SC_GLOBAL: return T_GLOBAL;
    case SC_LOCAL: return T_LOCAL;
    case SC_ALL: return T_ALL;
    case SC_NOONE: return T_NOONE;
    case SC_INST:
    {
        int32_t t = rd32s(*pc);
        *pc += 4;
        return t;
    }
    }
    return T_SELF;
}

static int32_t value_target(value_t v)
{
    int32_t t = v_int(v);
    v_release(v);
    return t;
}

/* ---- arithmetic ---- */

static value_t concat(value_t a, value_t b)
{
    value_t sa = v_tostring(a), sb = v_tostring(b);
    const char *x = v_cstring(sa), *y = v_cstring(sb);
    int lx = (int)strlen(x), ly = (int)strlen(y);
    value_t r;
    char *buf = malloc(lx + ly + 1);
    if (!buf)
    {
        v_release(sa);
        v_release(sb);
        return v_cstr("");
    }
    memcpy(buf, x, lx);
    memcpy(buf + lx, y, ly);
    r = v_str(buf, lx + ly);
    free(buf);
    v_release(sa);
    v_release(sb);
    return r;
}

static value_t binop(int op, value_t a, value_t b)
{
    gmreal_t x, y;
    value_t r;

    if (op == OP_ADD && (v_is_str(a) || v_is_str(b)))
    {
        r = concat(a, b);
        v_release(a);
        v_release(b);
        return r;
    }
    x = v_num(a);
    y = v_num(b);
    v_release(a);
    v_release(b);
    switch (op)
    {
    case OP_ADD: return v_real(x + y);
    case OP_SUB: return v_real(x - y);
    case OP_MUL: return v_real(x * y);
    case OP_DIV:
        if (y == 0)
        {
            vm_error("division by zero");
            return v_real(0);
        }
        return v_real(x / y);
    case OP_REM:
        if (y == 0)
        {
            return v_real(0);
        }
        return v_real(truncf(x / y));
    case OP_MOD:
        if (y == 0)
        {
            return v_real(0);
        }
        return v_real(fmodf(x, y));
    case OP_AND: return v_real((gmreal_t)((int32_t)floorf(x + 0.5f) & (int32_t)floorf(y + 0.5f)));
    case OP_OR: return v_real((gmreal_t)((int32_t)floorf(x + 0.5f) | (int32_t)floorf(y + 0.5f)));
    case OP_XOR: return v_real((gmreal_t)((int32_t)floorf(x + 0.5f) ^ (int32_t)floorf(y + 0.5f)));
    case OP_SHL: return v_real((gmreal_t)((int32_t)floorf(x + 0.5f) << (int32_t)floorf(y + 0.5f)));
    case OP_SHR: return v_real((gmreal_t)((int32_t)floorf(x + 0.5f) >> (int32_t)floorf(y + 0.5f)));
    }
    return v_real(0);
}

static value_t compare(int op, value_t a, value_t b)
{
    int c;
    bool res;
    if (op == OP_CMP_EQ || op == OP_CMP_NE)
    {
        res = v_equal(a, b);
        if (op == OP_CMP_NE)
        {
            res = !res;
        }
    }
    else
    {
        c = v_compare(a, b);
        switch (op)
        {
        case OP_CMP_LT: res = c < 0; break;
        case OP_CMP_LE: res = c <= 0; break;
        case OP_CMP_GE: res = c >= 0; break;
        default: res = c > 0; break;
        }
    }
    v_release(a);
    v_release(b);
    return v_real(res ? 1 : 0);
}

/* ---- with() ---- */

typedef struct
{
    instance_t **list;
    int n, pos;
    instance_t *saved_self, *saved_other;
} with_t;

/* with() loops of all running calls; each call remembers where its own
   start (kept off the C stack, which is only 4 KB on the calculator) */
static with_t with_stack[MAX_WITH];
static int with_sp;

/* call arguments, also off the C stack */
#define ARG_STACK_SIZE 80 /* the game passes under 25 at once */
static value_t arg_stack[ARG_STACK_SIZE];
static int arg_sp;

static void with_end(with_t *w)
{
    vm_self = w->saved_self;
    vm_other = w->saved_other;
    free(w->list);
}

/* ---- the interpreter loop ----

   The running code's state lives in globals (pc, base, blk, code): on the
   eZ80 a big function's locals end up far from IX and every access costs
   several instructions, while globals are one load away. vm_call saves and
   restores them around nested calls. Opcodes with more work are separate
   small functions. */

const uint8_t *pc, *base;
uint16_t blk;
static uint16_t cur_code;
static value_t ret_val;

extern value_t native_gettext(int argc, value_t *args);

/* continue at code offset t (block and offset in it) */
static void jump(unsigned t)
{
    uint16_t b = (uint16_t)(t >> CODE_BLOCK_SHIFT);
    if (b != blk)
    {
        blk = b;
        base = code_block(cur_code, blk);
    }
    pc = base + (t & (CODE_BLOCK - 1));
}

/* after a call: our block may have been evicted from the code cache */
static void refetch(void)
{
    uint16_t off = (uint16_t)(pc - base);
    base = code_block(cur_code, blk);
    pc = base + off;
}

#ifdef CE_PROFILE
#define PSUB_START uint32_t psub_t0 = prof_now()
#define PSUB(k) prof_add(k, prof_now() - psub_t0)
#else
#define PSUB_START
#define PSUB(k)
#endif

static NOINLINE void op_push_var(void)
{
    uint8_t scope = pc[0];
    uint16_t id = rd16(pc + 1);
    int32_t target;
    value_t v;
    PSUB_START;
    pc += 3;
    if (scope == SC_SELF && id >= V_BUILTIN_COUNT && vm_self)
    {
        /* the common case: one of the running instance's own variables */
        value_t *slot = inst_var(vm_self, id, false);
        v = slot ? *slot : v_real(0);
        v_retain(v);
        push(v);
        PSUB(245);
        return;
    }
    if (scope == SC_GLOBAL)
    {
        v = id < gd.nglobals ? globals[id] : v_real(0);
        v_retain(v);
        push(v);
        PSUB(246);
        return;
    }
    if (scope & SC_ARRAY)
    {
        int32_t idx;
        if ((scope & 15) == SC_INST)
        {
            pc += 4;
        }
        idx = v_int(pop());
        target = value_target(pop());
        v = get_var(target, id, true, idx);
    }
    else if ((scope & 15) == SC_STACKTOP)
    {
        target = value_target(pop());
        v = get_var(target, id, false, 0);
    }
    else
    {
        target = scope_target(scope, &pc);
        v = get_var(target, id, false, 0);
    }
    v_retain(v);
    push(v);
    PSUB(scope & SC_ARRAY ? 247 : scope == SC_SELF ? 248 : 249);
}

static NOINLINE void op_pop_var(void)
{
    uint8_t scope = pc[0];
    uint16_t id = rd16(pc + 1);
    int32_t target, idx;
    value_t v;
    PSUB_START;
    pc += 3;
    if (scope & SC_ARRAY)
    {
        if ((scope & 15) == SC_INST)
        {
            pc += 4;
        }
        if (scope & SC_SWAP)
        {
            v = pop();
            idx = v_int(pop());
            target = value_target(pop());
        }
        else
        {
            idx = v_int(pop());
            target = value_target(pop());
            v = pop();
        }
        set_var(target, id, true, idx, v);
    }
    else if ((scope & 15) == SC_STACKTOP)
    {
        target = value_target(pop());
        v = pop();
        set_var(target, id, false, 0, v);
    }
    else
    {
        target = scope_target(scope, &pc);
        v = pop();
        set_var(target, id, false, 0, v);
    }
    PSUB(scope & SC_ARRAY ? 250 : scope == SC_SELF ? (id >= V_BUILTIN_COUNT ? 251 : 252) : scope == SC_GLOBAL ? 253 : 254);
}

static NOINLINE void op_dup(void)
{
    int n = *pc++ + 1;
    if (vsp - stack < n)
    {
        vm_error("dup underflow");
        return;
    }
    for (int i = 0; i < n; i++)
    {
        value_t v = vsp[-n];
        v_retain(v);
        push(v);
    }
}

static NOINLINE void op_binop(uint8_t op)
{
    value_t b = pop(), a = pop();
    push(binop(op, a, b));
}

static NOINLINE void op_compare(uint8_t op)
{
    value_t b = pop(), a = pop();
    push(compare(op, a, b));
}

static NOINLINE void op_pushenv(int with_base)
{
    int32_t target = value_target(pop());
    with_t *w;
    (void)with_base;
    if (with_sp >= MAX_WITH)
    {
        vm_error("with nesting");
        jump(rd24(pc));
        return;
    }
    w = &with_stack[with_sp++];
    HW(vm_hw_with, with_sp);
    w->saved_self = vm_self;
    w->saved_other = vm_other;
    w->pos = 0;
    w->n = 0;
    w->list = NULL;
    if (target == T_SELF || target == T_OTHER)
    {
        instance_t *one = target == T_SELF ? vm_self : vm_other;
        w->list = malloc(sizeof(instance_t *));
        if (one && w->list)
        {
            w->list[0] = one;
            w->n = 1;
        }
    }
    else
    {
        int cap = ninstances > 0 ? ninstances : 1;
        w->list = malloc(sizeof(instance_t *) * cap);
        if (w->list)
        {
            w->n = inst_select(target, w->list, cap);
        }
    }
    if (w->n == 0)
    {
        /* jump to the popenv, which finds nothing more to do */
        jump(rd24(pc));
        return;
    }
    vm_other = w->saved_self;
    vm_self = w->list[0];
    pc += 3;
}

static NOINLINE void op_popenv(int with_base)
{
    with_t *w;
    if (with_sp == with_base)
    {
        pc += 3;
        return;
    }
    w = &with_stack[with_sp - 1];
    w->pos++;
    while (w->pos < w->n && !w->list[w->pos]->alive)
    {
        w->pos++;
    }
    if (w->pos < w->n)
    {
        vm_self = w->list[w->pos];
        jump(rd24(pc));
    }
    else
    {
        with_end(w);
        with_sp--;
        pc += 3;
    }
}

static NOINLINE void op_call(uint8_t op)
{
#ifdef CE_PROFILE
    uint32_t t0 = timer_2_Counter;
    uint16_t fid = rd16(pc);
#endif
    uint16_t f = rd16(pc);
    int argc = pc[2];
    value_t *args_buf;
    value_t r;
    pc += 3;
    if (arg_sp + argc > ARG_STACK_SIZE)
    {
        vm_error("argument stack full");
        argc = 0;
    }
    args_buf = &arg_stack[arg_sp];
    arg_sp += argc;
    HW(vm_hw_args, arg_sp);
    for (int i = 0; i < argc; i++)
    {
        args_buf[i] = pop();
    }
    if (op == OP_CALL)
    {
        r = builtin_call(f, argc, args_buf);
    }
    else
    {
        r = vm_call(f, vm_self, vm_other, argc, args_buf);
    }
    refetch();
    for (int i = 0; i < argc; i++)
    {
        v_release(args_buf[i]);
    }
    arg_sp -= argc;
    push(r);
#ifdef CE_PROFILE
    if (op == OP_CALL && fid < 256)
    {
        vm_prof.fcycles[fid] += timer_2_Counter - t0;
        vm_prof.fcount[fid]++;
    }
#endif
}

static NOINLINE void op_gettext(void)
{
    /* scr_gettext(<text id>, args...): the key was resolved when the pack
       was built; the extra arguments are on the stack */
    uint16_t id = rd16(pc);
    int argc = pc[2];
    value_t *args_buf;
    value_t r;
    pc += 3;
    if (argc < 1 || arg_sp + argc > ARG_STACK_SIZE)
    {
        vm_error("argument stack full");
        return;
    }
    args_buf = &arg_stack[arg_sp];
    arg_sp += argc;
    args_buf[0] = text_by_id(id);
    for (int i = 1; i < argc; i++)
    {
        args_buf[i] = pop();
    }
    r = native_gettext(argc, args_buf);
    refetch();
    for (int i = 0; i < argc; i++)
    {
        v_release(args_buf[i]);
    }
    arg_sp -= argc;
    push(r);
}

static NOINLINE void op_branch(bool when)
{
    value_t c = pop();
    bool t = v_truthy(c);
    v_release(c);
    if (t == when)
    {
        jump(rd24(pc));
    }
    else
    {
        pc += 3;
    }
}

#ifdef VM_FAST
/* ce/src/vmfast.s: runs the simple instructions, returns at the first
   one it leaves to the switch below. It knows these layouts: */
void vm_fast(void);
_Static_assert(offsetof(instance_t, var_ids) == 75, "vmfast.s INST_VAR_IDS");
_Static_assert(offsetof(instance_t, var_vals) == 78, "vmfast.s INST_VAR_VALS");
_Static_assert(offsetof(instance_t, nvars) == 82, "vmfast.s INST_NVARS");
_Static_assert(offsetof(frame_t, locals) == 8, "vmfast.s FRAME_LOCALS");
_Static_assert(offsetof(gamedata_t, nglobals) == 16, "vmfast.s GD_NGLOBALS");
_Static_assert(sizeof(value_t) == 5 && STACK_SIZE * 5 == 1280, "vmfast.s STACK_BYTES");
_Static_assert(V_BUILTIN_COUNT == 126 && OP_CMP_LT == 20 && OP_NEXTBLK == 40 && OP_CONV_INT == 38 &&
                   OP_PUSH_UNDEF == 39 && OP_BF == 30 && OP_ADD == 9 && OP_PUSH_REAL == 3,
               "vmfast.s opcodes");
_Static_assert(SC_GLOBAL == 2 && SC_LOCAL == 3 && VT_HSTR == 3, "vmfast.s constants");
#define PROF_FAST 56
#endif

/* run the current code until it returns */
static void run(int with_base)
{
    for (;;)
    {
#ifdef VM_FAST
        PROF_OP(PROF_FAST);
        vm_fast();
#endif
        uint8_t op = *pc++;
        vm_ops++;
#ifdef VM_TRACE
        trace_ops[cur_code & 8191]++;
#endif
        PROF_OP(op);
        switch (op)
        {
        case OP_NOP:
            break;
        case OP_PUSH_REAL:
            vsp->t = VT_REAL;
            memcpy(&vsp->u.r, pc, 4);
            if (++vsp >= STACK_END)
            {
                vm_error("stack overflow");
                vsp--;
            }
            pc += 4;
            break;
        case OP_PUSH_I16:
            push(v_real((gmreal_t)(int16_t)rd16(pc)));
            pc += 2;
            break;
        case OP_PUSH_I32:
            push(v_real((gmreal_t)rd32s(pc)));
            pc += 4;
            break;
        case OP_PUSH_FSTR:
            vsp->t = VT_CSTR;
            vsp->u.cs = (const char *)far_ptr(rd24(pc));
            if (++vsp >= STACK_END)
            {
                vm_error("stack overflow");
                vsp--;
            }
            pc += 3;
            break;
        case OP_PUSH_UNDEF:
            push(v_undef());
            break;
        case OP_NEXTBLK:
            blk++;
            base = code_block(cur_code, blk);
            pc = base;
            break;
        case OP_PUSH_VAR:
            op_push_var();
            break;
        case OP_POP_VAR:
            op_pop_var();
            break;
        case OP_POPZ:
            v_release(pop());
            break;
        case OP_DUP:
            op_dup();
            break;
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_REM: case OP_MOD:
        case OP_AND: case OP_OR: case OP_XOR: case OP_SHL: case OP_SHR:
            op_binop(op);
            break;
        case OP_CMP_LT: case OP_CMP_LE: case OP_CMP_EQ: case OP_CMP_NE: case OP_CMP_GE: case OP_CMP_GT:
            op_compare(op);
            break;
        case OP_NEG:
        {
            value_t a = pop();
            push(v_real(-v_num(a)));
            v_release(a);
            break;
        }
        case OP_NOT:
        {
            value_t a = pop();
            push(v_real(v_truthy(a) ? 0 : 1));
            v_release(a);
            break;
        }
        case OP_CONV_INT:
            if (vsp > stack && vsp[-1].t == VT_REAL)
            {
                vsp[-1].u.r = (gmreal_t)(int32_t)vsp[-1].u.r;
            }
            break;
        case OP_JMP:
            jump(rd24(pc));
            break;
        case OP_BT:
            op_branch(true);
            break;
        case OP_BF:
            op_branch(false);
            break;
        case OP_PUSHENV:
            op_pushenv(with_base);
            break;
        case OP_POPENV:
            op_popenv(with_base);
            break;
        case OP_POPENV_DROP:
            if (with_sp > with_base)
            {
                with_end(&with_stack[--with_sp]);
            }
            break;
        case OP_CALL:
        case OP_CALL_SCRIPT:
            op_call(op);
            break;
        case OP_CALL_GETTEXT:
            op_gettext();
            break;
        case OP_RET:
            ret_val = pop();
            return;
        case OP_EXIT:
            ret_val = v_real(0);
            return;
        default:
            vm_error("bad opcode %d", op);
            ret_val = v_real(0);
            return;
        }
    }
}

value_t vm_call(uint16_t code, instance_t *self, instance_t *other, int argc, value_t *args)
{
    uint16_t nlocals;
    int with_base;
    value_t *stack_base;
    frame_t frame, *saved_cur = cur;
    instance_t *saved_self = vm_self, *saved_other = vm_other;
    const uint8_t *saved_pc = pc, *saved_base = base;
    uint16_t saved_blk = blk, saved_code = cur_code;
    value_t result;
#ifdef CE_PROFILE
    uint8_t saved_prof = prof_prev;
#endif

    if (code_nblocks(code) == 0)
    {
        /* not in this (test) pack */
        return v_real(0);
    }
    if (native_run(code, self, other))
    {
        return v_real(0); /* vm/native.c did it */
    }
    if (depth > 48)
    {
        vm_error("call depth");
        return v_real(0);
    }
    nlocals = code_nlocals(code);
    if (vsp + nlocals >= STACK_END)
    {
        vm_error("no room for locals");
        return v_real(0);
    }
    depth++;
    trace_use(TRACE_CODE, code);
    frame.code = code;
    frame.argc = argc;
    frame.args = args;
    frame.locals = vsp;
    for (int i = 0; i < nlocals; i++)
    {
        *vsp++ = v_real(0);
    }
    stack_base = vsp;
    with_base = with_sp;
    cur = &frame;
    vm_self = self;
    vm_other = other;
    cur_code = code;
    blk = 0;
    base = code_block(code, 0);
    pc = base;
    ret_val = v_real(0);

    run(with_base);

    result = ret_val;
#ifdef CE_PROFILE
    PROF_OP(saved_prof);
#endif
    while (with_sp > with_base)
    {
        free(with_stack[--with_sp].list);
    }
    while (vsp > stack_base)
    {
        v_release(pop());
    }
    while (vsp > stack_base - nlocals)
    {
        v_release(pop());
    }
    cur = saved_cur;
    vm_self = saved_self;
    vm_other = saved_other;
    cur_code = saved_code;
    blk = saved_blk;
    base = saved_base;
    pc = saved_pc;
    depth--;
    return result;
}

value_t vm_call_script(int script, instance_t *self, instance_t *other, int argc, value_t *args)
{
    uint16_t code = script_code(script);
    if (code == 0xffff)
    {
        return v_real(0);
    }
    return vm_call(code, self, other, argc, args);
}
