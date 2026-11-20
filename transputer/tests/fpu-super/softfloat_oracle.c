/* Host-only adapter: no host floating-point arithmetic in the oracle. */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "softfloat.h"

int main(void)
{
    unsigned width, mode;
    char op[16];
    uint64_t a, b, result;
    while (scanf("%u %15s %u %" SCNx64 " %" SCNx64,
                 &width, op, &mode, &a, &b) == 5) {
        softfloat_roundingMode = mode;
        softfloat_detectTininess = softfloat_tininess_afterRounding;
        softfloat_exceptionFlags = 0;
        if (width == 32) {
            float32_t x = { (uint32_t)a }, y = { (uint32_t)b }, z;
            if (!strcmp(op, "to_i32")) { result = (uint32_t)f32_to_i32(x, mode, true); goto output; }
            if (!strcmp(op, "to_f64")) { result = f32_to_f64(x).v; goto output; }
            if (!strcmp(op, "from_i32")) z = i32_to_f32((int32_t)a);
            else if (!strcmp(op, "round_int")) z = f32_roundToInt(x, mode, true);
            else if (!strcmp(op, "add")) z = f32_add(x, y);
            else if (!strcmp(op, "sub")) z = f32_sub(x, y);
            else if (!strcmp(op, "mul")) z = f32_mul(x, y);
            else if (!strcmp(op, "div")) z = f32_div(x, y);
            else if (!strcmp(op, "sqrt")) z = f32_sqrt(x);
            else return 2;
            result = z.v;
        } else if (width == 64) {
            float64_t x = { a }, y = { b }, z;
            if (!strcmp(op, "to_i32")) { result = (uint32_t)f64_to_i32(x, mode, true); goto output; }
            if (!strcmp(op, "to_f32")) { result = f64_to_f32(x).v; goto output; }
            if (!strcmp(op, "from_i32")) z = i32_to_f64((int32_t)a);
            else if (!strcmp(op, "from_u32")) z = ui32_to_f64((uint32_t)a);
            else if (!strcmp(op, "round_int")) z = f64_roundToInt(x, mode, true);
            else if (!strcmp(op, "add")) z = f64_add(x, y);
            else if (!strcmp(op, "sub")) z = f64_sub(x, y);
            else if (!strcmp(op, "mul")) z = f64_mul(x, y);
            else if (!strcmp(op, "div")) z = f64_div(x, y);
            else if (!strcmp(op, "sqrt")) z = f64_sqrt(x);
            else return 2;
            result = z.v;
        } else return 2;
output:
        printf("%016" PRIx64 " %u\n", result, (unsigned)softfloat_exceptionFlags);
    }
    return ferror(stdin) || ferror(stdout);
}
