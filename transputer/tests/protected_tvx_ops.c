/* Compile twice: -mt9000 (software) and -mt9000tvx (native). */
#ifndef TVX_BACKEND
#error Define TVX_BACKEND to the exported function name
#endif
unsigned TVX_BACKEND(unsigned op, unsigned a, unsigned b)
{
    if (op >= 2) {
        union { float f; unsigned u; } result;
        result.f = __bf16dot(&a, &b, 1, op == 3 ? 1.0f : 0.0f);
        return result.u;
    }
    return op ? __bf16mul2(a, b) : __bf16add2(a, b);
}
