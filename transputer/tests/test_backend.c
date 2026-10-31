/*
 * Transputer TCC Backend - Comprehensive Demonstration
 *
 * This program demonstrates all major features of the Transputer TinyCC backend:
 * - Basic arithmetic and logical operations
 * - Function calls and returns
 * - Floating point operations
 * - Memory operations and pointers
 * - Complex control flow
 * - Performance benchmarks
 */

// Bare-metal type definitions
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef signed int int32_t;
typedef float float32_t;
typedef unsigned int size_t;

// Debug output addresses
#define DEBUG_ADDR ((volatile uint32_t*)0x80001000)
#define UART_ADDR  ((volatile uint32_t*)0x10000000)

// Function prototypes
void test_basic_operations(void);
void test_function_calls(void);
void test_floating_point(void);
void test_memory_operations(void);
void test_control_flow(void);
void test_performance(void);

// External functions for testing CALL/RET
extern int add_numbers(int a, int b);
extern float compute_pi_approximation(void);
extern void *memory_test(void *ptr, size_t size);

// Global variables for testing
static int global_counter = 0;
static float global_float = 3.14159f;

// UART output function
void uart_putc(char c) {
    *UART_ADDR = (uint32_t)c;
}

void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s++);
    }
}

// Test basic arithmetic and logical operations
void test_basic_operations(void) {
    int a = 42;
    int b = 24;
    int result;

    // Arithmetic operations
    result = a + b;
    if (result != 66) while(1); // Error halt

    result = a - b;
    if (result != 18) while(1);

    result = a * b;
    if (result != 1008) while(1);

    result = a / b;
    if (result != 1) while(1);

    result = a % b;
    if (result != 18) while(1);

    // Logical operations
    int c = a & b;
    if (c != 8) while(1);

    c = a | b;
    if (c != 58) while(1);

    c = a ^ b;
    if (c != 50) while(1);

    c = ~a;
    if (c != -43) while(1);

    // Shift operations
    c = a << 2;
    if (c != 168) while(1);

    c = a >> 1;
    if (c != 21) while(1);

    // Increment global counter
    global_counter++;
}

// Test function calls and returns
void test_function_calls(void) {
    // Direct function calls
    int sum = add_numbers(10, 20);
    if (sum != 30) while(1);

    // Function pointer calls
    int (*add_ptr)(int, int) = add_numbers;
    sum = add_ptr(5, 15);
    if (sum != 20) while(1);

    // Nested function calls
    int result = add_numbers(add_numbers(1, 2), add_numbers(3, 4));
    if (result != 10) while(1);
}

// Test floating point operations
void test_floating_point(void) {
    float a = 3.14159f;
    float b = 2.71828f;
    float result;

    // Basic arithmetic
    result = a + b;
    if (result < 5.85f || result > 5.87f) while(1);

    result = a - b;
    if (result < 0.42f || result > 0.44f) while(1);

    result = a * b;
    if (result < 8.53f || result > 8.55f) while(1);

    result = a / b;
    if (result < 1.155f || result > 1.157f) while(1);

    // Comparisons
    if (!(a > b)) while(1);
    if (!(b < a)) while(1);
    if (!(a == a)) while(1);

    // Type conversions
    int int_val = (int)a;
    if (int_val != 3) while(1);

    float float_val = (float)int_val;
    if (float_val < 2.9f || float_val > 3.1f) while(1);

    // Update global
    global_float = a;
}

// Test memory operations and pointers
void test_memory_operations(void) {
    // Pointer operations
    int value = 12345;
    int *ptr = &value;
    if (*ptr != 12345) while(1);

    *ptr = 67890;
    if (value != 67890) while(1);

    // Array operations
    int array[10];
    for (int i = 0; i < 10; i++) {
        array[i] = i * 10;
    }

    for (int i = 0; i < 10; i++) {
        if (array[i] != i * 10) while(1);
    }

    // Pointer arithmetic
    int *arr_ptr = array;
    if (*(arr_ptr + 5) != 50) while(1);

    // Memory test function
    void *mem_result = memory_test(array, sizeof(array));
    if (mem_result != array) while(1);
}

// Test complex control flow
void test_control_flow(void) {
    // If-else statements
    int x = 10;
    int result = 0;

    if (x > 5) {
        result = 1;
    } else {
        result = 0;
    }
    if (result != 1) while(1);

    // Loops
    int sum = 0;
    for (int i = 1; i <= 10; i++) {
        sum += i;
    }
    if (sum != 55) while(1);

    // While loop
    int counter = 0;
    while (counter < 5) {
        counter++;
    }
    if (counter != 5) while(1);

    // Switch statement
    int switch_val = 3;
    int switch_result = 0;

    switch (switch_val) {
        case 1: switch_result = 10; break;
        case 2: switch_result = 20; break;
        case 3: switch_result = 30; break;
        default: switch_result = 0; break;
    }
    if (switch_result != 30) while(1);
}

// Performance benchmark
void test_performance(void) {
    volatile int perf_counter = 0;

    // Integer operations benchmark
    for (int i = 0; i < 1000; i++) {
        perf_counter += i * i;
        perf_counter -= i;
        perf_counter ^= i;
        perf_counter &= 0xFFFF;
    }

    // Floating point benchmark
    volatile float fp_perf = 0.0f;
    for (int i = 0; i < 100; i++) {
        fp_perf += (float)i;
        fp_perf *= 1.1f;
        fp_perf /= 1.05f;
    }

    // Memory access benchmark
    volatile int mem_array[100];
    for (int i = 0; i < 100; i++) {
        mem_array[i] = i;
        perf_counter += mem_array[i];
    }
}

// Main entry point
void _start(void) {
    uart_puts("Transputer TCC Backend Demo Starting...\n");

    // Test basic operations
    test_basic_operations();
    uart_puts("Basic operations: PASSED\n");

    // Test function calls
    test_function_calls();
    uart_puts("Function calls: PASSED\n");

    // Test floating point
    test_floating_point();
    uart_puts("Floating point: PASSED\n");

    // Test memory operations
    test_memory_operations();
    uart_puts("Memory operations: PASSED\n");

    // Test control flow
    test_control_flow();
    uart_puts("Control flow: PASSED\n");

    // Performance benchmark
    test_performance();
    uart_puts("Performance test: PASSED\n");

    // Success!
    uart_puts("ALL TESTS PASSED! Transputer TCC Backend is fully functional.\n");
    *DEBUG_ADDR = 0xDEADBEEF;  // Success marker

    while (1) {
        // Loop forever
    }
}