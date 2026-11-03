// kernel32/mangofs.c - MangoFS v2 kernel file system (kernel-owned)
//
// The boot chain (stage1/stage2) does NOT read a filesystem at all:
// stage2 loads KERNEL.BIN as a raw blob (BootMap v2), then hands the kernel
// the start LBA of the filesystem region in the bootinfo block (0x502).
// Everything below that region belongs to the kernel: it formats it on
// first boot (mkfs) and then manages superblock + bitmap + root dir +
// data sectors entirely on its own.
//
// Region layout (all absolute LBAs):
//   fs_base            superblock (1 sector)
//   fs_base + 1 ..     bitmap (1 bit per sector of the whole region)
//   bitmap_lba + n ..  root directory (2 sectors, 32 x 32B)
//   root_lba + n ..    data area
//
// Superblock (512B):
//   +0  char[8]  magic    "MFSV2   "
//   +8  byte     version  2
//   +9  byte     reserved
//   +10 word     bitmap_lba
//   +12 word     root_lba
//   +14 word     root_sectors
//   +16 word     data_lba
//   +18 dword    total_sectors    (region size in sectors)
//   +22 dword    free_sectors     (free data sectors)
//   +26 ..        reserved
//
// Directory entry (32B): classic 8.3 layout, compatible with shell.c's
// mangofs_entry_t.
#include "mangofs.h"
#include "disk.h"
#include "time.h"

/* On-disk magic: 8 bytes, "MFSV2" + 3 trailing spaces (matches the
   superblock layout documented at the top of this file).  Both the mkfs
   writer and the mount validator used to loop over only 7 bytes, so the
   8th byte of the documented magic was never actually written or
   checked - it silently stayed 0x00. */
#define FS_MAGIC_STR    "MFSV2   "
#define FS_MAGIC_LEN    8
#define FS_VERSION      2
#define DISK_TOTAL      32768     /* 16 MiB image, fixed in build_img.ps1 */

#define BOOT_INFO_FS    0x502     /* dword: fs region start LBA (stage2) */

static unsigned char fs_root_buf[FS_ROOT_SECTORS * 512];
static unsigned char fs_file_buf[FS_MAX_FILE_SIZE];
static unsigned char fs_bitmap[8192];    /* 65536 sectors worth */

static unsigned int fs_super_ok = 0;
static unsigned int fs_base = 0;              /* region start */
static unsigned int fs_total = 0;              /* region size in sectors */
static unsigned int fs_bitmap_lba = 0;
static unsigned int fs_bitmap_sectors = 0;
static unsigned int fs_root_lba = 0;
static unsigned int fs_root_sectors = FS_ROOT_SECTORS;
static unsigned int fs_data_lba = 0;
static unsigned int fs_free_sectors = 0;

/* ---- low-level helpers ---- */
static unsigned int fs_get16(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int fs_get32(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static void fs_put16(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void fs_put32(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

/* ---- bitmap ---- */
static unsigned int fs_bit_used(unsigned int i) {
    return (fs_bitmap[i >> 3] >> (i & 7)) & 1;
}

static void fs_bit_set(unsigned int i, int used) {
    if (used)
        fs_bitmap[i >> 3] |= (unsigned char)(1 << (i & 7));
    else
        fs_bitmap[i >> 3] &= (unsigned char)~(1 << (i & 7));
}

static int fs_write_bitmap(void) {
    return ata_write_sectors(fs_bitmap_lba, fs_bitmap, fs_bitmap_sectors);
}

static int fs_read_bitmap(void) {
    return ata_read_sectors(fs_bitmap_lba, fs_bitmap, fs_bitmap_sectors);
}

static int fs_write_root(void) {
    return ata_write_sectors(fs_root_lba, fs_root_buf, fs_root_sectors);
}

int fs_read_root(void) {
    return ata_read_sectors(fs_root_lba, fs_root_buf, fs_root_sectors);
}

static void fs_now(unsigned short *mtime, unsigned short *mdate) {
    int y = 1980, mo = 1, d = 1, h = 0, mi = 0, s = 0;

    time_get(&y, &mo, &d, &h, &mi, &s);
    if (y < 1980)
        y = 1980;
    *mdate = (unsigned short)(((y - 1980) << 9) | (mo << 5) | d);
    *mtime = (unsigned short)((h << 11) | (mi << 5) | (s / 2));
}

/* ---- geometry ---- */
static void fs_setup_layout(void) {
    unsigned int after;

    fs_bitmap_lba = fs_base + 1;               /* bitmap right after superblock */
    fs_bitmap_sectors = (fs_total + 4095) / 4096;   /* 1 bit per sector */
    after = fs_bitmap_lba + fs_bitmap_sectors;
    fs_root_lba = after;                       /* root right after the bitmap */
    after += fs_root_sectors;
    fs_data_lba = after;
}

/* ---- mkfs: format the whole region ---- */
static int fs_mkfs(void) {
    unsigned char sb[512];
    unsigned int i, data_start_rel;

    for (i = 0; i < 512; i++)
        sb[i] = 0;
    for (i = 0; i < FS_MAGIC_LEN; i++)
        sb[i] = (unsigned char)FS_MAGIC_STR[i];
    sb[8] = FS_VERSION;
    fs_put16(sb + 10, fs_bitmap_lba);
    fs_put16(sb + 12, fs_root_lba);
    fs_put16(sb + 14, fs_root_sectors);
    fs_put16(sb + 16, fs_data_lba);
    fs_put32(sb + 18, fs_total);

    data_start_rel = fs_data_lba - fs_base;
    fs_put32(sb + 22, fs_total - data_start_rel);
    fs_free_sectors = fs_total - data_start_rel;

    if (ata_write_sectors(fs_base, sb, 1) != 0)
        return -1;

    /* bitmap: all used, then clear the data area bits */
    for (i = 0; i < 8192; i++)
        fs_bitmap[i] = 0xFF;
    fs_bitmap_sectors = (fs_total + 4095) / 4096;
    for (i = 0; i < fs_bitmap_sectors * 4096; i++) {
        if (i >= data_start_rel)
            fs_bit_set(i, 0);
    }

    /* zero the root directory */
    for (i = 0; i < FS_ROOT_SECTORS * 512; i++)
        fs_root_buf[i] = 0;
    if (fs_write_root() != 0 || fs_write_bitmap() != 0)
        return -1;
    return 0;
}

/* ---- public API ---- */

/* Mount; auto-format when the magic is missing (first boot). */
int fs_init(void) {
    unsigned char sb[512];
    int i, ok = 1;

    fs_base = *(volatile unsigned int *)BOOT_INFO_FS;
    if (fs_base == 0 || fs_base >= DISK_TOTAL - 8)
        return -1;
    fs_total = DISK_TOTAL - fs_base;
    fs_setup_layout();

    if (ata_read_sectors(fs_base, sb, 1) != 0)
        return -1;
    for (i = 0; i < FS_MAGIC_LEN; i++) {
        if (sb[i] != (unsigned char)FS_MAGIC_STR[i]) {
            ok = 0;
            break;
        }
    }
    if (!ok) {
        fs_free_sectors = 0;
        if (fs_mkfs() != 0)
            return -1;
    } else {
        fs_total = fs_get32(sb + 18);
        fs_free_sectors = fs_get32(sb + 22);
        fs_setup_layout();
        if (fs_read_bitmap() != 0)
            return -1;
    }
    fs_super_ok = 1;
    return 0;
}

int fs_ready(void) {
    return fs_super_ok;
}

int fs_root_entry(int i, mangofs_entry_t *out) {
    unsigned char *p;

    if (!fs_super_ok || i < 0 || i >= FS_MAX_FILES)
        return -1;
    p = fs_root_buf + i * 32;
    *out = *(mangofs_entry_t *)p;
    if (p[0] == 0x00)
        return 1;
    return 0;
}

void fs_parse_8_3(const char *src, char *out83) {
    int i, n;

    for (i = 0; i < 11; i++)
        out83[i] = ' ';

    n = 0;
    while (*src && *src != '.' && n < 8) {
        char c = *src++;
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        out83[n++] = c;
    }
    if (*src == '.') {
        src++;
        n = 8;
        while (*src && n < 11) {
            char c = *src++;
            if (c >= 'a' && c <= 'z')
                c -= 'a' - 'A';
            out83[n++] = c;
        }
    }
}

/* name matching for 8.3 buffers (space padded) */
static int fs_name83_eq(const unsigned char *p, const char *n) {
    int j;
    for (j = 0; j < 11; j++) {
        if (p[j] != (unsigned char)n[j])
            return 0;
    }
    return 1;
}

int fs_find(const char *name83, mangofs_entry_t *out) {
    int i;

    if (fs_read_root() != 0)
        return -1;
    for (i = 0; i < FS_MAX_FILES; i++) {
        unsigned char *p = fs_root_buf + i * 32;
        if (p[0] == 0x00)
            continue;
        if (fs_name83_eq(p, name83)) {
            *out = *(mangofs_entry_t *)p;
            return 0;
        }
    }
    return -1;
}

int fs_read_file(const mangofs_entry_t *e) {
    unsigned int sectors;

    if (e->size > FS_MAX_FILE_SIZE)
        return -1;
    sectors = (e->size + 511) / 512;
    if (sectors == 0)
        return 0;
    if (ata_read_sectors(e->start_lba, fs_file_buf, sectors) != 0)
        return -1;
    return (int)e->size;
}

const unsigned char *fs_file_data(void) {
    return fs_file_buf;
}

/* ---- allocation ---- */
static unsigned int fs_alloc(unsigned int count) {
    unsigned int i, run = 0, run_start = 0, start = fs_data_lba - fs_base;

    for (i = start; i < fs_total; i++) {
        if (!fs_bit_used(i)) {
            if (run == 0)
                run_start = i;
            run++;
            if (run >= count) {
                unsigned int lba = fs_base + run_start;
                unsigned int j;
                for (j = 0; j < count; j++)
                    fs_bit_set(run_start + j, 1);
                fs_write_bitmap();
                /* The bitmap is the source of truth; keep the cached
                   counter in step with what we just marked. */
                if (fs_free_sectors >= count)
                    fs_free_sectors -= count;
                else
                    fs_free_sectors = 0;
                return lba;
            }
        } else {
            run = 0;
        }
    }
    return 0;
}

static void fs_release(unsigned int lba, unsigned int sectors) {
    unsigned int i, base, freed = 0;

    if (lba < fs_data_lba || lba >= DISK_TOTAL)
        return;                     /* metadata or out-of-region: never free */

    base = lba - fs_base;
    for (i = 0; i < sectors; i++) {
        if (base + i >= fs_total)
            break;
        if (fs_bit_used(base + i)) {      /* only count bits we really flip */
            fs_bit_set(base + i, 0);
            freed++;
        }
    }
    fs_write_bitmap();
    /* Count what was actually cleared.  Adding the full `sectors` made the
       counter drift upward whenever the loop stopped early at the end of
       the region, so fs_free_sectors eventually reported more free space
       than the region contains. */
    fs_free_sectors += freed;
}

/* create an empty file; entry slots found in root */
int fs_create(const char *name83) {
    int i, free_slot = -1;
    unsigned short mtime, date;

    if (!fs_super_ok)
        return -1;
    if (fs_read_root() != 0)
        return -1;
    for (i = 0; i < FS_MAX_FILES; i++) {
        unsigned char *p = fs_root_buf + i * 32;
        if (p[0] == 0x00) {
            if (free_slot < 0)
                free_slot = i;
            continue;
        }
        if (fs_name83_eq(p, name83))
            return 0;                     /* already exists: treat as ok */
    }
    if (free_slot < 0)
        return -1;
    fs_now(&mtime, &date);
    {
        unsigned char *p = fs_root_buf + free_slot * 32;
        int j;
        for (j = 0; j < 11; j++)
            p[j] = (unsigned char)name83[j];
        p[11] = FS_ATTR_FILE;
        p[12] = 0;
        p[13] = 0;
        fs_put16(p + 14, 0);          /* start_lba = 0: empty */
        fs_put32(p + 16, 0);          /* size     = 0 */
        fs_put16(p + 20, mtime);
        fs_put16(p + 22, date);
        for (j = 24; j < 32; j++)
            p[j] = 0;
    }
    return fs_write_root();
}

/* Overwrite or create a file from a memory buffer. */
int fs_write_file(const char *name83, const void *data, unsigned int size) {
    mangofs_entry_t e;
    unsigned int sectors, lba, s, n, x, i;
    unsigned char tmp[512];
    unsigned short mtime, date;
    const unsigned char *src = (const unsigned char *)data;
    unsigned int pos = 0;

    if (!fs_super_ok)
        return -1;
    if (size > FS_MAX_FILE_SIZE)
        return -1;

    sectors = (size + 511) / 512;

    if (fs_find(name83, &e) == 0) {
        if (e.start_lba != 0 && e.size > 0)
            fs_release(e.start_lba, (e.size + 511) / 512);
        lba = sectors ? fs_alloc(sectors) : 0;
    } else {
        lba = sectors ? fs_alloc(sectors) : 0;
    }
    if (sectors && lba == 0)
        return -1;

    for (s = 0; s < sectors; s++) {
        unsigned int remain = size - pos;
        n = remain < 512 ? remain : 512;
        for (x = 0; x < 512; x++)
            tmp[x] = 0;
        for (x = 0; x < n; x++)
            tmp[x] = src[pos + x];
        if (ata_write_sectors(lba + s, tmp, 1) != 0)
            return -1;
        pos += n;
    }

    fs_now(&mtime, &date);
    if (fs_read_root() != 0)
        return -1;
    for (i = 0; i < FS_MAX_FILES; i++) {
        unsigned char *p = fs_root_buf + i * 32;
        if (p[0] == 0x00 || !fs_name83_eq(p, name83))
            continue;
        fs_put16(p + 14, lba);
        fs_put32(p + 16, size);
        fs_put16(p + 20, mtime);
        fs_put16(p + 22, date);
        return fs_write_root();
    }
    /* not found after write: recreate the entry (e.g. size 0 create) */
    if (fs_create(name83) == 0 && fs_read_root() == 0) {
        for (i = 0; i < FS_MAX_FILES; i++) {
            unsigned char *p = fs_root_buf + i * 32;
            if (p[0] == 0x00 || !fs_name83_eq(p, name83))
                continue;
            fs_put16(p + 14, lba);
            fs_put32(p + 16, size);
            fs_put16(p + 20, mtime);
            fs_put16(p + 22, date);
            return fs_write_root();
        }
    }
    return -1;
}

/* Delete a file: clear the directory slot, release sectors. */
int fs_delete(const char *name83) {
    mangofs_entry_t e;
    int i;

    if (!fs_super_ok)
        return -1;
    if (fs_find(name83, &e) != 0)
        return -1;
    if (e.start_lba != 0 && e.size > 0)
        fs_release(e.start_lba, (e.size + 511) / 512);
    fs_read_root();
    for (i = 0; i < FS_MAX_FILES; i++) {
        unsigned char *p = fs_root_buf + i * 32;
        if (p[0] != 0x00 && fs_name83_eq(p, name83)) {
            p[0] = 0x00;
            return fs_write_root();
        }
    }
    return -1;
}