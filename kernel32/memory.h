// kernel32/memory.h - MangoOS physical memory manager and kernel heap.
//
// Physical memory (16 MB box, 4 KB pages):
//   0x000000-0x0FFFFF  kernel image + VGA text/ROM areas (unmanaged)
//   0x100000-0x2D4C00  GUI back buffer (fixed)
//   0x2E0000-0xFFFFFF  managed page pool (first-fit bitmap)
//     - 0x500000-0x52E000  reserved: FC render surface + snapshots
//   0x1000000          top of RAM
//
// mem_alloc_pages / mem_free_pages manage whole 4 KB pages.
// kmalloc / kfree are a first-fit heap on top of the page pool; the
// heap grows in 64 KB chunks on demand.  Coalescing is not done yet
// (reserved for later): repetitive same-size alloc/free pairs reuse
// blocks because first-fit lands on the same-size blocks.
//
// The module is lazy: the first allocation initializes the bitmap and
// marks the reserved regions, so no explicit boot-time init is needed
// (kernel.c runs proc_init before anything allocates).
#ifndef MANGO_MEMORY_H
#define MANGO_MEMORY_H

typedef struct {
    unsigned int pages_total;   /* 4 KB pages under management */
    unsigned int pages_free;    /* pages not in use right now */
    unsigned int heap_used;     /* bytes currently owned by kmalloc blocks */
} mem_stats_t;

unsigned int mem_alloc_pages(unsigned int n);   /* 0 = out of memory */
void mem_free_pages(unsigned int addr, unsigned int n);
void *kmalloc(unsigned int size);               /* NULL = out of memory */
void kfree(void *p);
void mem_stats(mem_stats_t *s);

#endif /* MANGO_MEMORY_H */