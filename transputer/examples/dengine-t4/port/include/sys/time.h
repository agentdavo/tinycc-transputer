/* The platform clock is the frame clock of the port, so every node sees the
 * same time for the same frame. */
#ifndef DE_SYS_TIME_H
#define DE_SYS_TIME_H
struct timeval { long tv_sec; long tv_usec; };
#define gettimeofday de_gettimeofday
int gettimeofday(struct timeval *tv, void *tz);
#endif
