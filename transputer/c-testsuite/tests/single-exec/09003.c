#include <stdio.h>
struct h { unsigned short a, b, c, d; };
struct b { unsigned char p, q, r, s; };
struct h H;
struct b B;
int main(void) {
    struct h *ph = &H; struct b *pb = &B;
    ph->a = 0x1111; ph->b = 0x2222; ph->c = 0x3333; ph->d = 0x4444;
    printf("h %x %x %x %x\n", H.a, H.b, H.c, H.d);
    pb->p = 0x11; pb->q = 0x22; pb->r = 0x33; pb->s = 0x44;
    printf("b %x %x %x %x\n", B.p, B.q, B.r, B.s);
    { short sv = -3; short *sp = &sv; printf("s %d\n", *sp); }
    { signed char cv = -5; signed char *cp = &cv; printf("c %d\n", *cp); }
    return 0;
}
