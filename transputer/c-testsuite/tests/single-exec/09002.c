#include <stdio.h>
struct s { unsigned short a, b, c, d; };
struct s S = { 0x1111, 0x2222, 0x3333, 0x4444 };
int main(void) {
    struct s *p = &S;
    printf("a=%x b=%x c=%x d=%x\n", p->a, p->b, p->c, p->d);
    printf("a+b=%x  c+d=%x\n", p->a + p->b, p->c + p->d);
    if (p->b == 0x2222) printf("b-cmp OK\n"); else printf("b-cmp BAD\n");
    if (p->d == 0x4444) printf("d-cmp OK\n"); else printf("d-cmp BAD\n");
    return 0;
}
