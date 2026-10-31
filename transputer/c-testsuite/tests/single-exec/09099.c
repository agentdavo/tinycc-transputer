int printf(const char *, ...);
void *memcpy(void *d, const void *s, unsigned long n);
static char a[300], b[300];
struct box { char *dp; const char *sp; unsigned long n; };
static struct box bx;
static unsigned long getn(unsigned long x) { return x; }
int main(void) {
    int i, bad = 0;
    for (i = 0; i < 300; i++) { a[i] = (char)(i * 7 + 1); b[i] = 0; }
    /* simple */
    memcpy(b, a, 100);
    for (i = 0; i < 100; i++) if (b[i] != a[i]) bad |= 1;
    /* zero length must not corrupt */
    b[100] = 0x5A; memcpy(b + 100, a, 0);
    if (b[100] != 0x5A) bad |= 2;
    /* return value used */
    { char *r = (char *)memcpy(b + 101, a + 5, 20);
      if (r != b + 101) bad |= 4;
      for (i = 0; i < 20; i++) if (b[101 + i] != a[5 + i]) bad |= 8; }
    /* args from struct fields (non-trivial lvalues) */
    bx.dp = b + 150; bx.sp = a + 9; bx.n = 33;
    memcpy(bx.dp, bx.sp, bx.n);
    for (i = 0; i < 33; i++) if (b[150 + i] != a[9 + i]) bad |= 16;
    /* arg from a call result */
    memcpy(b + 200, a + 3, getn(41));
    for (i = 0; i < 41; i++) if (b[200 + i] != a[3 + i]) bad |= 32;
    /* unaligned src and dst */
    memcpy(b + 251, a + 2, 17);
    for (i = 0; i < 17; i++) if (b[251 + i] != a[2 + i]) bad |= 64;
    printf("memcpy bad=%d\n", bad);
    return 0;
}
