// kernel32/rtc.c - CMOS RTC driver (date/time commands)
#define RTC_INDEX 0x70
#define RTC_DATA  0x71

static unsigned char rtc_data(void) {
    unsigned char v;
    __asm__ volatile("inb %1, %b0" : "=a"(v) : "Nd"(RTC_DATA));
    return v;
}

static unsigned char rtc_in(unsigned char reg) {
    __asm__ volatile("outb %b0, %1" : : "a"(reg | 0x80), "Nd"(RTC_INDEX));  /* NMI off */
    __asm__ volatile("outb %b0, %1" : : "a"(0x00), "Nd"(0x80));              /* delay */
    return rtc_data();
}

static int bcd_to_bin(unsigned char bcd) {
    return (int)((bcd >> 4) * 10 + (bcd & 0x0F));
}

/* Read date and time. Returns 0 on success, -1 on retry-needed (rare).
   Interrupts stay disabled kernel-wide, so the read is naturally atomic. */
int rtc_read(int *year, int *month, int *day,
             int *hour, int *minute, int *second) {
    unsigned char sec1, sec2;

    while (rtc_in(0x0A) & 0x80)   /* wait for update-in-progress clear */
        ;

    sec1 = rtc_in(0x00);
    *minute = bcd_to_bin(rtc_in(0x02));
    *hour = bcd_to_bin(rtc_in(0x04));
    *day = bcd_to_bin(rtc_in(0x07));
    *month = bcd_to_bin(rtc_in(0x08));
    *year = bcd_to_bin(rtc_in(0x09)) + 2000;
    sec2 = rtc_in(0x00);

    __asm__ volatile("outb %b0, %1" : : "a"(0x0A), "Nd"(RTC_INDEX));  /* NMI on */

    if (sec1 != sec2)
        return -1;
    *second = bcd_to_bin(sec1);
    return 0;
}