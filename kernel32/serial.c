// kernel32/serial.c - 16550 UART (COM1, 38400 8N1)
// RX path: the UART FIFO is only 16 bytes, far smaller than a pasted
// command line; every received byte is drained into a software ring
// buffer, so slow pollers (shell, GUI) never lose input.
#include "serial.h"

#define COM1 0x3F8

#define RX_RING 256

static unsigned char rx_ring[RX_RING];
static volatile unsigned int rx_head;    /* next write position */
static volatile unsigned int rx_tail;    /* next read position */

static void outb(unsigned short port, unsigned char value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static unsigned char inb(unsigned short port) {
    unsigned char value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);    /* disable all interrupts */
    outb(COM1 + 3, 0x80);    /* enable DLAB */
    outb(COM1 + 0, 0x03);    /* divisor low  = 3   (38400 baud) */
    outb(COM1 + 1, 0x00);    /* divisor high = 0 */
    outb(COM1 + 3, 0x03);    /* 8 bits, no parity, 1 stop bit */
    outb(COM1 + 2, 0xC7);    /* enable + clear FIFOs, 14-byte threshold */
    outb(COM1 + 4, 0x0B);    /* enabled, RTS/DSR set */
    rx_head = 0;
    rx_tail = 0;
}

/* Move everything the UART FIFO currently holds into the ring buffer. */
static void serial_rx_drain(void) {
    while (inb(COM1 + 5) & 0x01) {
        unsigned int next = (rx_head + 1) & (RX_RING - 1);
        if (next == rx_tail)
            return;                     /* ring full: stop taking bytes */
        rx_ring[rx_head] = inb(COM1);
        rx_head = next;
    }
}

static void serial_wait_tx(void) {
    while (!(inb(COM1 + 5) & 0x20))
        ;
}

void serial_putc(char c) {
    serial_wait_tx();
    outb(COM1, (unsigned char)c);
}

void serial_write(const char *text) {
    while (*text)
        serial_putc(*text++);
}

int serial_getc(void) {
    serial_rx_drain();
    if (rx_head == rx_tail)
        return -1;
    {
        int c = rx_ring[rx_tail];
        rx_tail = (rx_tail + 1) & (RX_RING - 1);
        return c;
    }
}