/* test_reloc_duplocal_helper.c - Duplicate local symbol test helper
 *
 * Defines a static "compute" function with the same name as in the
 * main test file, but different behavior (multiply instead of add).
 */

/* Our local "compute" returns x * 10 */
static unsigned long compute(unsigned long x)
{
    return x * 10;
}

unsigned long duplocal_helper_call(unsigned long x)
{
    return compute(x);
}
