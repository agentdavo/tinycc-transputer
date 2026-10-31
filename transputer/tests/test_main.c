/* Simple main to test external function calls */

#define DEBUG_ADDR ((volatile unsigned int*)0x80001000)

// Function declarations - force external linkage
extern void test_basic_ops(void);
extern void test_arithmetic(void);
extern void test_logical(void);

void test_main(void) {
    // Use function pointers to force CALL instruction generation
    void (*func_ptr1)(void) = test_basic_ops;
    void (*func_ptr2)(void) = test_arithmetic;
    void (*func_ptr3)(void) = test_logical;

    // Call via function pointers - should generate CALL
    func_ptr1();
    func_ptr2();
    func_ptr3();

    // Mark completion
    *DEBUG_ADDR = 0xCAFEBABE;
}

// Function declarations (defined in test_functions.c)