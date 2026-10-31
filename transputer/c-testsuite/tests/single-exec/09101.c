int printf(const char *, ...);
struct ent { unsigned long link[2]; unsigned long rcu[2]; unsigned long cap; unsigned long cpus[4]; };
static struct ent e;
static int check_is_bitmap(const unsigned long *b) { return 1; }
/* Exactly the shape of the kernel's to_cpumask(): a ternary with a constant
 * condition selecting the pointer, used only for its type-check side. */
#define to_bitmap(bm) ((unsigned long *)(1 ? (bm) : (void *)(unsigned long)sizeof(check_is_bitmap(bm))))
static unsigned long *span(struct ent *p) { return to_bitmap(p->cpus); }
int main(void) {
    unsigned long off = (unsigned long)span(&e) - (unsigned long)&e;
    unsigned long want = (unsigned long)&e.cpus[0] - (unsigned long)&e;
    printf("span offset %lu want %lu %s\n", off, want, off == want ? "OK" : "BAD");
    return 0;
}
