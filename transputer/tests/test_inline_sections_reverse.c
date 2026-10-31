/* Same mixed-section graph as test_inline_sections.c, with the deferred
   definitions deliberately ordered before their shared callee.  Placement
   must not depend on the order in which TinyCC later emits the outlines. */

static __inline__ int reverse_shared(int value);

static __inline__ int reverse_live_path(int value)
{
    return reverse_shared(value);
}

static __inline__ int reverse_init_path(int value)
{
    return reverse_shared(value);
}

static __inline__ int reverse_shared(int value)
{
    return value + 1;
}

__attribute__((section(".init.text")))
int reverse_init_caller(int value)
{
    return reverse_init_path(value);
}

__attribute__((section(".live.text")))
int reverse_live_caller(int value)
{
    return reverse_live_path(value);
}
