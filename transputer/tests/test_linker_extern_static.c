/* Negative final-link regression: only_static is defined only as a static in
 * test_reloc_extern_static_helper.c, so these references are undefined and
 * must be diagnosed, not bound to that static. */
extern int only_static(void);

int (*const extern_static_tab[])(void) = { only_static };

void test_main(void)
{
    only_static();
}
