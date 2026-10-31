#include <stdio.h>
struct m { unsigned char pad; unsigned short odd; unsigned int w; };
struct s2 { short a, b, c, d; };
struct s2 N = { -1, -2, -3, -4 };
unsigned short arr[6] = { 10, 20, 30, 40, 50, 60 };
int idx = 3;
int main(void) {
    struct s2 *p = &N;
    printf("neg %d %d %d %d\n", p->a, p->b, p->c, p->d);
    printf("arr %d %d\n", arr[1], arr[idx]);
    { unsigned short *q = &arr[2]; printf("ptr %d %d\n", q[0], q[1]); }
    return 0;
}
