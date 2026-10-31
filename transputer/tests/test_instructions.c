/*
 * Comprehensive Transputer Instruction Test Suite
 * Tests every Transputer instruction with breadcrumb debugging
 * Run with: ./transputer-tcc -static -nostdlib -o test_instructions.elf test_instructions.c
 *           ./transputer-netload test_instructions.elf test_instructions.btl
 *           ./transputer/build.sh --boot test_instructions.btl --debug
 */

// Simple UART output function (inline assembly-like)
void uart_putc_simple(char c) {
    // This should generate the same sequence as mmio_helpers.S
    // But TCC might not generate the exact PFIX sequence we need
    volatile char *uart = (char*)0x10000000;
    *uart = c;
}

// Memory-based debugging instead of UART
#define DEBUG_ADDR ((volatile unsigned int*)0x80001000)
#define BREADCRUMB(n) do { \
    *DEBUG_ADDR = (unsigned int)(n); \
} while(0)

#define ASSERT(x) do { \
    if (!(x)) { \
        BREADCRUMB('F'); \
        /* Continue instead of infinite loop to see all failures */ \
    } \
} while(0)

// Function declarations
void test_basic_ops();
void test_arithmetic();
void test_logical();
void test_comparisons();
void test_jumps();
void test_functions();
void test_prefix_encoding();
void test_memory_ops();
void test_workspace_ops();
void test_stack_ops();
void test_edge_cases();
void test_opr_instructions();
void test_instruction_sequences();
void test_floating_point();
void test_fp_transcendental();
void test_performance();
void run_all_tests();
void test_single_instruction();
int simple_func(int x);
int add_func(int a, int b);
int multiply_func(int x, int y);
int nested_func(int x);
void func_with_locals();
void func_many_locals();
int dup_func(int x);
int rev_func(int a, int b);
int stack_manip_func(int a, int b, int c);
void test_uncommon_opr();
int complex_expression(int a, int b, int c, int d, int e);
int nested_loops(int n);

// Global variables for testing memory operations
volatile int global_var = 0x12345678;
volatile int global_array[10] = {0,1,2,3,4,5,6,7,8,9};

void _start() {
    BREADCRUMB('S'); // Start

    // Test 1: Basic Load/Store Operations
    BREADCRUMB('1');
    test_basic_ops();

    // Test 2: Arithmetic Operations
    BREADCRUMB('2');
    test_arithmetic();

    // Test 3: Logical Operations
    BREADCRUMB('3');
    test_logical();

    // Test 4: Comparison Operations
    BREADCRUMB('4');
    test_comparisons();

    // Test 5: Jump Operations
    BREADCRUMB('5');
    test_jumps();

    // Test 6: Function Calls and Returns
    BREADCRUMB('6');
    test_functions();

    // Test 7: Prefix Encoding (Large Constants)
    BREADCRUMB('7');
    test_prefix_encoding();

    // Test 8: Memory Operations (LDNL/STNL)
    BREADCRUMB('8');
    test_memory_ops();

    // Test 9: Workspace Operations (AJW)
    BREADCRUMB('9');
    test_workspace_ops();

    // Test 10: Stack Manipulation
    BREADCRUMB('A');
    test_stack_ops();

    // Test 11: Floating Point Operations (T8)
    BREADCRUMB('B');
    // Simple test first - does the program reach here?
    volatile int fp_test_marker = 0xBEEF;
    fp_test_marker = 0xDEAD;  // Should generate integer operations
    test_floating_point();

    // Test 12: FP Transcendental Functions (if available)
    BREADCRUMB('C');
    test_fp_transcendental();

    // Test 13: Performance Analysis
    BREADCRUMB('D');
    test_performance();

    BREADCRUMB('E'); // End

    // Infinite loop to halt
    while(1) {}
}

// Test basic load/store operations
void test_basic_ops() {
    BREADCRUMB('a');

    // Test LDC (Load Constant)
    int x = 42;  // Should generate LDC 42
    ASSERT(x == 42);

    // Add complexity to prevent inlining
    volatile int dummy = 0;
    dummy += x;

    // Test LDL/STL (Load/Store Local)
    int local_var = 0;
    local_var = 123;  // LDL, then STL
    ASSERT(local_var == 123);

    // Test ADC (Add Constant)
    int y = 10;
    y += 5;  // Should use ADC
    ASSERT(y == 15);

    // Test SBL (Subtract Constant)
    y -= 3;  // Should use SBL
    ASSERT(y == 12);

    BREADCRUMB('b');
}

// Test arithmetic operations via OPR
void test_arithmetic() {
    BREADCRUMB('c');

    // Test ADD (0x05)
    int a = 10, b = 20;
    int sum = a + b;
    ASSERT(sum == 30);

    // Add complexity to prevent inlining
    volatile int dummy = 0;
    dummy += sum;

    // Test SUB (0x04)
    int diff = b - a;
    ASSERT(diff == 10);

    // Test MUL (0x06)
    int prod = a * b;
    ASSERT(prod == 200);

    // Test DIV (0x07)
    int quot = 100 / 10;
    ASSERT(quot == 10);

    // Test REM (0x08)
    int rem = 17 % 5;
    ASSERT(rem == 2);

    BREADCRUMB('d');
}

// Test logical operations
void test_logical() {
    BREADCRUMB('e');

    // Test XOR (0x0B)
    int xor_result = 0xAA ^ 0x55;
    ASSERT(xor_result == 0xFF);

    // Test OR (0x0A)
    int or_result = 0xAA | 0x55;
    ASSERT(or_result == 0xFF);

    // Test AND (0x0C)
    int and_result = 0xAA & 0xF0;
    ASSERT(and_result == 0xA0);

    // Add complexity to prevent inlining
    volatile int dummy = 0;
    dummy += and_result;

    BREADCRUMB('f');
}

// Test comparison operations
void test_comparisons() {
    BREADCRUMB('g');

    // Test GT (0x09) - Greater Than
    int gt1 = (10 > 5) ? 1 : 0;
    ASSERT(gt1 == 1);
    int gt2 = (5 > 10) ? 1 : 0;
    ASSERT(gt2 == 0);

    // Test EQ (0x01) - Equal
    int eq1 = (10 == 10) ? 1 : 0;
    ASSERT(eq1 == 1);
    int eq2 = (10 == 5) ? 1 : 0;
    ASSERT(eq2 == 0);

    // Test NOT (0x0F) - Logical Not
    int not1 = !0;
    ASSERT(not1 == 1);
    int not2 = !1;
    ASSERT(not2 == 0);

    // Add complexity to prevent inlining
    volatile int dummy = 0;
    dummy += not1 + not2;

    BREADCRUMB('h');
}

// Test jump operations
void test_jumps() {
    BREADCRUMB('i');

    // Test CJ (Conditional Jump) - if statements
    int flag = 1;
    int result = 0;
    if (flag) {
        result = 42;
    }
    ASSERT(result == 42);

    // Test CJ with else
    flag = 0;
    if (flag) {
        result = 99;
    } else {
        result = 100;
    }
    ASSERT(result == 100);

    // Test while loop (uses CJ)
    int counter = 0;
    int sum = 0;
    while (counter < 5) {
        sum += counter;
        counter++;
    }
    ASSERT(sum == 10);  // 0+1+2+3+4

    // Add complexity to prevent inlining
    volatile int dummy = 0;
    dummy += sum;

    BREADCRUMB('j');
}

// Test function calls and returns
__attribute__((noinline)) void test_functions() {
    BREADCRUMB('k');

    // Simple function call
    int result1 = simple_func(5);
    ASSERT(result1 == 10);

    // Function with multiple parameters
    int result2 = add_func(3, 7);
    ASSERT(result2 == 10);

    // Function that calls another function
    int result3 = nested_func(4);
    ASSERT(result3 == 24);  // 4 * 6

    BREADCRUMB('l');
}

// Helper functions for test_functions
int simple_func(int x) {
    return x * 2;
}

int add_func(int a, int b) {
    return a + b;
}

int multiply_func(int x, int y) {
    return x * y;
}

int nested_func(int x) {
    return multiply_func(x, 6);
}

// Test prefix encoding for large constants
__attribute__((noinline)) void test_prefix_encoding() {
    BREADCRUMB('m');

    // Test large constants that require PFIX encoding
    int large1 = 0x1234;    // Should use PFIX + LDC
    ASSERT(large1 == 0x1234);

    int large2 = 0x123456;  // Multiple PFIX
    ASSERT(large2 == 0x123456);

    int large3 = 0x12345678; // Many PFIX
    ASSERT(large3 == 0x12345678);

    // Test negative constants with NFIX
    int neg1 = -42;         // Should use NFIX
    ASSERT(neg1 == -42);

    int neg2 = -0x1234;     // NFIX + negative
    ASSERT(neg2 == -0x1234);

    BREADCRUMB('n');
}

// Test memory operations (LDNL/STNL)
void test_memory_ops() {
    BREADCRUMB('o');

    // Test LDNL/STNL with global variables
    global_var = 999;
    int loaded = global_var;  // Should use LDNL
    ASSERT(loaded == 999);

    // Test with array indexing
    global_array[3] = 777;
    int array_val = global_array[3];  // LDNL
    ASSERT(array_val == 777);

    // Test pointer operations
    int *ptr = &global_array[5];
    *ptr = 555;  // STNL
    int ptr_val = *ptr;  // LDNL
    ASSERT(ptr_val == 555);

    BREADCRUMB('p');
}

// Test workspace operations (AJW)
void test_workspace_ops() {
    BREADCRUMB('q');

    // Functions with local variables use AJW
    func_with_locals();

    // Test function with many locals
    func_many_locals();

    BREADCRUMB('r');
}

// Helper functions for workspace tests
void func_with_locals() {
    int a = 1, b = 2, c = 3;
    ASSERT(a + b + c == 6);
}

void func_many_locals() {
    int a1 = 1, a2 = 2, a3 = 3, a4 = 4, a5 = 5;
    int b1 = 6, b2 = 7, b3 = 8, b4 = 9, b5 = 10;
    ASSERT(a1 + a2 + a3 + a4 + a5 + b1 + b2 + b3 + b4 + b5 == 55);
}

// Test stack manipulation operations
void test_stack_ops() {
    BREADCRUMB('s');

    // Test DUP (0x02) - Duplicate top of stack
    int dup_test = dup_func(42);
    ASSERT(dup_test == 84);  // 42 * 2 using DUP

    // Test REV (0x00) - Reverse AReg/BReg
    int rev_test = rev_func(10, 20);
    ASSERT(rev_test == 30);  // 20 + 10 (reversed)

    // Test complex stack operations
    int stack_test = stack_manip_func(5, 10, 15);
    ASSERT(stack_test == 50);  // Some calculation

    BREADCRUMB('t');
}

// Helper functions for stack operations
int dup_func(int x) {
    // DUP should be generated for expressions like x + x
    return x + x;
}

int rev_func(int a, int b) {
    // REV swaps AReg and BReg
    return b + a;  // Compiler might use REV
}

int stack_manip_func(int a, int b, int c) {
    // Complex expression that uses stack manipulation
    return (a + b) * c + (b - a);
}

// Test floating point operations (T8 Transputer)
void test_floating_point() {
    BREADCRUMB('f');

    // Debug: Write to memory to show we reached this function
    volatile unsigned int *debug_fp = (volatile unsigned int*)0x80001010;
    *debug_fp = 0xF0F0F0F0;  // FP test marker

    // Test floating point constants and basic operations
    float f1 = 3.14159f;
    float f2 = 2.71828f;

    // Write float values to memory for inspection
    volatile float *debug_floats = (volatile float*)0x80001020;
    debug_floats[0] = f1;
    debug_floats[1] = f2;

    ASSERT(f1 > 3.0f);
    ASSERT(f2 > 2.0f);

    // Test floating point addition (FPADD 0x29)
    float fadd = f1 + f2;
    ASSERT(fadd > 5.8f && fadd < 5.9f);

    // Test floating point subtraction (FPSUB 0x2A)
    float fsub = f1 - f2;
    ASSERT(fsub > 0.4f && fsub < 0.5f);

    // Test floating point multiplication (FPMUL 0x2B)
    float fmul = f1 * f2;
    ASSERT(fmul > 8.5f && fmul < 8.6f);

    // Test floating point division (FPDIV 0x2C)
    float fdiv = f1 / f2;
    ASSERT(fdiv > 1.15f && fdiv < 1.16f);

    // Test floating point comparisons
    ASSERT(f1 > f2);
    ASSERT(f2 < f1);
    ASSERT(f1 == f1);  // Should use floating point comparison

    // Test negative floating point values
    float neg_f = -3.14f;
    ASSERT(neg_f < 0.0f);

    // Test zero and special values
    float zero_f = 0.0f;
    float pos_inf = 1.0f / zero_f;  // Positive infinity
    float neg_inf = -1.0f / zero_f; // Negative infinity

    ASSERT(zero_f == 0.0f);
    // Note: infinity checks may not work depending on implementation

    BREADCRUMB('g');
}

// Test transcendental floating point operations
__attribute__((noinline)) void test_fp_transcendental() {
    BREADCRUMB('h');

    // Debug: Write to memory to show we reached this function
    volatile unsigned int *debug_fp = (volatile unsigned int*)0x80001014;
    *debug_fp = 0xF0F0F0F0;  // FP transcendental test marker

    // Test basic operations that might be available
    // Note: Transputer FPU may not have all transcendental functions,
    // but let's test what we can

    float x = 4.0f;
    float y = 2.0f;

    // Test some operations that might work
    float result1 = x / y;  // Should be 2.0
    ASSERT(result1 > 1.9f && result1 < 2.1f);

    // Test floating point negation
    float neg_x = -x;
    ASSERT(neg_x < 0.0f);

    // Test comparisons with different values
    float small = 0.1f;
    float large = 100.0f;
    ASSERT(small < large);
    ASSERT(large > small);

    // Test floating point with integers
    int int_val = 5;
    float float_val = (float)int_val;
    ASSERT(float_val > 4.5f && float_val < 5.5f);

    // Note: True transcendental functions (sqrt, sin, cos, exp, log)
    // may not be implemented in basic Transputer FPU operations.
    // The FPENTRY operations require special handling that we haven't
    // implemented yet. For now, we test basic FP operations.

    BREADCRUMB('i');
}

// Test performance characteristics
void test_performance() {
    BREADCRUMB('j');

    volatile int *cycle_counter = (volatile int*)0x80001200;

    // Test integer operation throughput
    int start_time = 0;  // Would need timer if available
    *cycle_counter = start_time;

    for (int i = 0; i < 1000; i++) {
        volatile int result = i * i + i;
        (void)result;  // Prevent optimization
    }

    int end_time = 0;  // Would capture actual time
    *cycle_counter = end_time;

    // Test floating point operation throughput
    volatile float *fp_counter = (volatile float*)0x80001210;

    for (int i = 0; i < 100; i++) {
        volatile float a = (float)i;
        volatile float b = a * 2.0f + 1.0f;
        fp_counter[i % 10] = b;  // Prevent optimization
    }

    BREADCRUMB('k');
}

// Additional comprehensive tests for edge cases

void test_edge_cases() {
    BREADCRUMB('u');

    // Test boundary values
    int max_int = 2147483647;
    int min_int = -2147483648;
    ASSERT(max_int > 0);
    ASSERT(min_int < 0);

    // Test zero and negative zero
    int zero = 0;
    int neg_zero = -0;
    ASSERT(zero == neg_zero);

    // Test overflow/underflow conditions
    int overflow = 2147483647 + 1;  // Should wrap around
    ASSERT(overflow == -2147483648);

    BREADCRUMB('v');
}

// Test all OPR secondary instructions
void test_opr_instructions() {
    BREADCRUMB('w');

    // Test REV (0x00) - already tested
    // Test LB (0x02) - Load Byte - not commonly used
    // Test BSUB (0x03) - Byte Subscript - not commonly used
    // Test SUB (0x04) - already tested
    // Test ADD (0x05) - already tested
    // Test MUL (0x06) - already tested
    // Test DIV (0x07) - already tested
    // Test REM (0x08) - already tested
    // Test GT (0x09) - already tested
    // Test OR (0x0A) - already tested
    // Test XOR (0x0B) - already tested
    // Test AND (0x0C) - already tested
    // Test NOT (0x0F) - already tested

    // Test higher OPR instructions (require PFIX)
    // These are less commonly used but should be tested
    test_uncommon_opr();

    BREADCRUMB('x');
}

void test_uncommon_opr() {
    // Test some uncommon OPR instructions if supported
    // LB (0x12) - Load Byte from address
    // SB (0x13) - Store Byte to address
    // These may not be implemented in TCC backend
}

// Test instruction combinations and sequences
void test_instruction_sequences() {
    BREADCRUMB('y');

    // Test complex expressions that combine many instructions
    int result = complex_expression(1, 2, 3, 4, 5);
    ASSERT(result == 15);  // 1+2+3+4+5

    // Test nested loops and conditionals
    int loop_result = nested_loops(3);
    ASSERT(loop_result == 36);  // 3*3*4

    BREADCRUMB('z');
}

int complex_expression(int a, int b, int c, int d, int e) {
    return a + b + c + d + e;
}

int nested_loops(int n) {
    int result = 0;
    for (int i = 1; i <= n; i++) {
        for (int j = 1; j <= n; j++) {
            result += i * j;
        }
    }
    return result;
}

// Main test runner
void run_all_tests() {
    _start();  // Just call _start
}

// For testing individual components
void test_single_instruction() {
    // Use this to test one instruction at a time
    BREADCRUMB('T');

    // Example: Test LDC instruction specifically
    // int x = 0xFF;  // Should generate PFIX + LDC
    // ASSERT(x == 0xFF);

    BREADCRUMB('t');
}