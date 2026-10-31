/*
 * Simplified test for static variables - avoid complex operations
 */

// Bare-metal type definitions
typedef unsigned char uint8_t;
typedef unsigned int size_t;

// Very simple static variable test
void *simple_static_test(void) {
    static char buffer[100];  // Simple static array
    static int counter = 0;   // Simple static variable

    // Avoid complex expressions - just simple operations
    counter = counter + 1;

    // Avoid sizeof - use literal
    if (counter > 50) {
        counter = 0;
    }

    // Avoid complex pointer arithmetic
    return (void *)&buffer[0];  // Just return buffer address
}

// Test static variable access without complex operations
int test_static_simple(void) {
    static int value = 42;

    value = value * 2;  // Simple operation

    return value;
}

// Test pointer arithmetic with static variables
void *test_static_pointer(void) {
    static char buffer[100];
    static int offset = 10;

    // Simple pointer arithmetic
    return &buffer[offset];
}

// Test array access with static variables
int test_static_array(void) {
    static int array[10];
    static int index = 5;

    array[index] = 42;
    return array[index];
}

// Test complex pointer arithmetic (this might cause 0xF3)
void *test_complex_pointer(void) {
    static char buffer[100];
    static int offset = 10;

    // Complex expression: buffer + offset
    char *ptr = buffer + offset;
    return ptr;
}

// Test sizeof with static array
int test_static_sizeof(void) {
    static char buffer[100];

    return sizeof(buffer);  // This might be the issue
}

// Test the exact failing pattern: if (used + size > sizeof(memory_pool))
int test_failing_pattern(void) {
    static char memory_pool[1024];
    static size_t used = 0;
    size_t size = 100;

    // Try different formulations to avoid the problematic instruction
    size_t total = used + size;
    const size_t max_size = sizeof(memory_pool);

    if (total > max_size) {  // Break it into separate operations
        return 0;
    }

    return 1;
}