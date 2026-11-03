// kernel32/disk.c - ATA PIO (LBA28) disk driver for the primary controller
#include "disk.h"

#define ATA_BASE   0x1F0
#define ATA_DATA   0x1F0   /* r/w 16-bit data */
#define ATA_ERR    0x1F1
#define ATA_SECTOR 0x1F2   /* sector count  */
#define ATA_LBA_LO 0x1F3
#define ATA_LBA_MI 0x1F4
#define ATA_LBA_HI 0x1F5
#define ATA_DRIVE  0x1F6   /* drv|LBA|high 4 bits of LBA28 */
#define ATA_CMD    0x1F7
#define ATA_STATUS 0x1F7

#define CMD_READ_SECTORS  0x20
#define CMD_WRITE_SECTORS 0x30
#define CMD_FLUSH_CACHE   0xE7

static void outb(unsigned short port, unsigned char value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void outw(unsigned short port, unsigned short value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

static unsigned char inb(unsigned short port) {
    unsigned char value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static unsigned short inw(unsigned short port) {
    unsigned short value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

/* Bounded waits: an unbounded spin here wedged the whole kernel when the
   drive never reached the expected state (the shell hung in fs_init and
   the keyboard polling loop was never reached).  ~300k port reads is
   ample for PIO, yet far below "forever". */
static int ata_wait_not_busy(void) {
    int timeout = 300000;
    while (inb(ATA_STATUS) & 0x80)
        if (--timeout == 0)
            return -1;
    return 0;
}

static int ata_wait_ready(void) {
    int timeout = 300000;
    while (!(inb(ATA_STATUS) & 0x40))
        if (--timeout == 0)
            return -1;
    return 0;
}

/* Read `count` sectors from `lba` into `buf`. Returns 0 on success, -1 on error. */
int ata_read_sectors(uint32_t lba, void *buf, unsigned int count) {
    unsigned int s;
    uint16_t *dst = (uint16_t *)buf;

    for (s = 0; s < count; s++) {
        unsigned int i;

        if (ata_wait_not_busy() != 0)
            return -1;

        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));   /* LBA28, master */
        outb(ATA_SECTOR, 1);                             /* read 1 sector */
        outb(ATA_LBA_LO, (unsigned char)(lba >> 0));
        outb(ATA_LBA_MI, (unsigned char)(lba >> 8));
        outb(ATA_LBA_HI, (unsigned char)(lba >> 16));
        outb(ATA_CMD, CMD_READ_SECTORS);

        if (ata_wait_not_busy() != 0 || ata_wait_ready() != 0)
            return -1;

        if (inb(ATA_STATUS) & 0x01)              /* ERR set? */
            return -1;

        for (i = 0; i < 256; i++)                /* 256 words per sector */
            *dst++ = inw(ATA_DATA);

        lba++;
    }
    return 0;
}

/* Write `count` sectors from `buf` to `lba`. Returns 0 on success, -1 on error. */
int ata_write_sectors(uint32_t lba, const void *buf, unsigned int count) {
    unsigned int s;
    const uint16_t *src = (const uint16_t *)buf;

    for (s = 0; s < count; s++) {
        unsigned int i;

        if (ata_wait_not_busy() != 0)
            return -1;

        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));   /* LBA28, master */
        outb(ATA_SECTOR, 1);                             /* write 1 sector */
        outb(ATA_LBA_LO, (unsigned char)(lba >> 0));
        outb(ATA_LBA_MI, (unsigned char)(lba >> 8));
        outb(ATA_LBA_HI, (unsigned char)(lba >> 16));
        outb(ATA_CMD, CMD_WRITE_SECTORS);

        if (ata_wait_not_busy() != 0 || ata_wait_ready() != 0)
            return -1;

        if (inb(ATA_STATUS) & 0x01)              /* ERR set? */
            return -1;

        for (i = 0; i < 256; i++)                /* 256 words per sector */
            outw(ATA_DATA, *src++);

        /* flush write cache so data is committed to media */
        if (ata_wait_not_busy() != 0)
            return -1;
        outb(ATA_CMD, CMD_FLUSH_CACHE);
        if (ata_wait_not_busy() != 0)
            return -1;

        lba++;
    }
    return 0;
}