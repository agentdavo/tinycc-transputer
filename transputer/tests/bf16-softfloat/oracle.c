/* Host adapter for the supplied SoftFloat fork's bfloat16 API. */
#include <stdio.h>
#include <stdint.h>
#include "softfloat.h"
#ifdef BINARY16
#define bfloat16_t float16_t
#define bf16_to_f32 f16_to_f32
#define f32_to_bf16 f32_to_f16
#endif
int main(void) {
    unsigned op,x;
    while (scanf("%u %x", &op, &x)==2) {
        softfloat_roundingMode=softfloat_round_near_even;
        softfloat_detectTininess=softfloat_tininess_afterRounding;
        softfloat_exceptionFlags=0;
        if (!op) {
            bfloat16_t v={(uint16_t)x}; float32_t r=bf16_to_f32(v);
            printf("%08x %u\n",(unsigned)r.v,(unsigned)softfloat_exceptionFlags);
        } else {
            float32_t v={x}; bfloat16_t r=f32_to_bf16(v);
            printf("%08x %u\n",(unsigned)r.v,(unsigned)softfloat_exceptionFlags);
        }
    }
    return 0;
}
