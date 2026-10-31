/*
 * Minimal floating point test for Transputer TCC backend
 */

#define DEBUG_ADDR ((volatile unsigned int*)0x80001000)

void _start() {
    // Test basic floating point operation
    float a = 3.0f;
    float b = 2.0f;
    float c = a + b;  // Should generate FPADD

    // Write result to debug memory
    volatile float *result = (volatile float*)0x80001010;
    *result = c;

    // Write success marker
    *DEBUG_ADDR = 0xCAFEBABE;

    while(1) {} // Infinite loop
}