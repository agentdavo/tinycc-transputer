/*
 * Test file for static variables and complex operations
 * These seem to be causing 0xF3 warnings
 */

// Bare-metal type definitions
typedef unsigned char uint8_t;
typedef unsigned int size_t;

// Test static variables in functions
void *test_static_alloc(size_t size) {
    static char memory_pool[1024];  // Static array in function
    static size_t used = 0;         // Static variable

    if (used + size > sizeof(memory_pool)) {
        return 0;
    }

    void *ptr = &memory_pool[used];  // Pointer arithmetic on static array
    used += size;

    // Zero memory (complex loop)
    char *p = (char *)ptr;
    for (size_t i = 0; i < size; i++) {
        p[i] = 0;
    }

    return ptr;
}

// Test complex expressions with static variables
int test_complex_static_ops(void) {
    static int counter = 0;
    static int *ptr_array[10];

    // Complex expression involving static variables
    int result = counter++ * 2 + sizeof(ptr_array) / sizeof(void*);

    // Array access with computed index
    int index = result % 10;
    ptr_array[index] = &counter;

    return result;
}

// Test nested function calls with static state
static int call_count = 0;

int test_nested_calls(void) {
    call_count++;

    // Nested calls with static state modifications
    void *ptr1 = test_static_alloc(16);
    void *ptr2 = test_static_alloc(32);

    int result = test_complex_static_ops();

    if (ptr1 && ptr2) {
        result += call_count;
    }

    return result;
}