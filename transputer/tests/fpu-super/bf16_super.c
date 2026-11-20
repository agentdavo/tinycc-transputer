/* Keep original-chip software and optional TVX conversion images distinct. */
#define VECTOR_HEADER "super-vectors.h"
#ifdef FPU_BF16_TVX
#define PATH_NAME CPU_NAME "-tvx"
#else
#define PATH_NAME CPU_NAME "-software"
#endif
#define PART 0
#define test_main bf16_test_main
#include "../bf16-softfloat/main.c"
