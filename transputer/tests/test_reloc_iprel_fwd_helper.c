/* test_reloc_iprel_fwd_helper.c - Forward CALL target
 *
 * Linked AFTER test_reloc_iprel_fwd.c so the CALL is forward (positive offset).
 */

unsigned long iprel_fwd_helper(unsigned long x)
{
    return x * x;
}
