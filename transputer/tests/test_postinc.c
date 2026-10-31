/* Test post-increment code generation */

volatile int uart = 0x10000000;

/* Simple post-increment test on local pointer */
int test_postinc(const char *s) {
    const char *p = s;
    char c;

    /* This loop should increment p each iteration */
    while ((c = *p++) != '\0') {
        uart = c;  /* Output character */
    }
    return 0;
}

/* Entry point */
void test_main(void) {
    static const char msg[] = "Hello";
    test_postinc(msg);
}
