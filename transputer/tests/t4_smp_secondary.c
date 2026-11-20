/* Secondary image calls the same boot implementation linked into Linux. */
#include <asm/t4_smp_boot.h>
int test_main(void) { tp_boot_secondary(); return 1; }
