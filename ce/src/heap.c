/* malloc/free/realloc over several regions: the toolchain's heap area
 * (~32 KB, between the BSS and the OS) and more RAM from the UTRAM AppVar
 * (main.c, heap_add). GameMaker's arrays, strings and instances need more
 * than the first one alone.
 *
 * K&R style: free blocks in an address-ordered list, first fit, merged
 * with their neighbours when freed. Allocated blocks keep only their size
 * (3 bytes) in front of the data. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "heap.h"

typedef struct blk
{
    size_t size;      /* of the whole block, header included */
    struct blk *next; /* free blocks only */
} blk_t;

#define HDR sizeof(size_t)
#define MIN_BLOCK sizeof(blk_t)

static blk_t *free_list;
size_t heap_used, heap_peak, heap_total;

extern uint8_t __heap_low[], __heap_high[];

/* frees a block into the list, merging it with adjacent free blocks */
static void release(blk_t *b)
{
    blk_t *prev = NULL, *n = free_list;
    while (n && n < b)
    {
        prev = n;
        n = n->next;
    }
    if (n && (uint8_t *)b + b->size == (uint8_t *)n)
    {
        b->size += n->size;
        b->next = n->next;
    }
    else
    {
        b->next = n;
    }
    if (prev && (uint8_t *)prev + prev->size == (uint8_t *)b)
    {
        prev->size += b->size;
        prev->next = b->next;
    }
    else if (prev)
    {
        prev->next = b;
    }
    else
    {
        free_list = b;
    }
}

void heap_add(void *mem, size_t size)
{
    blk_t *b = mem;
    if (size < MIN_BLOCK)
    {
        return;
    }
    b->size = size;
    heap_total += size;
    release(b);
}

static void heap_init(void)
{
    static bool done;
    if (!done)
    {
        done = true;
        heap_add(__heap_low, (size_t)(__heap_high - __heap_low));
    }
}

/* caches that give their memory back when malloc runs short */
static bool (*reclaim)(void);

void heap_set_reclaim(bool (*fn)(void))
{
    reclaim = fn;
}

static void *malloc_(size_t n);

void *malloc(size_t n)
{
    void *p = malloc_(n);
    while (!p && reclaim && reclaim())
    {
        p = malloc_(n);
    }
    return p;
}

static void *malloc_(size_t n)
{
    blk_t *prev = NULL, *b;
    size_t need = n + HDR;
    heap_init();
    if (need < MIN_BLOCK)
    {
        need = MIN_BLOCK;
    }
    if (n > 0x7fffff)
    {
        return NULL;
    }
    for (b = free_list; b; prev = b, b = b->next)
    {
        if (b->size >= need)
        {
            if (b->size - need >= MIN_BLOCK)
            {
                /* take the end of the block: the list stays as it is */
                b->size -= need;
                b = (blk_t *)((uint8_t *)b + b->size);
                b->size = need;
            }
            else if (prev)
            {
                prev->next = b->next;
            }
            else
            {
                free_list = b->next;
            }
            heap_used += b->size;
            if (heap_used > heap_peak)
            {
                heap_peak = heap_used;
            }
            return (uint8_t *)b + HDR;
        }
    }
    return NULL;
}

/* size bytes at a multiple of 256, not crossing a 64 KB boundary (for
   blit.s, which forms addresses from their bytes), or NULL */
void *malloc_aligned(size_t size)
{
    blk_t *prev = NULL, *b;
    heap_init();
    for (b = free_list; b; prev = b, b = b->next)
    {
        uintptr_t bs = (uintptr_t)b, be = bs + b->size, p = (bs + HDR + 255) & ~(uintptr_t)255;
        for (;;)
        {
            if (p - HDR != bs && p - HDR - bs < MIN_BLOCK)
            {
                p += 256; /* no room for the free piece in front */
                continue;
            }
            if (p + size > be)
            {
                break;
            }
            if ((p >> 16) != ((p + size - 1) >> 16))
            {
                p = (p + size - 1) & ~(uintptr_t)0xffff; /* the next bank */
                continue;
            }
            {
                blk_t *a = (blk_t *)(p - HDR);
                uintptr_t end = p + size;
                /* take the block out, give back the pieces around */
                if (prev)
                {
                    prev->next = b->next;
                }
                else
                {
                    free_list = b->next;
                }
                if (be - end < MIN_BLOCK)
                {
                    end = be;
                }
                a->size = (size_t)(end - (uintptr_t)a);
                if ((uintptr_t)a != bs)
                {
                    blk_t *f = b;
                    f->size = (size_t)((uintptr_t)a - bs);
                    release(f);
                }
                if (end != be)
                {
                    blk_t *t = (blk_t *)end;
                    t->size = (size_t)(be - end);
                    release(t);
                }
                heap_used += a->size;
                if (heap_used > heap_peak)
                {
                    heap_peak = heap_used;
                }
                return (void *)p;
            }
        }
    }
    return NULL;
}

void free(void *p)
{
    blk_t *b;
    if (!p)
    {
        return;
    }
    b = (blk_t *)((uint8_t *)p - HDR);
    heap_used -= b->size;
    release(b);
}

void *realloc(void *p, size_t n)
{
    blk_t *b;
    size_t need = n + HDR;
    void *q;
    if (!p)
    {
        return malloc(n);
    }
    if (need < MIN_BLOCK)
    {
        need = MIN_BLOCK;
    }
    b = (blk_t *)((uint8_t *)p - HDR);
    if (b->size >= need)
    {
        return p;
    }
    /* grow into the free block right after it, when there is one */
    {
        blk_t *prev = NULL, *f = free_list;
        uint8_t *end = (uint8_t *)b + b->size;
        while (f && (uint8_t *)f < end)
        {
            prev = f;
            f = f->next;
        }
        if (f && (uint8_t *)f == end && b->size + f->size >= need)
        {
            size_t rest = b->size + f->size - need;
            if (rest >= MIN_BLOCK)
            {
                blk_t *r = (blk_t *)((uint8_t *)b + need);
                r->size = rest;
                r->next = f->next;
                if (prev)
                {
                    prev->next = r;
                }
                else
                {
                    free_list = r;
                }
                heap_used += need - b->size;
                b->size = need;
            }
            else
            {
                if (prev)
                {
                    prev->next = f->next;
                }
                else
                {
                    free_list = f->next;
                }
                heap_used += f->size;
                b->size += f->size;
            }
            if (heap_used > heap_peak)
            {
                heap_peak = heap_used;
            }
            return p;
        }
    }
    q = malloc(n);
    if (q)
    {
        memcpy(q, p, b->size - HDR);
        free(p);
    }
    return q;
}
