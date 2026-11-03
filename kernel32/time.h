// kernel32/time.h - wall clock (CMOS RTC) and string formatting.
#ifndef MANGO_TIME_H
#define MANGO_TIME_H

/* Read the RTC through rtc_read() with the retry the update-in-progress
   race needs.  Returns 0 on success, -1 if every attempt raced. */
int time_get(int *year, int *month, int *day,
             int *hour, int *minute, int *second);

/* Format "YYYY-MM-DD HH:MM:SS" into buf (needs >= 20 bytes).
   Returns the length, or 0 when the RTC read failed. */
int time_str(char *buf);

/* rtc.c - raw CMOS RTC access; returns 0 on success. */
int rtc_read(int *year, int *month, int *day,
             int *hour, int *minute, int *second);

#endif /* MANGO_TIME_H */