// kernel32/disk.h - ATA PIO block device (master device only).
//
// LBA28, 16-bit port I/O, bounded timeouts.  The bounds matter: an
// unbounded wait on a wedged controller froze the whole kernel, so every
// poll loop caps at a fixed iteration count and reports failure.
#ifndef MANGO_DISK_H
#define MANGO_DISK_H

#include <stdint.h>

/* Both return 0 on success, non-zero on timeout / error. */
int ata_read_sectors(uint32_t lba, void *buf, unsigned int count);
int ata_write_sectors(uint32_t lba, const void *buf, unsigned int count);

#endif /* MANGO_DISK_H */