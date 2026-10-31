/* test_reloc_iprel_bwd_helper.c - Backward CALL target
 *
 * Linked BEFORE test_reloc_iprel_bwd.c so the CALL is backward (negative offset).
 */

unsigned long iprel_bwd_helper(unsigned long a, unsigned long b)
{
    return a - b;
}
