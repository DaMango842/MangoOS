// kernel32/memory.c - MangoOS physical memory manager and kernel heap.
//
// Page pool: first-fit bitmap over [MEM_POOL_BASE, MEM_POOL_END).  The
// FC render surface and its snapshots live inside the pool and are
// marked used at init.  Everything below MEM_POOL_BASE (kernel image,
// back buffer) is fixed and never enters the pool.
//
// Heap: first-fit free list over blocks carved from 64 KB chunks of
// the page pool.  Block layout:
//   [ hdr 8B: size|bit0-free, next_off ] [ payload ] [ ftr 8B: size|bit0-free ]
// stride = 16 + size, so payloads stay 8-byte aligned.  The footer
// (copy of the header tag) marks the end of the block; the next block's
// header sits right after it.  Free blocks are singly linked by their
// offset from the heap base (absolute addresses are fine too, but the
// heap may relocate conceptually; offsets keep the list address-free).

#include "memory.h"

#define MEM_RAM_TOP      0x1000000u   /* 16 MB box */
#define MEM_POOL_BASE    0x2E0000u    /* page-aligned, after the back buffer */
#define MEM_POOL_PAGES   ((MEM_RAM_TOP - MEM_POOL_BASE) >> 12)   /* 3360 */

#define MEM_FC_BASE      0x500000u    /* fc_surface + prev_win + prev_bar */
#define MEM_FC_END       0x52E000u    /* page-aligned end of the FC buffers */

#define MEM_GUI_PREV_BASE 0x530000u   /* GUI cmp-skip snapshot, 800x600x4 */
#define MEM_GUI_PREV_END  0x710000u   /* page-aligned end of the snapshot */

#define MEM_HEAP_CHUNK   (64u * 1024u)      /* pages grabbed per growth */
#define MEM_BLK_ALIGN    8u
#define MEM_BLK_MIN      16u               /* minimum payload of a block */

extern char __kernel_end[];

static unsigned char pmm_bit[MEM_POOL_PAGES / 8 + 1];
static unsigned int mem_inited;

static unsigned int heap_base;          /* first chunk of the heap, 0 = none */
static unsigned int heap_used;          /* bytes in kmalloc blocks */

/* free list: header->next holds the byte offset of the next free block
   header relative to heap_base; bit0 of size marks a free block */
static unsigned int free_head_off;

#define BLK_FREE 1u

static inline unsigned int blk_size(const void *h) {
    return ((const unsigned int *)h)[0] & ~BLK_FREE;
}

static inline int blk_free(const void *h) {
    return (int)(((const unsigned int *)h)[0] & BLK_FREE);
}

/* ---- page pool ---- */

static void pmm_bit_set(unsigned int i) {
    pmm_bit[i >> 3] |= (unsigned char)(1u << (i & 7));
}

static void pmm_bit_clr(unsigned int i) {
    pmm_bit[i >> 3] &= (unsigned char)~(1u << (i & 7));
}

static int pmm_bit_get(unsigned int i) {
    return (pmm_bit[i >> 3] >> (i & 7)) & 1u;
}

static void mem_init(void) {
    unsigned int i;

    for (i = 0; i < MEM_POOL_PAGES; i++)
        pmm_bit_clr(i);
    /* FC buffers are reserved inside the pool */
    for (i = (MEM_FC_BASE - MEM_POOL_BASE) >> 12;
         i < (MEM_FC_END - MEM_POOL_BASE) >> 12; i++)
        pmm_bit_set(i);
    /* GUI cmp-skip snapshot (render.h REN_GUI_PREV) is reserved too */
    for (i = (MEM_GUI_PREV_BASE - MEM_POOL_BASE) >> 12;
         i < (MEM_GUI_PREV_END - MEM_POOL_BASE) >> 12; i++)
        pmm_bit_set(i);
    mem_inited = 1;
}

unsigned int mem_alloc_pages(unsigned int n) {
    unsigned int i, j;

    if (!mem_inited)
        mem_init();
    if (!n || n > MEM_POOL_PAGES)
        return 0;
    for (i = 0; i <= MEM_POOL_PAGES - n; ) {
        if (pmm_bit_get(i)) {
            i++;
            continue;
        }
        for (j = i; j < i + n && !pmm_bit_get(j); j++)
            ;
        if (j == i + n) {
            for (j = i; j < i + n; j++)
                pmm_bit_set(j);
            return MEM_POOL_BASE + i * 4096u;
        }
        i = j + 1;
    }
    return 0;
}

void mem_free_pages(unsigned int addr, unsigned int n) {
    unsigned int i, idx;

    if (!n || addr < MEM_POOL_BASE || addr >= MEM_RAM_TOP ||
        (addr & 0xFFFu) || (addr - MEM_POOL_BASE) >> 12 > MEM_POOL_PAGES - n)
        return;
    idx = (addr - MEM_POOL_BASE) >> 12;
    for (i = idx; i < idx + n; i++)
        pmm_bit_clr(i);
}

/* ---- heap ---- */

static void heap_grow(unsigned int need) {
    unsigned int chunk, addr;

    chunk = need < MEM_HEAP_CHUNK ? MEM_HEAP_CHUNK : need;
    addr = mem_alloc_pages((chunk + 4095u) >> 12);
    if (!addr)
        return;
    if (!heap_base) {
        heap_base = addr;
    }
    /* carve a fresh free block over the new chunk and push it to the
       head of the free list (it is the largest block, so first-fit
       lands on it immediately) */
    ((unsigned int *)addr)[0] = chunk | BLK_FREE;   /* header: size */
    ((unsigned int *)addr)[1] = free_head_off;      /* header: next */
    ((unsigned int *)(addr + 8u + chunk))[0] = chunk | BLK_FREE;  /* footer */
    free_head_off = addr - heap_base;
}

void *kmalloc(unsigned int size) {
    unsigned int need, off, prev_off, take, rem, next_off;

    if (!size)
        size = MEM_BLK_MIN;
    need = (size + MEM_BLK_ALIGN - 1u) & ~(MEM_BLK_ALIGN - 1u);
    if (need < MEM_BLK_MIN)
        need = MEM_BLK_MIN;

    if (!free_head_off)
        heap_grow(need + 24u);
    if (!free_head_off)
        return 0;

    /* first fit */
    off = free_head_off;
    prev_off = 0;
    while (off) {
        unsigned int h = heap_base + off;
        if (blk_size((void *)h) >= need)
            break;
        prev_off = off;
        off = *(unsigned int *)(h + 4);
    }
    if (!off) {
        heap_grow(need + 24u);
        if (!free_head_off)
            return 0;
        off = free_head_off;    /* fresh chunk is at the head and fits */
    }
    take = heap_base + off;
    rem = blk_size((void *)take) - need;

    /* unlink from the free list */
    next_off = *(unsigned int *)(take + 4);
    if (prev_off)
        *(unsigned int *)(heap_base + prev_off + 4) = next_off;
    else
        free_head_off = next_off;

    if (rem >= MEM_BLK_MIN + 16u) {
        /* split: leave the tail as a new free block (its own 16 bytes
           of header+footer come out of the remainder) */
        unsigned int tail = take + 16u + need;
        rem -= 16u;
        ((unsigned int *)tail)[0] = rem | BLK_FREE;
        ((unsigned int *)tail)[1] = free_head_off;
        ((unsigned int *)(tail + 8u + rem))[0] = rem | BLK_FREE;
        free_head_off = tail - heap_base;
    }
    ((unsigned int *)take)[0] = need;               /* allocated */
    ((unsigned int *)(take + 8u + need))[0] = need;
    heap_used += need;
    return (void *)(take + 8u);
}

void kfree(void *p) {
    unsigned int h, size;

    if (!p)
        return;
    h = (unsigned int)p - 8u;
    size = blk_size((void *)h);
    heap_used -= size;
    ((unsigned int *)h)[0] = size | BLK_FREE;
    ((unsigned int *)h)[1] = free_head_off;
    ((unsigned int *)(h + 8u + size))[0] = size | BLK_FREE;
    free_head_off = h - heap_base;
}

void mem_stats(mem_stats_t *s) {
    unsigned int i, free_pages = 0;

    if (!mem_inited)
        mem_init();
    for (i = 0; i < MEM_POOL_PAGES; i++)
        if (!pmm_bit_get(i))
            free_pages++;
    s->pages_total = MEM_POOL_PAGES;
    s->pages_free = free_pages;
    s->heap_used = heap_used;
}