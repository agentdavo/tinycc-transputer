/* Kernel-layout fixture: all per-CPU subsections must form one template. */
extern char __per_cpu_start[], __per_cpu_end[], __per_cpu_load[], __init_begin[];
unsigned int init_stack[4096] __attribute__((aligned(16384)));
extern unsigned int pcpu_zero;
extern char __lock_text_start[], __lock_text_end[];
void __attribute__((section(".spinlock.text"))) fixture_lock(void) { }
unsigned int ordinary = 0x11223344;
unsigned int pcpu_first __attribute__((section(".data..percpu..first"))) = 1;
unsigned int pcpu_page __attribute__((section(".data..percpu..page_aligned"),aligned(4096))) = 2;
unsigned int pcpu_normal __attribute__((section(".data..percpu"))) = 3;
unsigned int pcpu_shared __attribute__((section(".data..percpu..shared_aligned"),aligned(64))) = 4;
unsigned int init_data __attribute__((section(".init.data"))) = 5;
static int inside(void *p) { return (char *)p >= __per_cpu_start && (char *)p+4 <= __per_cpu_end; }
void start_kernel(void)
{
    int bad = ((unsigned int)__per_cpu_start & 4095) || __per_cpu_load != __per_cpu_start;
    const char *s;
    bad |= (unsigned int)fixture_lock < (unsigned int)__lock_text_start || (unsigned int)fixture_lock >= (unsigned int)__lock_text_end;
    bad |= pcpu_zero != 0 || !inside(&pcpu_zero);
    bad |= !inside(&pcpu_first) || !inside(&pcpu_page) || !inside(&pcpu_normal) || !inside(&pcpu_shared);
    bad |= inside(&ordinary) || inside(&init_data) || __init_begin < __per_cpu_end;
    bad |= ((unsigned int)&pcpu_page & 4095) || ((unsigned int)&pcpu_shared & 63);
    bad |= pcpu_first != 1 || pcpu_page != 2 || pcpu_normal != 3 || pcpu_shared != 4;
    bad |= ordinary != 0x11223344 || init_data != 5;
    s = bad ? "PERCPU-LAYOUT-FAIL\n" : "PERCPU-LAYOUT-PASS\n";
    while (*s) *(volatile unsigned int *)0x10000000u = *s++;
}
