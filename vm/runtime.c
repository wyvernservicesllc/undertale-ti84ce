/* Instances, events, rooms and the GameMaker: Studio 1.4 frame order. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"
#include "gen_native.h"

#define PI_F 3.14159265f

gamestate_t gs;
instance_t **instances;
int ninstances;
static int capinstances;

uint8_t key_down[256], key_pressed[256], key_released[256];

/* instances kept by persistent rooms while the player is elsewhere */
typedef struct
{
    instance_t **list;
    int n;
    bool saved;
} room_stash_t;
static room_stash_t *stash;
static bool *room_persist; /* room_persistent, changeable at run time */
static bool first_room_done;

/* ---- instances ---- */

const motion_t motion_default = { .gravity_direction = 270, .path_index = -1, .path_scale = 1,
                                  .alarm = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 } };

motion_t *inst_m(instance_t *in)
{
    if (in->m == &motion_default)
    {
        motion_t *m = malloc(sizeof *m);
        if (!m)
        {
            static motion_t scratch; /* out of memory: changes go nowhere */
            vm_error("out of memory (motion)");
            scratch = motion_default;
            return &scratch;
        }
        *m = motion_default;
        in->m = m;
    }
    return in->m;
}

static void inst_defaults(instance_t *in, int obj)
{
    const obj_rec_t *o = OBJ(obj);
    memset(in, 0, sizeof *in);
    in->obj = (int16_t)obj;
    in->alive = 1;
    in->visible = o->flags & 1;
    in->solid = (o->flags & 2) != 0;
    in->persistent = (o->flags & 4) != 0;
    in->sprite_index = o->sprite;
    in->mask_index = o->mask;
    in->depth = (gmreal_t)o->depth;
    in->image_xscale = in->image_yscale = 1;
    in->image_alpha = 1;
    in->image_speed = 1;
    in->image_blend = 0xffffff;
    in->m = (motion_t *)&motion_default;
}

static instance_t *inst_alloc(int obj, int32_t id, gmreal_t x, gmreal_t y)
{
    instance_t *in = malloc(sizeof *in);
    if (!in)
    {
        vm_error("out of memory (instance)");
        return NULL;
    }
    inst_defaults(in, obj);
    in->id = id;
    in->x = in->xstart = in->xprevious = x;
    in->y = in->ystart = in->yprevious = y;
    if (ninstances == capinstances)
    {
        int cap = capinstances ? capinstances * 2 : 64;
        instance_t **l = realloc(instances, sizeof(instance_t *) * cap);
        if (!l)
        {
            free(in);
            return NULL;
        }
        instances = l;
        capinstances = cap;
    }
    instances[ninstances++] = in;
    return in;
}

/* Purely visual particles the game makes by the hundred (a dying
   monster's dust, Asgore's background): at most PARTICLE_CAP of each at a
   time, or they fill the calculator's RAM. instance_create returns noone
   for the rest, which the game's code takes in its stride. */
#define PARTICLE_CAP 24

static bool particle_capped(int obj)
{
    static const int16_t particles[] = { PARTICLE_OBJECTS };
    int n = 0;
    for (unsigned k = 0; k < sizeof particles / sizeof particles[0]; k++)
    {
        if (particles[k] == obj)
        {
            for (int i = 0; i < ninstances; i++)
            {
                if (instances[i]->obj == obj && instances[i]->alive && ++n >= PARTICLE_CAP)
                {
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

instance_t *inst_create(int obj, gmreal_t x, gmreal_t y)
{
    instance_t *in;
    if (obj < 0 || obj >= (int)gd.nobjects)
    {
        vm_error("instance_create of bad object %d", obj);
        return NULL;
    }
    if (particle_capped(obj))
    {
        return NULL;
    }
    in = inst_alloc(obj, gs.next_id++, x, y);
    if (in)
    {
        event_run(in, NULL, EV_CREATE, 0);
    }
    return in;
}

static void inst_free(instance_t *in)
{
    if (in->m != &motion_default)
    {
        free(in->m);
    }
    for (int i = 0; i < in->nvars; i++)
    {
        v_release(in->var_vals[i]);
    }
    free(in->var_ids);
    free(in->var_vals);
    free(in);
}

void inst_destroy(instance_t *in, bool run_event)
{
    if (!in || !in->alive)
    {
        return;
    }
    if (run_event)
    {
        event_run(in, NULL, EV_DESTROY, 0);
    }
    in->alive = 0;
}

/* Drop destroyed instances from the list. */
static void inst_cleanup(void)
{
    int j = 0;
    for (int i = 0; i < ninstances; i++)
    {
        if (instances[i]->alive)
        {
            instances[j++] = instances[i];
        }
        else
        {
            inst_free(instances[i]);
        }
    }
    ninstances = j;
}

instance_t *inst_find_id(int32_t id)
{
    for (int i = 0; i < ninstances; i++)
    {
        if (instances[i]->id == id && instances[i]->alive)
        {
            return instances[i];
        }
    }
    return NULL;
}

bool inst_is_a(const instance_t *in, int obj)
{
    int o = in->obj;
    int guard = 0;
    while (o >= 0 && guard++ < 32)
    {
        if (o == obj)
        {
            return true;
        }
        o = OBJ(o)->parent;
    }
    return false;
}

int inst_select(int32_t target, instance_t **out, int max)
{
    int n = 0;
    if (target == -1)
    {
        if (vm_self && max > 0)
        {
            out[n++] = vm_self;
        }
        return n;
    }
    if (target == -2)
    {
        if (vm_other && max > 0)
        {
            out[n++] = vm_other;
        }
        return n;
    }
    if (target == -4 || target < -3)
    {
        return 0;
    }
    if (target >= 100000)
    {
        instance_t *in = inst_find_id(target);
        if (in && max > 0)
        {
            out[n++] = in;
        }
        return n;
    }
    for (int i = 0; i < ninstances && n < max; i++)
    {
        instance_t *in = instances[i];
        if (in->alive && (target == -3 || inst_is_a(in, target)))
        {
            out[n++] = in;
        }
    }
    return n;
}

/* ---- instance variables ---- */

value_t *inst_var(instance_t *in, uint16_t id, bool create)
{
    const uint16_t *ids = in->var_ids;
    unsigned lo = 0, hi = in->nvars;
    while (lo < hi)
    {
        unsigned mid = (lo + hi) >> 1;
        if (ids[mid] < id)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    if (lo < in->nvars && ids[lo] == id)
    {
        return in->var_vals + lo;
    }
    if (!create)
    {
        return NULL;
    }
    if (in->nvars == in->capvars)
    {
        uint16_t cap = in->capvars ? in->capvars + in->capvars / 2 : 8;
        uint16_t *ni = realloc(in->var_ids, sizeof(uint16_t) * cap);
        value_t *nv;
        if (!ni)
        {
            vm_error("out of memory (variables)");
            return NULL;
        }
        in->var_ids = ni;
        nv = realloc(in->var_vals, sizeof(value_t) * cap);
        if (!nv)
        {
            vm_error("out of memory (variables)");
            return NULL;
        }
        in->var_vals = nv;
        in->capvars = cap;
    }
    memmove(&in->var_ids[lo + 1], &in->var_ids[lo], sizeof(uint16_t) * (in->nvars - lo));
    memmove(&in->var_vals[lo + 1], &in->var_vals[lo], sizeof(value_t) * (in->nvars - lo));
    in->var_ids[lo] = id;
    in->var_vals[lo] = v_real(0);
    in->nvars++;
    return in->var_vals + lo;
}

value_t inst_get(instance_t *in, uint16_t id)
{
    value_t *v = inst_var(in, id, false);
    return v ? *v : v_real(0);
}

void inst_set(instance_t *in, uint16_t id, value_t v)
{
    value_t *slot = inst_var(in, id, true);
    if (!slot)
    {
        v_release(v);
        return;
    }
    v_release(*slot);
    *slot = v;
}

value_t inst_get_arr(instance_t *in, uint16_t id, int32_t idx)
{
    value_t *v = inst_var(in, id, false);
    return v ? arr_get(*v, idx) : v_real(0);
}

void inst_set_arr(instance_t *in, uint16_t id, int32_t idx, value_t v)
{
    value_t *slot = inst_var(in, id, true);
    if (!slot)
    {
        v_release(v);
        return;
    }
    arr_set(slot, idx, v);
}

/* Put a deactivated instance back in the list. */
void inst_reinsert(instance_t *in)
{
    if (ninstances == capinstances)
    {
        int cap = capinstances ? capinstances * 2 : 64;
        instance_t **l = realloc(instances, sizeof(instance_t *) * cap);
        if (!l)
        {
            return;
        }
        instances = l;
        capinstances = cap;
    }
    instances[ninstances++] = in;
}

/* ---- collision ---- */

static int mask_sprite(const instance_t *in)
{
    return in->mask_index >= 0 ? in->mask_index : in->sprite_index;
}

bool inst_bbox(instance_t *in, gmreal_t *l, gmreal_t *t, gmreal_t *r, gmreal_t *b)
{
    int s = mask_sprite(in);
    const sprite_rec_t *sp;
    gmreal_t x1, x2, y1, y2;
    if (s < 0 || s >= (int)gd.nsprites)
    {
        return false;
    }
    sp = SPR(s);
    x1 = in->x + in->image_xscale * (sp->left - sp->ox);
    x2 = in->x + in->image_xscale * (sp->right + 1 - sp->ox) - (in->image_xscale > 0 ? 1 : -1);
    y1 = in->y + in->image_yscale * (sp->top - sp->oy);
    y2 = in->y + in->image_yscale * (sp->bottom + 1 - sp->oy) - (in->image_yscale > 0 ? 1 : -1);
    if (in->image_angle != 0)
    {
        /* rotate the four corners around the origin and take their bounds */
        gmreal_t c = gm_dcos(in->image_angle), sn = -gm_dsin(in->image_angle);
        gmreal_t xs[4] = { x1, x2, x1, x2 }, ys[4] = { y1, y1, y2, y2 };
        gmreal_t minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
        for (int i = 0; i < 4; i++)
        {
            gmreal_t dx = xs[i] - in->x, dy = ys[i] - in->y;
            gmreal_t rx = in->x + dx * c - dy * sn, ry = in->y + dx * sn + dy * c;
            if (rx < minx) minx = rx;
            if (rx > maxx) maxx = rx;
            if (ry < miny) miny = ry;
            if (ry > maxy) maxy = ry;
        }
        x1 = minx; x2 = maxx; y1 = miny; y2 = maxy;
    }
    *l = x1 < x2 ? x1 : x2;
    *r = x1 < x2 ? x2 : x1;
    *t = y1 < y2 ? y1 : y2;
    *b = y1 < y2 ? y2 : y1;
    return true;
}

/* Is room pixel (px, py) set in the instance's precise mask? */
static bool mask_hit(instance_t *in, int px, int py)
{
    int s = mask_sprite(in);
    const sprite_rec_t *sp = SPR(s);
    far_t img = sprite_image(s, (int)in->image_index);
    gmreal_t lx, ly;
    trace_use(TRACE_SPRITE, s);
    int mx, my;
    if (!sp->precise || (!img && !sp->nmasks))
    {
        return true;
    }
    lx = (px + 0.5f - in->x);
    ly = (py + 0.5f - in->y);
    if (in->image_angle != 0)
    {
        gmreal_t c = gm_dcos(in->image_angle), sn = gm_dsin(in->image_angle);
        gmreal_t rx = lx * c - ly * sn, ry = lx * sn + ly * c;
        lx = rx;
        ly = ry;
    }
    mx = (int)floorf(lx / in->image_xscale + sp->ox);
    my = (int)floorf(ly / in->image_yscale + sp->oy);
    if (mx < 0 || my < 0 || mx >= sp->w || my >= sp->h)
    {
        return false;
    }
    if (sp->nmasks)
    {
        const uint8_t *m = far_ptr(rd_far(sp->masks));
        int rowb = (sp->w + 7) / 8;
        int f = (int)in->image_index % sp->nmasks;
        if (f < 0)
        {
            f += sp->nmasks;
        }
        m += (uint32_t)f * rowb * sp->h;
        return (m[my * rowb + (mx >> 3)] >> (7 - (mx & 7))) & 1;
    }
    return img_pixel(img, sp->w, mx, my);
}

static bool is_precise(instance_t *in)
{
    int s = mask_sprite(in);
    return s >= 0 && s < (int)gd.nsprites && SPR(s)->precise && (SPR(s)->nmasks || sprite_image(s, 0));
}

/* One instance's collision mask, scanned row by row: the fast way to test
   two precise masks against each other (no rotation). */
#define CM_W 512
uint8_t vm_rowbuf[2 * CM_W]; /* also pack.c img_pixel's row */

typedef struct
{
    instance_t *in;
    const sprite_rec_t *sp;
    far_t img;
    const uint8_t *mbits; /* stored masks: this frame's */
    const uint8_t *row;   /* the current row: mask bits or pixels */
    uint8_t *buf;
    int rowb;
    bool precise;
    int32_t ax, sx; /* mask x at the first column, and per column (16.16) */
} cmask_t;

static bool cmask_init(cmask_t *c, instance_t *in, int x1, int k)
{
    int s = mask_sprite(in);
    c->in = in;
    c->precise = is_precise(in);
    if (!c->precise)
    {
        return true;
    }
    if (in->image_angle != 0)
    {
        return false;
    }
    c->sp = SPR(s);
    c->img = sprite_image(s, (int)in->image_index);
    if (c->sp->nmasks)
    {
        int f = (int)in->image_index % c->sp->nmasks;
        if (f < 0)
        {
            f += c->sp->nmasks;
        }
        c->rowb = (c->sp->w + 7) / 8;
        c->mbits = far_ptr(rd_far(c->sp->masks)) + (uint32_t)f * c->rowb * c->sp->h;
    }
    else if (!c->img)
    {
        c->precise = false; /* like mask_hit: no pixels, the box counts */
        return true;
    }
    else if (c->sp->w > CM_W)
    {
        return false;
    }
    c->buf = vm_rowbuf + k * CM_W;
    c->ax = (int32_t)floorf(((x1 + 0.5f - in->x) / in->image_xscale + c->sp->ox) * 65536);
    c->sx = (int32_t)(65536 / in->image_xscale);
    return true;
}

static bool cmask_row(cmask_t *c, int y)
{
    int my = (int)floorf((y + 0.5f - c->in->y) / c->in->image_yscale + c->sp->oy);
    if (my < 0 || my >= c->sp->h)
    {
        return false;
    }
    if (c->sp->nmasks)
    {
        c->row = c->mbits + my * c->rowb;
    }
    else
    {
        img_decode_row(c->img, my, c->buf, c->sp->w);
        c->row = c->buf;
    }
    return true;
}

static inline bool cmask_hit(const cmask_t *c, int32_t acc)
{
    int mx = (int)(acc >> 16);
    if (!c->precise)
    {
        return true;
    }
    if (mx < 0 || mx >= c->sp->w)
    {
        return false;
    }
    return c->sp->nmasks ? (c->row[mx >> 3] >> (7 - (mx & 7))) & 1 : c->row[mx] != 0;
}

bool inst_collide(instance_t *a, instance_t *b)
{
    gmreal_t al, at, ar, ab, bl, bt, br, bb;
    int x1, y1, x2, y2;
    cmask_t ca, cb;
    if (!inst_bbox(a, &al, &at, &ar, &ab) || !inst_bbox(b, &bl, &bt, &br, &bb))
    {
        return false;
    }
    if (al > br || bl > ar || at > bb || bt > ab)
    {
        return false;
    }
    if (!is_precise(a) && !is_precise(b))
    {
        return true;
    }
    x1 = (int)floorf(al > bl ? al : bl);
    x2 = (int)floorf(ar < br ? ar : br);
    y1 = (int)floorf(at > bt ? at : bt);
    y2 = (int)floorf(ab < bb ? ab : bb);
    if (cmask_init(&ca, a, x1, 0) && cmask_init(&cb, b, x1, 1))
    {
        for (int y = y1; y <= y2; y++)
        {
            int32_t pa, pb;
            if ((ca.precise && !cmask_row(&ca, y)) || (cb.precise && !cmask_row(&cb, y)))
            {
                continue;
            }
            pa = ca.ax;
            pb = cb.ax;
            for (int x = x1; x <= x2; x++, pa += ca.sx, pb += cb.sx)
            {
                if (cmask_hit(&ca, pa) && cmask_hit(&cb, pb))
                {
                    return true;
                }
            }
        }
        return false;
    }
    for (int y = y1; y <= y2; y++)
    {
        for (int x = x1; x <= x2; x++)
        {
            if (mask_hit(a, x, y) && mask_hit(b, x, y))
            {
                return true;
            }
        }
    }
    return false;
}

bool inst_collide_at(instance_t *a, gmreal_t x, gmreal_t y, instance_t *b)
{
    gmreal_t ox = a->x, oy = a->y;
    bool r;
    a->x = x;
    a->y = y;
    r = inst_collide(a, b);
    a->x = ox;
    a->y = oy;
    return r;
}

bool inst_point(instance_t *in, gmreal_t px, gmreal_t py, bool precise)
{
    gmreal_t l, t, r, b;
    if (!inst_bbox(in, &l, &t, &r, &b))
    {
        return false;
    }
    if (px < l || px > r + 0.999f || py < t || py > b + 0.999f)
    {
        return false;
    }
    if (precise && is_precise(in))
    {
        return mask_hit(in, (int)floorf(px), (int)floorf(py));
    }
    return true;
}

bool inst_rect(instance_t *in, gmreal_t x1, gmreal_t y1, gmreal_t x2, gmreal_t y2, bool precise)
{
    gmreal_t l, t, r, b;
    if (x1 > x2) { gmreal_t s = x1; x1 = x2; x2 = s; }
    if (y1 > y2) { gmreal_t s = y1; y1 = y2; y2 = s; }
    if (!inst_bbox(in, &l, &t, &r, &b))
    {
        return false;
    }
    if (x1 > r || l > x2 || y1 > b || t > y2)
    {
        return false;
    }
    if (precise && is_precise(in))
    {
        int ix1 = (int)floorf(x1 > l ? x1 : l), ix2 = (int)floorf(x2 < r ? x2 : r);
        int iy1 = (int)floorf(y1 > t ? y1 : t), iy2 = (int)floorf(y2 < b ? y2 : b);
        for (int y = iy1; y <= iy2; y++)
        {
            for (int x = ix1; x <= ix2; x++)
            {
                if (mask_hit(in, x, y))
                {
                    return true;
                }
            }
        }
        return false;
    }
    return true;
}

/* ---- events ---- */

uint16_t event_find(int obj, int type, int sub, int *owner)
{
    int guard = 0;
    while (obj >= 0 && guard++ < 32)
    {
        const obj_rec_t *o = OBJ(obj);
        for (int i = 0; i < o->nevents; i++)
        {
            const event_rec_t *e = EVENT(OBJ_FIRST_EVENT(o) + i);
            if (e->type == type && e->subtype == sub)
            {
                if (owner)
                {
                    *owner = obj;
                }
                return e->code;
            }
        }
        obj = o->parent;
    }
    return 0xffff;
}

bool event_run_as(instance_t *in, instance_t *other, int obj, int type, int sub)
{
    int owner;
    uint16_t code = event_find(obj, type, sub, &owner);
    int so = vm_ev_obj, st = vm_ev_type, ss = vm_ev_sub;
    if (code == 0xffff)
    {
        return false;
    }
    vm_ev_obj = owner;
    vm_ev_type = type;
    vm_ev_sub = sub;
    v_release(vm_call(code, in, other, 0, NULL));
    vm_ev_obj = so;
    vm_ev_type = st;
    vm_ev_sub = ss;
    return true;
}

bool event_run(instance_t *in, instance_t *other, int type, int sub)
{
    if (!in || !in->alive)
    {
        return false;
    }
    return event_run_as(in, other, in->obj, type, sub);
}

/* Run an event for every live instance, in creation order. Instances
   created during the loop wait for the next frame. */
static void run_all(int type, int sub)
{
    int n = ninstances;
    for (int i = 0; i < n && i < ninstances; i++)
    {
        instance_t *in = instances[i];
        if (in->alive)
        {
            event_run(in, NULL, type, sub);
        }
    }
}

/* ---- rooms ---- */

static void load_views(int room)
{
    const room_rec_t *r = ROOM(room);
    gs.views_enabled = (r->flags & 1) != 0;
    for (int i = 0; i < MAX_VIEWS; i++)
    {
        const room_view_t *v = ROOMVIEW(room, i);
        view_t *d = &gs.views[i];
        d->enabled = v->enabled;
        d->visible = v->enabled;
        d->x = v->x;
        d->y = v->y;
        d->w = v->w ? v->w : SCREEN_W;
        d->h = v->h ? v->h : SCREEN_H;
        d->px = v->px;
        d->py = v->py;
        d->pw = v->pw;
        d->ph = v->ph;
        d->hborder = v->bx;
        d->vborder = v->by;
        d->hspeed = v->sx;
        d->vspeed = v->sy;
        d->follow = v->follow;
        d->angle = 0;
    }
    for (int i = 0; i < 8; i++)
    {
        const room_bg_t *b = ROOMBG(room, i);
        background_t *d = &gs.bgs[i];
        d->visible = b->enabled;
        d->foreground = b->fg;
        d->htiled = b->htile;
        d->vtiled = b->vtile;
        d->index = b->bg;
        d->x = b->x;
        d->y = b->y;
        d->hspeed = b->hspeed;
        d->vspeed = b->vspeed;
        d->xscale = d->yscale = 1;
        d->alpha = 1;
        d->blend = 0xffffff;
    }
}

extern void tiles_load_room(int room);

static void pack_jump_restore(int room);

static void enter_room(int room)
{
    const room_rec_t *r = ROOM(room);
    room_stash_t *st = &stash[room];
    int first_new;

    trace_use(TRACE_ROOM, room);
    pack_jump_restore(room);
    gs.room = room;
    gs.room_width = r->w;
    gs.room_height = r->h;
    gs.room_speed = r->speed;
    gs.background_color = r->color & 0xffffff;
    gs.background_showcolor = (r->flags & 2) != 0;
    load_views(room);
    tiles_load_room(room);

    first_new = ninstances;
    if (st->saved)
    {
        /* persistent room: bring its instances back */
        for (int i = 0; i < st->n; i++)
        {
            if (ninstances == capinstances)
            {
                int cap = capinstances ? capinstances * 2 : 64;
                instance_t **l = realloc(instances, sizeof(instance_t *) * cap);
                if (!l)
                {
                    break;
                }
                instances = l;
                capinstances = cap;
            }
            instances[ninstances++] = st->list[i];
        }
        free(st->list);
        st->list = NULL;
        st->n = 0;
        st->saved = false;
    }
    else
    {
        for (int i = 0; i < r->ninst; i++)
        {
            const inst_rec_t *ir = INST(r->first_inst + i);
            int32_t id = 100000 + ir->idoff;
            instance_t *in;
            if (ir->obj < 0)
            {
                continue;
            }
            /* a persistent instance that already exists is not duplicated */
            if (inst_find_id(id))
            {
                continue;
            }
            in = inst_alloc(ir->obj, id, ir->x, ir->y);
            if (!in)
            {
                continue;
            }
            in->image_xscale = ir->xscale / 256.0f;
            in->image_yscale = ir->yscale / 256.0f;
            if (id >= gs.next_id)
            {
                gs.next_id = id + 1;
            }
        }
        /* the room's own instances only: ones their create events make have
           run their own create event already */
        int last_new = ninstances;
        for (int i = first_new; i < last_new && i < ninstances; i++)
        {
            instance_t *in = instances[i];
            const inst_rec_t *rec = NULL;
            for (int k = 0; k < r->ninst; k++)
            {
                const inst_rec_t *ir = INST(r->first_inst + k);
                if (100000 + ir->idoff == in->id)
                {
                    rec = ir;
                    break;
                }
            }
            event_run(in, NULL, EV_CREATE, 0);
            if (rec && rec->creation != 0xffff && in->alive)
            {
                v_release(vm_call(rec->creation, in, NULL, 0, NULL));
            }
        }
    }
    if (!first_room_done)
    {
        first_room_done = true;
        run_all(EV_OTHER, EV_OTHER_GAME_START);
    }
    if (r->creation != 0xffff)
    {
        v_release(vm_call(r->creation, NULL, NULL, 0, NULL));
    }
    run_all(EV_OTHER, EV_OTHER_ROOM_START);
    inst_cleanup();
}

void room_goto(int room)
{
    if (room >= 0 && room < (int)gd.nrooms)
    {
        gs.pending_room = room;
    }
}

bool room_is_persistent(int room)
{
    return room_persist[room];
}

void room_set_persistent(int room, bool p)
{
    room_persist[room] = p;
}

/* "room entrance interact facing", the state a door leaves for the room
   it leads to (obj_mainchara's create event reads it) */
static int pack_jump_format(char *buf, int room)
{
    int32_t v[4];
    int n = 0;
    v[0] = room;
    v[1] = v_int(globals[GV_entrance]);
    v[2] = v_int(globals[GV_interact]);
    v[3] = v_int(globals[GV_facing]);
    for (int i = 0; i < 4; i++)
    {
        char tmp[12];
        int k = 0;
        int32_t x = v[i] < 0 ? -v[i] : v[i];
        if (v[i] < 0)
        {
            buf[n++] = '-';
        }
        do
        {
            tmp[k++] = (char)('0' + x % 10);
            x /= 10;
        } while (x);
        while (k)
        {
            buf[n++] = tmp[--k];
        }
        buf[n++] = i < 3 ? ' ' : 0;
    }
    return n - 1;
}

/* entering the room a pack change saved at: put the door state back */
static void pack_jump_restore(int room)
{
    char *data;
    int len;
    if (plat_file_read("ut_packjump", &data, &len))
    {
        long v[4] = { -1, 0, 0, 0 };
        char *p = data;
        for (int i = 0; i < 4; i++)
        {
            v[i] = strtol(p, &p, 10);
        }
        free(data);
        if (v[0] == room)
        {
            plat_file_delete("ut_packjump");
            v_release(globals[GV_entrance]);
            globals[GV_entrance] = v_real((gmreal_t)v[1]);
            v_release(globals[GV_interact]);
            globals[GV_interact] = v_real((gmreal_t)v[2]);
            v_release(globals[GV_facing]);
            globals[GV_facing] = v_real((gmreal_t)v[3]);
        }
    }
}

static void change_room(void)
{
    int next = gs.pending_room;
    int old = gs.room;
    gs.pending_room = -1;
    if (ROOM(next)->flags & ROOM_NOT_IN_PACK)
    {
        /* The next room is in another pack: save the game as if the
           player had got there (the game's own save, with that room), and
           how they came in, so Continue with the next pack goes on from
           there; the platform then asks for that pack. */
        char buf[64];
        int n;
        v_release(globals[GV_currentroom]);
        globals[GV_currentroom] = v_real((gmreal_t)next);
        if (ninstances)
        {
            v_release(vm_call(CODE_gml_Script_scr_save, instances[0], NULL, 0, NULL));
        }
        n = pack_jump_format(buf, next);
        plat_file_write("ut_packjump", buf, n);
        gs.missing_room = next;
        gs.end_game = true;
        return;
    }

    run_all(EV_OTHER, EV_OTHER_ROOM_END);
    inst_cleanup();
    if (room_persist[old])
    {
        room_stash_t *st = &stash[old];
        int j = 0;
        st->list = malloc(sizeof(instance_t *) * (ninstances ? ninstances : 1));
        st->n = 0;
        for (int i = 0; i < ninstances; i++)
        {
            if (instances[i]->persistent)
            {
                instances[j++] = instances[i];
            }
            else if (st->list)
            {
                st->list[st->n++] = instances[i];
            }
        }
        ninstances = j;
        st->saved = true;
    }
    else
    {
        for (int i = 0; i < ninstances; i++)
        {
            if (!instances[i]->persistent)
            {
                instances[i]->alive = 0;
            }
        }
        inst_cleanup();
    }
    enter_room(next);
}

extern void builtins_reset(void);

void game_start(void)
{
    for (int i = 0; i < ninstances; i++)
    {
        inst_free(instances[i]);
    }
    ninstances = 0;
    if (globals)
    {
        for (uint32_t i = 0; i < gd.nglobals; i++)
        {
            v_release(globals[i]);
        }
    }
    free(globals);
    globals = calloc(gd.nglobals ? gd.nglobals : 1, sizeof(value_t));
    /* unassigned globals are undefined, so variable_global_exists works */
    for (uint32_t i = 0; i < gd.nglobals; i++)
    {
        globals[i] = v_undef();
    }
    free(stash);
    stash = calloc(gd.nrooms, sizeof(room_stash_t));
    free(room_persist);
    room_persist = calloc(gd.nrooms, sizeof(bool));
    for (uint32_t i = 0; i < gd.nrooms; i++)
    {
        room_persist[i] = ROOM(i)->persistent;
    }
    memset(&gs, 0, sizeof gs);
    gs.pending_room = -1;
    gs.missing_room = -1;
    gs.next_id = 100000;
    gs.draw_color = 0xffffff;
    gs.draw_alpha = 1;
    gs.draw_font = -1;
    first_room_done = false;
    builtins_reset();
    enter_room(room_order(0));
}

/* ---- motion ---- */

static void sync_speed(instance_t *in)
{
    inst_m(in)->speed = sqrtf(in->m->hspeed * in->m->hspeed + in->m->vspeed * in->m->vspeed);
    if (in->m->hspeed != 0 || in->m->vspeed != 0)
    {
        gmreal_t d = gm_datan2(-in->m->vspeed, in->m->hspeed);
        if (d < 0)
        {
            d += 360;
        }
        inst_m(in)->direction = d;
    }
}

extern void path_step(instance_t *in);

static void motion(instance_t *in)
{
    if (in->m == &motion_default)
    {
        return; /* not moving */
    }
    if (in->m->friction != 0 && in->m->speed != 0)
    {
        gmreal_t s = in->m->speed;
        s = s > 0 ? s - in->m->friction : s + in->m->friction;
        if ((in->m->speed > 0 && s < 0) || (in->m->speed < 0 && s > 0))
        {
            s = 0;
        }
        inst_m(in)->hspeed = s * gm_dcos(in->m->direction);
        inst_m(in)->vspeed = -s * gm_dsin(in->m->direction);
        inst_m(in)->speed = s;
    }
    if (in->m->gravity != 0)
    {
        inst_m(in)->hspeed += in->m->gravity * gm_dcos(in->m->gravity_direction);
        inst_m(in)->vspeed -= in->m->gravity * gm_dsin(in->m->gravity_direction);
        sync_speed(in);
    }
    if (in->m->path_index >= 0)
    {
        path_step(in);
    }
    in->x += in->m->hspeed;
    in->y += in->m->vspeed;
}

/* ---- the frame ---- */

extern void draw_frame(void);

static void collisions(void)
{
    int n = ninstances;
    for (int i = 0; i < n && i < ninstances; i++)
    {
        instance_t *in = instances[i];
        int obj = in->obj;
        int guard = 0;
        if (!in->alive)
        {
            continue;
        }
        /* walk this object's collision events, own and inherited */
        while (obj >= 0 && guard++ < 32)
        {
            const obj_rec_t *o = OBJ(obj);
            for (int k = 0; k < o->nevents; k++)
            {
                const event_rec_t *e = EVENT(OBJ_FIRST_EVENT(o) + k);
                int target;
                uint16_t ecode;
                if (e->type != EV_COLLISION)
                {
                    continue;
                }
                target = e->subtype;
                ecode = e->code;
                /* a child's own event for the same target overrides */
                if (obj != in->obj && event_find(in->obj, EV_COLLISION, target, NULL) != ecode)
                {
                    continue;
                }
                for (int j = 0; j < n && j < ninstances && in->alive; j++)
                {
                    instance_t *ot = instances[j];
                    if (ot == in || !ot->alive || !inst_is_a(ot, target))
                    {
                        continue;
                    }
#ifdef CE_PROFILE
                    uint32_t c0 = prof_now();
                    bool hit = inst_collide(in, ot);
                    prof_add(266, prof_now() - c0);
                    if (hit)
#else
                    if (inst_collide(in, ot))
#endif
                    {
                        if (ot->solid || in->solid)
                        {
                            in->x = in->xprevious;
                            in->y = in->yprevious;
                        }
                        {
                            int so = vm_ev_obj, st = vm_ev_type, ss = vm_ev_sub;
                            vm_ev_obj = obj;
                            vm_ev_type = EV_COLLISION;
                            vm_ev_sub = target;
#ifdef CE_PROFILE
                            c0 = prof_now();
#endif
                            v_release(vm_call(ecode, in, ot, 0, NULL));
#ifdef CE_PROFILE
                            prof_add(270, prof_now() - c0);
#endif
                            vm_ev_obj = so;
                            vm_ev_type = st;
                            vm_ev_sub = ss;
                        }
                    }
                }
            }
            obj = o->parent;
        }
    }
}

static int sprite_frames(int s)
{
    return s >= 0 && s < (int)gd.nsprites ? SPR(s)->frames : 0;
}
#ifdef CE_PROFILE
/* time since the last mark into a profile slot (tools/ceprof.py) */
#define PSUB_START uint32_t psub_t0 = prof_now()
#define PSUB_END(k) (prof_add(k, prof_now() - psub_t0), psub_t0 = prof_now())
#else
#define PSUB_START
#define PSUB_END(k)
#endif


void game_frame(void)
{
    int n;

    gs.frame++;
    n = ninstances;
    for (int i = 0; i < n; i++)
    {
        instance_t *in = instances[i];
        in->xprevious = in->x;
        in->yprevious = in->y;
        if (in->m != &motion_default)
        {
            in->m->path_positionprevious = in->m->path_position;
        }
    }

    prof_phase(PROF_BEGIN_STEP);
    run_all(EV_STEP, EV_STEP_BEGIN);
    prof_phase(PROF_ALARMS);

    n = ninstances;
    for (int i = 0; i < n && i < ninstances; i++)
    {
        instance_t *in = instances[i];
        for (int a = 0; a < MAX_ALARMS && in->alive && in->m != &motion_default; a++)
        {
            if (in->m->alarm[a] > 0 && --in->m->alarm[a] == 0)
            {
                in->m->alarm[a] = -1;
                event_run(in, NULL, EV_ALARM, a);
            }
        }
    }

    /* keyboard events */
    prof_phase(PROF_KEYS);
    {
        bool any = false;
        for (int k = 2; k < 256; k++)
        {
            if (key_down[k])
            {
                any = true;
                run_all(EV_KEYBOARD, k);
            }
        }
        run_all(EV_KEYBOARD, any ? VK_ANYKEY : VK_NOKEY);
        for (int k = 2; k < 256; k++)
        {
            if (key_pressed[k])
            {
                run_all(EV_KEYPRESS, k);
                run_all(EV_KEYPRESS, VK_ANYKEY);
            }
            if (key_released[k])
            {
                run_all(EV_KEYRELEASE, k);
            }
        }
    }

    prof_phase(PROF_STEP);
    run_all(EV_STEP, EV_STEP_NORMAL);
    prof_phase(PROF_MOTION);

    PSUB_START;
    n = ninstances;
    for (int i = 0; i < n && i < ninstances; i++)
    {
        if (instances[i]->alive)
        {
            motion(instances[i]);
        }
    }
    PSUB_END(267);

    collisions();
    PSUB_END(268);

    /* outside room / intersect boundary events, when that changes */
    n = ninstances;
    for (int i = 0; i < n && i < ninstances; i++)
    {
        instance_t *in = instances[i];
        gmreal_t l, t, r, b;
        bool out, bound;
        if (!in->alive)
        {
            continue;
        }
        if (!inst_bbox(in, &l, &t, &r, &b))
        {
            l = r = in->x;
            t = b = in->y;
        }
        out = r < 0 || l >= gs.room_width || b < 0 || t >= gs.room_height;
        bound = !out && (l < 0 || r >= gs.room_width || t < 0 || b >= gs.room_height);
        if (out && !in->outside)
        {
            in->outside = 1;
            event_run(in, NULL, EV_OTHER, EV_OTHER_OUTSIDE);
        }
        else if (!out)
        {
            in->outside = 0;
        }
        if (bound && !in->at_boundary)
        {
            in->at_boundary = 1;
            event_run(in, NULL, EV_OTHER, 1);
        }
        else if (!bound)
        {
            in->at_boundary = 0;
        }
    }

    prof_phase(PROF_END_STEP);
    run_all(EV_STEP, EV_STEP_END);

    /* animation */
    n = ninstances;
    for (int i = 0; i < n && i < ninstances; i++)
    {
        instance_t *in = instances[i];
        int frames;
        if (!in->alive)
        {
            continue;
        }
        frames = sprite_frames(in->sprite_index);
        in->image_index += in->image_speed;
        if (frames > 0 && in->image_index >= frames)
        {
            in->image_index -= frames;
            event_run(in, NULL, EV_OTHER, EV_OTHER_ANIMATION_END);
        }
        else if (frames > 0 && in->image_index < 0)
        {
            in->image_index += frames;
            event_run(in, NULL, EV_OTHER, EV_OTHER_ANIMATION_END);
        }
    }

    inst_cleanup();
#ifdef CE_DEBUG
    {
        extern uint32_t prof_logic_end;
        prof_logic_end = plat_time_ms();
    }
#endif
    prof_phase(PROF_DRAW);
    draw_frame();
    prof_phase(47);
    inst_cleanup();

    if (gs.restart_game)
    {
        gs.restart_game = false;
        game_start();
        return;
    }
    if (gs.pending_room >= 0)
    {
        change_room();
    }
}

void input_update(const uint8_t *now)
{
    for (int k = 0; k < 256; k++)
    {
        key_pressed[k] = now[k] && !key_down[k];
        key_released[k] = !now[k] && key_down[k];
        key_down[k] = now[k];
    }
}

/* ---- a hash of the game's state, to compare runs (host -H, the
   calculator's `make TEST=`) ---- */

static uint32_t sh_hash;

static void sh_bytes(const void *p, size_t n)
{
    const uint8_t *b = p;
    while (n--)
    {
        sh_hash = (sh_hash ^ *b++) * 16777619u;
    }
}

static void sh_value(value_t v)
{
    sh_bytes(&v.t, 1);
    if (v.t == VT_REAL)
    {
        sh_bytes(&v.u.r, 4);
    }
    else if (v_is_str(v))
    {
        const char *s = v_cstring(v);
        sh_bytes(s, strlen(s));
    }
    else if (v.t == VT_ARRAY)
    {
        for (int i = 0; i < v.u.a->n; i++)
        {
            int32_t idx = v.u.a->e[i].idx;
            sh_bytes(&idx, 4);
            sh_value(v.u.a->e[i].v);
        }
    }
}

/* part -1: the room and globals; part i: instance i */
uint32_t vm_part_hash(int part)
{
    sh_hash = 2166136261u;
    if (part < 0)
    {
        int32_t room = gs.room;
        sh_bytes(&room, 4);
        for (uint32_t i = 0; i < gd.nglobals; i++)
        {
            sh_value(globals[i]);
        }
    }
    else
    {
        instance_t *in = instances[part];
        int32_t obj = in->obj;
        sh_bytes(&in->id, 4);
        sh_bytes(&obj, 4);
        sh_bytes(&in->x, 4);
        sh_bytes(&in->y, 4);
        sh_bytes(&in->image_index, 4);
        for (int k = 0; k < MAX_ALARMS; k++)
        {
            int32_t al = in->m->alarm[k];
            sh_bytes(&al, 4);
        }
        for (int k = 0; k < in->nvars; k++)
        {
            int32_t id = in->var_ids[k];
            sh_bytes(&id, 4);
            sh_value(in->var_vals[k]);
        }
    }
    return sh_hash;
}

uint32_t vm_state_hash(void)
{
    uint32_t h = vm_part_hash(-1);
    for (int i = 0; i < ninstances; i++)
    {
        h = (h ^ vm_part_hash(i)) * 16777619u;
    }
    return h;
}

/* instance `id`'s state as (key, type, bits) triples, to find what two
   runs disagree on: keys 0-2 x, y, image_index, 10+ alarms, 1000+ vars */
int vm_inst_dump(int32_t id, uint32_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < ninstances; i++)
    {
        instance_t *in = instances[i];
        if (in->id != id)
        {
            continue;
        }
#define DUMP(k, t, bits) \
    if (n + 3 <= max) { out[n] = (k); out[n + 1] = (t); out[n + 2] = (bits); n += 3; }
        {
            union { gmreal_t f; uint32_t u; } c;
            c.f = in->x; DUMP(0, 1, c.u);
            c.f = in->y; DUMP(1, 1, c.u);
            c.f = in->image_index; DUMP(2, 1, c.u);
            for (int k = 0; k < MAX_ALARMS; k++)
            {
                DUMP(10 + k, 0, (uint32_t)in->m->alarm[k]);
            }
            for (int k = 0; k < in->nvars; k++)
            {
                value_t v = in->var_vals[k];
                uint32_t bits = 0;
                if (v.t == VT_REAL)
                {
                    c.f = v.u.r;
                    bits = c.u;
                }
                else if (v_is_str(v))
                {
                    sh_hash = 2166136261u;
                    sh_value(v);
                    bits = sh_hash;
                }
                DUMP(1000 + in->var_ids[k], v.t, bits);
            }
        }
#undef DUMP
    }
    return n;
}

#ifndef __TICE__
/* What the heap holds (host: MEM=1 with HASH=frames), to fit the
   calculator's small RAM. Counts payload bytes as the eZ80 would. */
static long mem_value(value_t v, int depth)
{
    long n = 0;
    if (v.t == VT_HSTR)
    {
        n = 3 + 4 + v.u.hs->len + 1;
    }
    else if (v.t == VT_ARRAY)
    {
        n = 3 + 9 + (long)v.u.a->cap * 9;
        for (int i = 0; i < v.u.a->n && depth < 4; i++)
        {
            n += mem_value(v.u.a->e[i].v, depth + 1);
        }
    }
    return n;
}

void vm_mem_report(void)
{
    long tot = 0, g = (long)gd.nglobals * 5, garr = 0;
    for (uint32_t i = 0; i < gd.nglobals; i++)
    {
        long m = mem_value(globals[i], 0);
        garr += m;
        if (m > 200)
        {
            fprintf(stderr, "  global %u: %ld bytes (%s n=%d cap=%d)\n", i, m, globals[i].t == VT_ARRAY ? "array" : "str",
                    globals[i].t == VT_ARRAY ? globals[i].u.a->n : 0, globals[i].t == VT_ARRAY ? globals[i].u.a->cap : 0);
        }
    }
    fprintf(stderr, "mem: globals %ld + contents %ld\n", g, garr);
    tot += g + garr;
    for (int i = 0; i < ninstances; i++)
    {
        instance_t *in = instances[i];
        /* eZ80: 84-byte instance + malloc header + list slot, its own
           motion_t (62 + 3) if it moves, the variable arrays */
        long m = 90 + (in->m != &motion_default ? 89 : 0) + (in->capvars ? (long)in->capvars * 7 + 6 : 0), c = 0;
        for (int k = 0; k < in->nvars; k++)
        {
            long vm = mem_value(in->var_vals[k], 0);
            c += vm;
            if (vm > 200)
            {
                value_t v = in->var_vals[k];
                fprintf(stderr, "    var %d: %ld (n=%d cap=%d first=%d last=%d)\n", in->var_ids[k], vm, v.t == VT_ARRAY ? v.u.a->n : 0,
                        v.t == VT_ARRAY ? v.u.a->cap : 0, v.t == VT_ARRAY && v.u.a->n ? v.u.a->e[0].idx : 0, v.t == VT_ARRAY && v.u.a->n ? v.u.a->e[v.u.a->n - 1].idx : 0);
            }
        }
        fprintf(stderr, "  inst %d obj %d: %ld + %ld (nvars %d cap %d)\n", in->id, in->obj, m, c, in->nvars, in->capvars);
        tot += m + c;
    }
    fprintf(stderr, "mem total ~%ld\n", tot);
}
#endif

/* Test hooks (host WARP/BATTLE, calculator UTIN commands): start a
   battle group the way the game's encounters do */
void vm_test_battle(int group)
{
    int g = data_find_global("battlegroup"), m = data_find_global("mercy");
    if (g >= 0)
    {
        v_release(globals[g]);
        globals[g] = v_real((gmreal_t)group);
    }
    if (m >= 0)
    {
        v_release(globals[m]);
        globals[m] = v_real(0);
    }
    inst_create(OBJ_obj_battler, 0, 0);
}
