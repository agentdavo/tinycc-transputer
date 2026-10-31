/* Compile-only regression test for TinyCC's outlined inline placement.

   Inspect with:
     ./transputer-tcc -nostdlib -nostdinc -c -o /tmp/inline-sections.o \
       transputer/tests/test_inline_sections.c
     readelf -SW -sW -rW /tmp/inline-sections.o

   inferred_inline, inferred_plain, and both nested wrappers must be emitted
   in .init.text because every direct caller is there.  explicit_inline must
   stay in .early.text.  conflicted_inline and mixed_shared must remain in
   .text because their callers do not share a section. */

extern int init_target(int);

static __inline__ __attribute__((__always_inline__))
int inferred_inline(int value)
{
    return init_target(value + 1);
}

static __inline__ int inferred_plain(int value)
{
    return init_target(value + 4);
}

static __inline__ int nested_inner(int value)
{
    return init_target(value + 5);
}

static __inline__ int nested_outer(int value)
{
    return nested_inner(value);
}

static __inline__ __attribute__((__always_inline__, section(".early.text")))
int explicit_inline(int value)
{
    return value + 2;
}

static __inline__ __attribute__((__always_inline__))
int conflicted_inline(int value)
{
    return value + 3;
}

static __inline__ int mixed_shared(int value)
{
    return value + 6;
}

static __inline__ int mixed_init_path(int value)
{
    return mixed_shared(value);
}

static __inline__ int mixed_live_path(int value)
{
    return mixed_shared(value);
}

static __inline__ int address_target(int value)
{
    return value + 7;
}

static __inline__ int init_address_path(int value)
{
    int (*fn)(int) = address_target;
    return fn(value);
}

__attribute__((section(".init.text")))
int init_caller(int value)
{
    return inferred_inline(value) + explicit_inline(value)
           + conflicted_inline(value) + inferred_plain(value)
           + nested_outer(value) + mixed_init_path(value)
           + init_address_path(value);
}

__attribute__((section(".live.text")))
int live_caller(int value)
{
    return conflicted_inline(value) + mixed_live_path(value);
}
