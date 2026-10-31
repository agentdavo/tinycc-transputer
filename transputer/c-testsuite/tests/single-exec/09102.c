int printf(const char *, ...);
struct ent { unsigned long a[2]; unsigned long b[2]; unsigned long c; unsigned long cpus[4]; };
static struct ent e;
static int chk(const unsigned long *b) { return 1; }
static unsigned long base(void) { return (unsigned long)&e; }
static unsigned long v1(struct ent *p) { return (unsigned long)(1 ? p->cpus : (void *)0); }
static unsigned long v2(struct ent *p) { return (unsigned long)(1 ? p->cpus : (void *)(unsigned long)sizeof(int)); }
static unsigned long v3(struct ent *p) { return (unsigned long)(1 ? p->cpus : (void *)(unsigned long)sizeof(chk(p->cpus))); }
static unsigned long v4(struct ent *p) { return (unsigned long)p->cpus; }
static unsigned long v5(struct ent *p) { unsigned long *q = 1 ? p->cpus : (void *)0; return (unsigned long)q; }
int main(void) {
    unsigned long b = base();
    printf("v1=%lu v2=%lu v3=%lu v4=%lu v5=%lu\n",
           v1(&e)-b, v2(&e)-b, v3(&e)-b, v4(&e)-b, v5(&e)-b);
    return 0;
}
