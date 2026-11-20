/* One image, common text/data/BSS and atomic allocation across deferred CPUs. */
typedef unsigned int u32;
unsigned int init_stack[4096] __attribute__((aligned(16384)));
static volatile u32 initialized = 0x12345678;
static volatile u32 allocated, done[8], results[8], pool[1024];
extern void board_park(void);
static u32 allocate(void)
{
    volatile u32 *ctl = (volatile u32 *)0x41000000;
    ctl[0] = (u32)&allocated;
    ctl[2] = 128;
    ctl[3] = 2;
    return ctl[4];
}
static void worker(void)
{
    u32 id = *(volatile u32 *)0x42000004, slot = allocate(), i;
    results[id] = initialized == 0x12345678 && slot < 1024;
    if (results[id])
        for (i = 0; i < 128; ++i) pool[slot+i] = id+1;
    done[id] = 1;
}
static void secondary(void)
{
    worker();
    board_park();
    for (;;) {}
}
void start_kernel(void)
{
    u32 count = *(volatile u32 *)0x42000008, cpu, i, seen = 0, bad = 0;
    const char *message;
    if (count < 2 || count > 8) bad = 1;
    for (cpu = 1; !bad && cpu < count; ++cpu) {
        volatile u32 *mailbox = (volatile u32 *)(0x40002000+cpu*16);
        mailbox[0] = (u32)secondary;
        mailbox[1] = 0x40100000+cpu*0x10000;
        mailbox[3] = 1;
    }
    if (!bad) worker();
    for (cpu = 0; !bad && cpu < count; ++cpu) {
        while (!done[cpu]) {}
        bad |= !results[cpu];
    }
    bad |= allocated != count*128;
    for (i = 0; !bad && i < count*128; i += 128) {
        u32 owner = pool[i], j;
        if (!owner || owner > count || (seen & (1u << owner))) { bad = 1; break; }
        seen |= 1u << owner;
        for (j = 0; j < 128; ++j) bad |= pool[i+j] != owner;
    }
    message = bad ? "SHARED-KERNEL-FAIL\n" : "SHARED-KERNEL-PASS\n";
    while (*message) *(volatile u32 *)0x10000000 = *message++;
    board_park();
}
