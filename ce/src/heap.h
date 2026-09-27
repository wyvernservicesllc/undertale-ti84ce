#ifndef HEAP_H
#define HEAP_H

#include <stdbool.h>
#include <stddef.h>

/* hands more RAM to malloc (heap.c) */
void heap_add(void *mem, size_t size);

/* fn frees some cached memory and returns true, or returns false when it
   has nothing left to give: malloc calls it before failing */
void heap_set_reclaim(bool (*fn)(void));

/* size bytes at a multiple of 256 not crossing a 64 KB boundary (free
   with free), or NULL */
void *malloc_aligned(size_t size);

extern size_t heap_used, heap_peak, heap_total;

#endif
