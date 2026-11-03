// kernel32/mangofs.h - MangoFS v2 public interface.
//
// The on-disk format is documented at the top of mangofs.c.  Layout:
//   fs_base            superblock (1 sector)
//   fs_base + 1 ..     bitmap (1 bit per sector of the region)
//   bitmap_lba + n ..  root directory (2 sectors, 32 x 32B entries)
//   root_lba + n ..    data area
//
// Mount is automatic and self-formatting: fs_init() writes a fresh
// superblock when the magic does not match, so the kernel never needs an
// explicit mkfs at boot.
#ifndef MANGO_MANGOFS_H
#define MANGO_MANGOFS_H

#include <stdint.h>

#define FS_MAX_FILES        32
#define FS_MAX_FILE_SIZE    16384
#define FS_ROOT_SECTORS     2

#define FS_ATTR_FILE        0x00
#define FS_ATTR_DIR         0x10

/* 32-byte directory entry, classic 8.3 layout. */
typedef struct {
    char name[8];
    char ext[3];
    unsigned char attr;
    unsigned char reserved[2];
    uint16_t start_lba;
    uint32_t size;
    uint16_t mtime;
    uint16_t mdate;
    uint32_t reserved2[2];
} mangofs_entry_t;

/* Mount / query.  fs_init() auto-formats on first boot. */
int fs_init(void);
int fs_ready(void);
int fs_read_root(void);

/* Directory access.  fs_root_entry returns 0 on success, 1 for a free
   slot (out is still filled in), -1 on a bad index. */
int fs_root_entry(int i, mangofs_entry_t *out);
int fs_find(const char *name83, mangofs_entry_t *out);

/* File data.  fs_read_file returns the size in bytes or -1; the bytes
   stay valid until the next fs_read_file / fs_write_file. */
int fs_read_file(const mangofs_entry_t *e);
const unsigned char *fs_file_data(void);

int fs_create(const char *name83);
int fs_write_file(const char *name83, const void *data, unsigned int size);
int fs_delete(const char *name83);

/* Normalise a name into an 11-byte space-padded 8.3 buffer. */
void fs_parse_8_3(const char *src, char *out83);

#endif /* MANGO_MANGOFS_H */