/*
 * Test file for kernel header issues
 * Try to include some kernel headers that might cause TCC problems
 */

// Bare-metal type definitions
typedef unsigned char uint8_t;
typedef unsigned int size_t;

// Try to simulate kernel header issues
#define __KERNEL__ 1
#define CONFIG_SMP 1

// Simplified kernel types that might cause issues
typedef int bool;
#define true 1
#define false 0

// Simple test function
int test_kernel_headers(void) {
    bool flag = true;
    size_t size = 100;

    if (flag && size > 0) {
        return 1;
    }
    return 0;
}

// Test with some kernel-style macros
#define BUG() do { while(1); } while(0)
#define WARN_ON(condition) ({ if (condition) BUG(); 0; })

int test_kernel_macros(void) {
    int x = 5;
    WARN_ON(x < 0);  // This should not trigger BUG
    return x;
}

// Test with inline functions (kernel style)
static inline int kernel_inline_func(int a, int b) {
    return a + b;
}

int test_kernel_inline(void) {
    return kernel_inline_func(10, 20);
}