// kernel32/serial.h - COM1 (16550) debug / automation channel.
//
// The kernel logs, keyboard tracing, exception dumps, GUI command
// injection and the emulator's verification commands all go out of here.
// Input is drained from the 16-byte hardware FIFO into a 256-byte
// software ring so a slow poller cannot lose pasted input.
#ifndef MANGO_SERIAL_H
#define MANGO_SERIAL_H

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *text);

/* Non-blocking: returns the next received byte or -1 when none. */
int serial_getc(void);

#endif /* MANGO_SERIAL_H */