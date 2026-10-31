#ifndef _SIGNAL_H
#define _SIGNAL_H

typedef void (*sighandler_t)(int);

#define SIGINT 2
#define SIGILL 4
#define SIGABRT 6
#define SIGFPE 8
#define SIGSEGV 11
#define SIGTERM 15

sighandler_t signal(int signum, sighandler_t handler);

#endif
