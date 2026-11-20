/* Shared-kernel idle hints: all secondary CPUs time out, then receive IPIs. */
typedef unsigned int u32;
unsigned int init_stack[4096] __attribute__((aligned(16384)));
static volatile u32 ready[8], done[8], timed[8], ticks[8], phase;
extern void board_park(void);
extern unsigned int __ldtimer(void);
extern void __sttimer(unsigned int);
static u32 clock_us(void) { return *(volatile u32 *)0x4200000cu; }
static void hint(void) { *(volatile u32 *)0x42000010u = 1; }
static void secondary(void)
{
    u32 id = *(volatile u32 *)0x42000004u;
    volatile u32 *pending = (volatile u32 *)(0x40001000u+id*4);
    u32 start, tick, i;
    /* Direct secondary entry bypasses the bootstrap STTIMER. */
    __sttimer(0);
    start = clock_us();
    tick = __ldtimer();
    for (i = 0; i < 8; i++) hint();
    timed[id] = clock_us()-start;
    ticks[id] = __ldtimer()-tick;
    ready[id] = 1;
    while (!phase) hint();
    while (!*pending) hint();
    done[id] = *pending == 1;
    *pending = 0;
    board_park();
    for (;;) { }
}
static void hex(u32 value)
{
    int shift;
    for (shift = 28; shift >= 0; shift -= 4) {
        u32 digit = (value >> shift) & 15;
        *(volatile u32 *)0x10000000u = digit < 10 ? '0'+digit : 'a'+digit-10;
    }
    *(volatile u32 *)0x10000000u = ' ';
}
void start_kernel(void)
{
    u32 count = *(volatile u32 *)0x42000008u, cpu, bad = count != 6;
    u32 deadline = clock_us()+5000000u;
    const char *text;
    for (cpu = 1; !bad && cpu < count; cpu++) {
        volatile u32 *mailbox = (volatile u32 *)(0x40002000u+cpu*16);
        mailbox[0] = (u32)secondary;
        mailbox[1] = 0x40100000u+cpu*0x10000;
        mailbox[3] = 1;
    }
    for (cpu = 1; !bad && cpu < count; cpu++) {
        while (!ready[cpu] && (int)(clock_us()-deadline) < 0) hint();
        bad |= !ready[cpu] || timed[cpu] < 8000u || !ticks[cpu];
    }
    phase = 1;
    for (cpu = 1; !bad && cpu < count; cpu++)
        *(volatile u32 *)(0x40001000u+cpu*4) = 1;
    for (cpu = 1; !bad && cpu < count; cpu++) {
        while (!done[cpu] && (int)(clock_us()-deadline) < 0) hint();
        bad |= !done[cpu];
    }
    for (cpu = 1; cpu < count; cpu++) {
        hex(cpu); hex(ready[cpu]); hex(timed[cpu]); hex(ticks[cpu]); hex(done[cpu]);
        *(volatile u32 *)0x10000000u = '\n';
    }
    text = bad ? "SHARED-IDLE-FAIL\n" : "SHARED-IDLE-PASS\n";
    while (*text) *(volatile u32 *)0x10000000u = *text++;
    board_park();
}
