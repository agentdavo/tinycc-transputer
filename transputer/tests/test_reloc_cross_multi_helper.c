/* test_reloc_cross_multi_helper.c - Multiple cross-file functions */

unsigned long cross_add(unsigned long a, unsigned long b) { return a + b; }
unsigned long cross_sub(unsigned long a, unsigned long b) { return a - b; }
unsigned long cross_mul(unsigned long a, unsigned long b) { return a * b; }
unsigned long cross_shl(unsigned long a, unsigned long b) { return a << b; }
unsigned long cross_xor(unsigned long a, unsigned long b) { return a ^ b; }
