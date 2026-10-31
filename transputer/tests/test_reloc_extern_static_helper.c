/* test_reloc_extern_static_helper.c - statics whose names other TUs use
 *
 * Helper for test_reloc_extern_static.c and the negative final link in
 * test_linker_strict_modes.sh (test_linker_extern_static.c).  Neither
 * static may satisfy another object's global reference.
 */

static int shared(void)
{
    return 1;
}

static int only_static(void)
{
    return 7;
}

int helper_shared(void)
{
    return shared();
}

int (*helper_keep)(void) = only_static;
