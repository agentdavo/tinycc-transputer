/* Test storing to local variables directly */

volatile int uart = 0x10000000;

/* Test post-increment on local pointer - must store new value back to local */
int test_local_ptr_inc(const char *s) {
    const char *p = s;   /* p is local, holds pointer to s */
    int count = 0;       /* count is local int */

    while (*p != '\0') {
        p++;             /* This stores new value TO p (local variable) */
        count++;         /* This stores new value TO count (local variable) */
    }
    return count;
}

void test_main(void) {
    static const char msg[] = "Hi";
    int n = test_local_ptr_inc(msg);
    uart = 'D';
    uart = '0' + n;
}
