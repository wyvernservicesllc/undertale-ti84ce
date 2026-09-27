/* Reads the data pack in place (layout: tools/cepack.py). Code and text
   are compressed in blocks and decompressed into small RAM caches. */
#include <stdlib.h>
#include <string.h>

#include "vm.h"

#ifdef __TICE__
#include <compression.h>
#define zx0_decompress(dst, src) zx0_Decompress(dst, src)
#else
void zx0_decompress_c(uint8_t *out, const uint8_t *in);
#define zx0_decompress(dst, src) zx0_decompress_c(dst, src)
#endif

gamedata_t gd;
const uint8_t *pack_win[PACK_MAX_WINDOWS];
static uint16_t cache_block[CODE_CACHE_BLOCKS]; /* global block number per slot */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* A blob: u8 method, then zx0 data (1) or u16 length + raw bytes (0). */
static void unblob(uint8_t *dst, const uint8_t *src)
{
    if (src[0] == 1)
    {
        zx0_decompress(dst, src + 1);
    }
    else
    {
        memcpy(dst, src + 3, rd16(src + 1));
    }
}

bool pack_init(void)
{
    memset(cache_block, 0xff, sizeof cache_block);
    const uint8_t *h = pack_win[0];
    uint32_t f[64];
    int i = 0;
    if (!h || memcmp(h, "UTP1", 4))
    {
        return false;
    }
    for (int k = 0; k < 64; k++)
    {
        f[k] = rd32(h + 4 + k * 4);
    }
    gd.nwindows = f[i++];
    gd.ncode = f[i++];
    gd.code_entries = f[i++];
    gd.nblocks = f[i++];
    gd.code_blocks = f[i++];
    gd.nscripts = f[i++];
    gd.scripts = f[i++];
    gd.nglobals = f[i++];
    gd.global_names = f[i++];
    gd.ninstvars = f[i++];
    gd.ntext = f[i++];
    gd.text_index = f[i++];
    gd.ntextblocks = f[i++];
    gd.text_blocks = f[i++];
    gd.ntexthashes = f[i++];
    gd.text_hashes = f[i++];
    gd.text_ids = f[i++];
    gd.nobjects = f[i++];
    gd.objects = f[i++];
    gd.events = f[i++];
    gd.nsprites = f[i++];
    gd.sprites = f[i++];
    gd.frames = f[i++];
    gd.nbgs = f[i++];
    gd.bgs = f[i++];
    gd.nfonts = f[i++];
    gd.fonts = f[i++];
    gd.glyphs = f[i++];
    gd.npaths = f[i++];
    gd.paths = f[i++];
    gd.points = f[i++];
    gd.nrooms = f[i++];
    gd.rooms = f[i++];
    gd.room_bgs = f[i++];
    gd.room_views = f[i++];
    gd.insts = f[i++];
    gd.tiles = f[i++];
    gd.tile_depths = f[i++];
    gd.nroomorder = f[i++];
    gd.roomorder = f[i++];
    gd.palette = f[i++];
    for (uint32_t w = 0; w < gd.nwindows; w++)
    {
        if (!pack_win[w])
        {
            return false;
        }
    }
    return true;
}

far_t far_elem_big(far_t base, far_t l)
{
    return base + ((l >> 15) << 16) + (l & 0x7fff);
}

const char *far_name(const uint8_t *name3)
{
    far_t a = rd_far(name3);
    return a ? (const char *)far_ptr(a) : "";
}

int32_t tile_depth(const tile_rec_t *t)
{
    return (int32_t)rd32(far_ptr(far_elem(gd.tile_depths, (far_t)t->depth * 4)));
}

int room_order(int i)
{
    return rd16(far_ptr(far_elem(gd.roomorder, (far_t)i * 2)));
}

uint16_t script_code(int script)
{
    if (script < 0 || script >= (int)gd.nscripts)
    {
        return 0xffff;
    }
    return rd16(far_ptr(far_elem(gd.scripts, (far_t)script * 2)));
}

/* ---- code block cache ---- */


/* CODE_CACHE_BLOCKS * CODE_BLOCK bytes, provided by the platform before
   pack_init (on the calculator it lives outside the small C heap) */
uint8_t *pack_cache_mem;
int pack_cache_blocks = CODE_CACHE_BLOCKS; /* how many fit in pack_cache_mem */
#define code_cache(i) (pack_cache_mem + (uint32_t)(i) * CODE_BLOCK)
uint32_t code_misses; /* for profiling */
static uint32_t cache_used[CODE_CACHE_BLOCKS];
static uint32_t cache_clock;

uint16_t code_nlocals(uint16_t entry)
{
    return rd16(far_ptr(far_elem(gd.code_entries, (far_t)entry * 8)) + 4);
}

uint16_t code_nblocks(uint16_t entry)
{
    return rd16(far_ptr(far_elem(gd.code_entries, (far_t)entry * 8)) + 2);
}

/* first block of each code entry, looked up once per entry */
static uint16_t last_entry = 0xffff, last_first;
static uint16_t raw_global = 0xffff; /* the last uncompressed block */
static const uint8_t *raw_ptr;

const uint8_t *code_block(uint16_t entry, uint16_t block)
{
    uint16_t global;
    int slot = 0;
    uint32_t oldest = 0xffffffff;
    if (entry != last_entry)
    {
        last_first = rd16(far_ptr(far_elem(gd.code_entries, (far_t)entry * 8)));
        last_entry = entry;
    }
    global = last_first + block;
    if (global == raw_global)
    {
        return raw_ptr;
    }
    cache_clock++;
    {
        /* its usual slot first */
        int h = global % pack_cache_blocks;
        if (cache_block[h] == global)
        {
            cache_used[h] = cache_clock;
            return code_cache(h);
        }
    }
    for (int i = 0; i < pack_cache_blocks; i++)
    {
        if (cache_block[i] == global)
        {
            cache_used[i] = cache_clock;
            return code_cache(i);
        }
        if (cache_used[i] < oldest)
        {
            oldest = cache_used[i];
            slot = i;
        }
    }
    /* prefer its usual slot when that one is old enough */
    {
        int h = global % pack_cache_blocks;
        if (cache_used[h] + (uint32_t)pack_cache_blocks < cache_clock)
        {
            slot = h;
        }
    }
    {
        /* uncompressed code (the hottest) runs straight from the pack */
        const uint8_t *src = far_ptr(rd_far(far_ptr(far_elem(gd.code_blocks, (far_t)global * 4))));
        if (src[0] == 0)
        {
            raw_global = global;
            raw_ptr = src + 3;
            return raw_ptr;
        }
    }
    code_misses++;
#ifdef CE_PROFILE
    uint32_t t0 = prof_now();
#endif
    unblob(code_cache(slot), far_ptr(rd_far(far_ptr(far_elem(gd.code_blocks, (far_t)global * 4)))));
#ifdef CE_PROFILE
    prof_add(255, prof_now() - t0);
#endif
    cache_block[slot] = global;
    cache_used[slot] = cache_clock;
    return code_cache(slot);
}

const char *code_name(uint16_t entry)
{
    (void)entry;
    return "code";
}

/* ---- text ---- */

#define TEXT_BLOCK 2048
/* decompressed text blocks: one on the calculator, where RAM is dear
   (text_by_id copies the string out) */
#ifdef __TICE__
#define TEXT_SLOTS 1
#else
#define TEXT_SLOTS 2
#endif
static uint8_t text_cache[TEXT_SLOTS][TEXT_BLOCK];
static uint16_t text_cached[TEXT_SLOTS]; /* block + 1, 0: empty */
static uint8_t text_next;

value_t text_by_id(uint16_t id)
{
    const uint8_t *e;
    uint16_t block, off;
    int slot;
    if (id >= gd.ntext)
    {
        return v_undef();
    }
    e = far_ptr(far_elem(gd.text_index, (far_t)id * 4));
    block = rd16(e);
    off = rd16(e + 2);
    for (slot = 0; slot < TEXT_SLOTS && text_cached[slot] != block + 1; slot++)
    {
    }
    if (slot == TEXT_SLOTS)
    {
        slot = text_next;
        text_next = (uint8_t)((text_next + 1) % TEXT_SLOTS);
        unblob(text_cache[slot], far_ptr(rd_far(far_ptr(far_elem(gd.text_blocks, (far_t)block * 4)))));
        text_cached[slot] = (uint16_t)(block + 1);
    }
    return v_str((const char *)text_cache[slot] + off, -1);
}

static uint32_t fnv(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++)
    {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    return h;
}

int text_find(const char *key)
{
    uint32_t h = fnv(key);
    int lo = 0, hi = (int)gd.ntexthashes;
    while (lo < hi)
    {
        int mid = (lo + hi) / 2;
        uint32_t m = rd32(far_ptr(far_elem(gd.text_hashes, (far_t)mid * 4)));
        if (m < h)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    if (lo < (int)gd.ntexthashes && rd32(far_ptr(far_elem(gd.text_hashes, (far_t)lo * 4))) == h)
    {
        return rd16(far_ptr(far_elem(gd.text_ids, (far_t)lo * 2)));
    }
    return -1;
}

/* ---- images ---- */

far_t sprite_image(int sprite, int frame)
{
    const sprite_rec_t *s;
    if (sprite < 0 || sprite >= (int)gd.nsprites)
    {
        return 0;
    }
    s = SPR(sprite);
    if (!s->frames)
    {
        return 0;
    }
    frame %= s->frames;
    if (frame < 0)
    {
        frame += s->frames;
    }
    return rd_far(far_ptr(far_elem(gd.frames, (far_t)(s->first_frame + frame) * 4)));
}

far_t bg_image(int bg)
{
    if (bg < 0 || bg >= (int)gd.nbgs)
    {
        return 0;
    }
    return rd_far(BG(bg)->img);
}

/* Find row y: pointer to its RLE data and the end. */
const uint8_t *img_row(far_t img, int y, const uint8_t **end)
{
    const uint8_t *hdr = far_ptr(img);
    int nbands = hdr[0];
    int b = 0;
    const uint8_t *band;
    int first;
    while (b + 1 < nbands && rd16(hdr + 1 + (b + 1) * 5) <= y)
    {
        b++;
    }
    first = rd16(hdr + 1 + b * 5);
    band = far_ptr(rd_far(hdr + 3 + b * 5));
    *end = band + rd16(band + (y - first + 1) * 2);
    return band + rd16(band + (y - first) * 2);
}

void img_decode_row(far_t img, int y, uint8_t *out, int w)
{
    const uint8_t *end, *p = img_row(img, y, &end);
    int x = 0;
    while (p < end && x < w)
    {
        uint8_t t = *p++;
        int n;
        if (t < 0x40)
        {
            n = t + 1;
            while (n-- && x < w)
            {
                out[x++] = 0;
            }
        }
        else if (t < 0x80)
        {
            uint8_t c = *p++;
            n = (t & 0x3f) + 1;
            while (n-- && x < w)
            {
                out[x++] = c;
            }
        }
        else
        {
            n = (t & 0x7f) + 1;
            while (n-- && x < w)
            {
                out[x++] = *p++;
            }
        }
    }
    while (x < w)
    {
        out[x++] = 0;
    }
}

bool img_pixel(far_t img, int w, int x, int y)
{
    static far_t cached_img;
    static int cached_y = -1;
    uint8_t *row = vm_rowbuf; /* shared with runtime.c's mask rows: never both at once */
    if (!img || x < 0 || x >= w || w > 1024)
    {
        return false;
    }
    if (img != cached_img || y != cached_y)
    {
        img_decode_row(img, y, row, w);
        cached_img = img;
        cached_y = y;
    }
    return row[x] != 0;
}

/* ---- names ---- */

int data_find_object(const char *name)
{
    (void)name; /* packs carry no object names */
    return -1;
}

int data_find_global(const char *name)
{
    for (uint32_t i = 0; i < gd.nglobals; i++)
    {
        if (!strcmp((const char *)far_ptr(rd_far(far_ptr(far_elem(gd.global_names, (far_t)i * 4)))), name))
        {
            return (int)i;
        }
    }
    return -1;
}
