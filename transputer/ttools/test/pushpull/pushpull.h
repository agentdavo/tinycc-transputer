/*
 * transputer memory map for the pushpull sample task
 */

#ifndef PUSHPULL_H
#define PUSHPULL_H

#define PUSHPULL_BUFSIZE    (0x1000)
#define PUSH_BUF1	(0x80001000)
#define PUSH_BUF2	(PUSH_BUF1 + PUSHPULL_BUFSIZE*1)
#define PULL_BUF1	(PUSH_BUF1 + PUSHPULL_BUFSIZE*2)
#define PULL_BUF2	(PUSH_BUF1 + PUSHPULL_BUFSIZE*3)

/* Status words for each buffer; hold length of data in buffer, 0 if no data */
#define PUSH_STAT1	(0x80009000-0x20+0x00)
#define PUSH_STAT2	(0x80009000-0x20+0x04)
#define PULL_STAT1	(0x80009000-0x20+0x08)
#define PULL_STAT2	(0x80009000-0x20+0x0c)

#define PUSHPULL_DEBUG1	(0x80009000-0x20+0x10)

#endif /* PUSHPULL_H */
