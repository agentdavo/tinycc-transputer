/* Negative final-link regression: missing_link_target must be diagnosed. */
extern void missing_link_target(void);

void test_main(void)
{
    missing_link_target();
}
