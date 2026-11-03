// kernel32/time.c - date/time facade: RTC access + clock formatting
#include "time.h"

/* Read the RTC with the retry the update-in-progress race needs.
   Returns 0 on success, -1 if both attempts raced (caller may retry). */
int time_get(int *year, int *month, int *day,
             int *hour, int *minute, int *second) {
    if (rtc_read(year, month, day, hour, minute, second) != 0)
        return rtc_read(year, month, day, hour, minute, second);
    return 0;
}

/* Format "YYYY-MM-DD HH:MM:SS" into buf (needs at least 20 bytes).
   Returns the string length, or 0 when the RTC read failed. */
int time_str(char *buf) {
    int y, mo, d, hh, mi, ss;
    int n = 0;

    if (time_get(&y, &mo, &d, &hh, &mi, &ss) != 0)
        return 0;
    buf[n++] = (char)('0' + (y / 1000) % 10);
    buf[n++] = (char)('0' + (y / 100) % 10);
    buf[n++] = (char)('0' + (y / 10) % 10);
    buf[n++] = (char)('0' + y % 10);
    buf[n++] = '-';
    buf[n++] = (char)('0' + (mo / 10) % 10);
    buf[n++] = (char)('0' + mo % 10);
    buf[n++] = '-';
    buf[n++] = (char)('0' + (d / 10) % 10);
    buf[n++] = (char)('0' + d % 10);
    buf[n++] = ' ';
    buf[n++] = (char)('0' + (hh / 10) % 10);
    buf[n++] = (char)('0' + hh % 10);
    buf[n++] = ':';
    buf[n++] = (char)('0' + (mi / 10) % 10);
    buf[n++] = (char)('0' + mi % 10);
    buf[n++] = ':';
    buf[n++] = (char)('0' + (ss / 10) % 10);
    buf[n++] = (char)('0' + ss % 10);
    buf[n] = '\0';
    return n;
}