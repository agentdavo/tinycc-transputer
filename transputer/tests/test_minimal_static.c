/* Minimal test for static variable comparison */
typedef unsigned int size_t;

int test_static_cmp(void) {
    static size_t used = 0;
    size_t size = 100;

    /* This is the problematic pattern */
    if (used + size > 1024) {
        return 0;
    }
    return 1;
}
