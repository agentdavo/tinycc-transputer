/* External test functions to force CALL instruction generation */

// Forward declarations
int simple_func(int x);
int add_func(int a, int b);

// Test basic load/store operations
void test_basic_ops() {
    // Simple implementation - just return to test CALL
    volatile int x = 42;
    x = x + 1;  // Prevent optimization
}

// Test arithmetic operations via OPR
void test_arithmetic() {
    volatile int a = 10, b = 20;
    volatile int sum = a + b;
    sum = sum + 1;  // Prevent optimization
}

// Test logical operations
void test_logical() {
    volatile int xor_result = 0xAA ^ 0x55;
    xor_result = xor_result + 1;  // Prevent optimization
}

// Test comparison operations
void test_comparisons() {
    volatile int gt1 = (10 > 5) ? 1 : 0;
    gt1 = gt1 + 1;  // Prevent optimization
}

// Test jump operations
void test_jumps() {
    volatile int flag = 1;
    volatile int result = 0;
    if (flag) {
        result = 42;
    }
    result = result + 1;  // Prevent optimization
}

// Test function calls and returns
void test_functions() {
    // This will test calling other functions
    volatile int result1 = simple_func(5);
    result1 = result1 + 1;  // Prevent optimization
}

// Helper functions
int simple_func(int x) {
    return x * 2;
}

int add_func(int a, int b) {
    return a + b;
}