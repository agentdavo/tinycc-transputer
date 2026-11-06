/*
 * syscall_stub.c - Standalone __transputer_syscall for userspace testing
 *
 * Same as kernel/syscall.c but without kernel headers dependency.
 * Link this with test programs instead of the kernel version.
 *
 * For operations that need kernel VFS (mount, stat, execve, mknod),
 * the stub calls through the syscall gateway published by the kernel
 * at TP_SYSCALL_GATE_ADDR.
 */

#define UART_DATA_REG  (*(volatile unsigned char *)0x10000000)

static void uart_putc(int c)
{
	UART_DATA_REG = (unsigned char)c;
}

static void uart_hex4(unsigned int v)
{
	static const char hx[] = "0123456789abcdef";
	uart_putc(hx[(v >> 12) & 0xf]);
	uart_putc(hx[(v >>  8) & 0xf]);
	uart_putc(hx[(v >>  4) & 0xf]);
	uart_putc(hx[(v >>  0) & 0xf]);
}

static void uart_hex8(unsigned long v)
{
	uart_hex4((unsigned int)(v >> 16));
	uart_hex4((unsigned int)(v & 0xffffUL));
}

#ifndef TP_TRACE_ALLOC
#define TP_TRACE_ALLOC 0
#endif

#define __NR_dup            23
#define __NR_dup3           24
#define __NR_pipe2          59
#define __NR_ioctl          29
#define __NR_fstat          80
#define __NR_mount          40
#define __NR_chdir          49
#define __NR_openat         56
#define __NR_close          57
#define __NR_read           63
#define __NR_write          64
#define __NR_writev         66
#define __NR_exit           93
#define __NR_exit_group     94
#define __NR_waitid         95
#define __NR_set_tid_address 96
#define __NR_clock_gettime  113
#define __NR_rt_sigaction   134
#define __NR_rt_sigprocmask 135
#define __NR_sigtimedwait   137
#define __NR_umask          166
#define __NR_setsid         157
#define __NR_getpid         172
#define __NR_getppid        173
#define __NR_getuid         174
#define __NR_geteuid        175
#define __NR_getgid         176
#define __NR_getegid        177
#define __NR_gettid         178
#define __NR_socket         198
#define __NR_brk            214
#define __NR_munmap         215
#define __NR_execve         221
#define __NR_mmap2          222
#define __NR_wait4          260
#define __NR_getcwd        1029
#define __NR_open          1024
#define __NR_mknod         1027
#define __NR_mkdir         1030
#define __NR_stat          1038
#define __NR_lstat         1039
#define __NR_pipe          1040
#define __NR_dup2          1041
#define __NR_fcntl         1052
#define __NR_lseek         1057
#define __NR_vfork         1071
#define __NR_tp_spawn      2000
#define __NR_uname          160
#define __NR_getdents64     61
#define __NR_getdents       1065
#define __NR_unlinkat       35
#define __NR_renameat       38
#define __NR_fchdir         50
#define __NR_faccessat      48
#define __NR_fstatat        79
#define __NR_newfstatat     79
#define __NR_readlinkat     78
#define __NR_fchmodat       53
#define __NR_fchownat       54
#define __NR_utimensat      88
#define __NR_statx          291

#define ENOSYS 38
#define EBADF  9
#define ENOTTY 25
#define ENOMEM 12
#define ENOENT 2
#define EINVAL 22
#define EAGAIN 11
#define ECHILD 10
#define EFAULT 14
#define EMFILE 24
#define ERANGE 34

#define TP_AT_FDCWD         (-100)
#define TP_MAX_FDS          16
#define TP_FD_CONSOLE       1
#define TP_FD_NULL          2
#define TP_FD_ROOT          3
#define TP_FD_SOCKET        4

/*
 * Syscall gateway — the kernel publishes the address of its own
 * __transputer_syscall at this fixed location.  For operations that
 * need kernel VFS we call through this pointer.
 */
#define TP_SYSCALL_GATE_ADDR 0x80000060UL

typedef long (*tp_syscall_fn_t)(long, long, long, long, long, long, long);

static long tp_kernel_syscall(long nr, long a1, long a2, long a3,
			      long a4, long a5, long a6)
{
	tp_syscall_fn_t gate =
		*(volatile tp_syscall_fn_t *)TP_SYSCALL_GATE_ADDR;

	if (!gate)
		return -ENOSYS;
	return gate(nr, a1, a2, a3, a4, a5, a6);
}

struct tp_fd_state {
	int kind;
	long flags;
};

static struct tp_fd_state tp_fd_table[TP_MAX_FDS] = {
	[0] = { TP_FD_CONSOLE, 0 },
	[1] = { TP_FD_CONSOLE, 0 },
	[2] = { TP_FD_CONSOLE, 0 },
};

static int tp_streq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static int tp_fd_valid(int fd)
{
	return fd >= 0 && fd < TP_MAX_FDS && tp_fd_table[fd].kind != 0;
}

static int tp_fd_alloc(int kind, long flags)
{
	int fd;

	for (fd = 3; fd < TP_MAX_FDS; fd++) {
		if (tp_fd_table[fd].kind == 0) {
			tp_fd_table[fd].kind = kind;
			tp_fd_table[fd].flags = flags;
			return fd;
		}
	}

	return -EMFILE;
}

static int tp_open_path(const char *path, long flags)
{
	if (!path)
		return -EFAULT;
	if (tp_streq(path, "/dev/console"))
		return tp_fd_alloc(TP_FD_CONSOLE, flags);
	if (tp_streq(path, "/dev/null"))
		return tp_fd_alloc(TP_FD_NULL, flags);
	if (tp_streq(path, "/"))
		return tp_fd_alloc(TP_FD_ROOT, flags);
	return -ENOENT;
}

/* Tentative definitions: standalone users such as PID 1 do not link the
 * uClibc allocator that normally provides these mmap argument slots.  When
 * libc is present its matching common definitions coalesce with these. */
long __transputer_mmap2_addr;
long __transputer_mmap2_len;
long __transputer_mmap2_prot;
long __transputer_mmap2_flags;
long __transputer_mmap2_fd;
long __transputer_mmap2_offset;

/*
 * Opt-in P trap ABI v1: A = number, B = user pointer to six words,
 * resumed A = signed result. The published ABI word is constant during
 * a call. Older kernels publish an aligned pointer here, never this tag.
 */
#ifndef TP_USE_T9K_SYSCALL
#define TP_USE_T9K_SYSCALL 0
#endif

#if TP_USE_T9K_SYSCALL
#define TP_TRAP_ABI_ADDR 0x8000006CUL
#define TP_TRAP_ABI_V1   0x54504601u

static int tp_has_frame_syscall(void)
{
	return *(volatile const unsigned int *)TP_TRAP_ABI_ADDR == TP_TRAP_ABI_V1;
}

extern long tp_trap_frame(long number, const long *args);
__asm__(
".text\n"
".global tp_trap_frame\n"
"tp_trap_frame:\n"
"  .byte 0x72, 0x71\n" /* ldl 2; ldl 1 */
"  .byte 0x60, 0xf8\n" /* syscall */
"  .byte 0x22, 0xf0\n" /* ret, with the supervisor result in A */
);
#endif

long __transputer_mmap2_packed(const long *args)
{
	static unsigned long mmap_bump = 0;
	unsigned long len;
	unsigned long result;
	tp_syscall_fn_t gate;

	if (!args)
		return -EFAULT;

#if TP_USE_T9K_SYSCALL
	if (tp_has_frame_syscall())
		return tp_trap_frame(__NR_mmap2, args);
#endif

	/*
	 * Prefer the kernel gate (real pages via __get_free_pages).
	 * The old userspace bump at 0x8FE00000 returns fake addresses with
	 * no backing store — busybox ls/malloc then faults or spins forever
	 * while logging [mADDR] for every page.
	 */
	gate = *(volatile tp_syscall_fn_t *)TP_SYSCALL_GATE_ADDR;
	if (gate)
		return gate(__NR_mmap2,
			args[0], args[1], args[2], args[3], args[4], args[5]);

	/* Bare-metal fallback: simple bump (host tests only) */
	if (mmap_bump == 0)
		mmap_bump = 0x8FE00000UL;

	len = (unsigned long)args[1];
	len = (len + 4095UL) & ~4095UL;
	result = mmap_bump;
	mmap_bump += len;

	return (long)result;
}


long __transputer_mmap2_globals(void)
{
	long args[6];

	args[0] = __transputer_mmap2_addr;
	args[1] = __transputer_mmap2_len;
	args[2] = __transputer_mmap2_prot;
	args[3] = __transputer_mmap2_flags;
	args[4] = __transputer_mmap2_fd;
	args[5] = __transputer_mmap2_offset;
	return __transputer_mmap2_packed(args);
}

long __transputer_syscall(long number, long a1, long a2, long a3,
			  long a4, long a5, long a6)
{
#if TP_USE_T9K_SYSCALL
	if (tp_has_frame_syscall()) {
		const long args[6] = { a1, a2, a3, a4, a5, a6 };

		return tp_trap_frame(number, args);
	}
#endif
	/*
	 * L-mode / no hard P: C-call gate (and local brk/mmap handling).
	 */
	/*
	 * When running under the kernel, route most syscalls through
	 * the kernel gate.  Only handle a few locally (brk, mmap for
	 * heap management, ioctl/fcntl for fd metadata, signals, IDs).
	 * This gives userspace full VFS access (open/read/write files,
	 * lseek, etc.) which native TCC needs for compilation.
	 */
	switch (number) {
	case __NR_execve:
	case __NR_mount:
	case __NR_mkdir:
	case __NR_mknod:
	case __NR_stat:
	case __NR_lstat:
	case __NR_fstat:
	case __NR_fstatat:
	case __NR_chdir:
	case __NR_fchdir:
	case __NR_getcwd:
	case __NR_vfork:
	case __NR_tp_spawn:
	case __NR_wait4:
	case __NR_waitid:
	case __NR_open:
	case __NR_openat:
	case __NR_close:
	case __NR_read:
	case __NR_write:
	case __NR_writev:
	case __NR_dup:
	case __NR_dup2:
	case __NR_dup3:
	case __NR_pipe:
	case __NR_pipe2:
	case __NR_socket:
	case __NR_lseek:
	case __NR_ioctl:
	case __NR_fcntl:
	case __NR_uname:
	case __NR_getdents64:
	case __NR_getdents:
	case __NR_unlinkat:
	case __NR_renameat:
	case __NR_faccessat:
	case __NR_readlinkat:
	case __NR_fchmodat:
	case __NR_fchownat:
	case __NR_utimensat:
	case __NR_statx:
		return tp_kernel_syscall(number, a1, a2, a3, a4, a5, a6);

	case __NR_brk:
	case __NR_mmap2: {
		/*
		 * Under the kernel: use mm-relative sys_brk/sys_mmap_pgoff.
		 * Fixed local arenas at 0x8FC00000/0x8FE00000 collide with
		 * init (~0x8fef) and high-loaded busybox (~0x8ff0), corrupting
		 * multi-applet argv (uname "applet not found").
		 * Bare-metal (no gate): fall through to local bump handlers.
		 */
		tp_syscall_fn_t gate =
			*(volatile tp_syscall_fn_t *)TP_SYSCALL_GATE_ADDR;
		if (gate)
			return gate(number, a1, a2, a3, a4, a5, a6);
		break;
	}
	}

	/*
	 * open/openat/close/read/write/writev/dup/dup2/dup3/socket/pipe/lseek
	 * are now all routed through the kernel gate above.
	 * The following handles only syscalls that are purely local.
	 */
	switch (number) {

	case __NR_exit:
	case __NR_exit_group: {
		/*
		 * Route exit through the kernel gate if available.
		 * This is essential for tp_spawn: the kernel's exit handler
		 * checks for an active spawn context and longjmps back to
		 * the shell instead of halting.
		 */
		tp_syscall_fn_t gate =
			*(volatile tp_syscall_fn_t *)TP_SYSCALL_GATE_ADDR;
		if (gate) {
			uart_putc('X');
			uart_putc('G');
			return gate(number, a1, a2, a3, a4, a5, a6);
		}
		uart_putc('\n');
		uart_putc('X');
		uart_putc('0' + (char)(a1 & 0xF));
		__asm__ __volatile__(".byte 0x21, 0xF5");
		while (1)
			;
		return 0;
	}

	case __NR_brk: {
		static unsigned long current_brk = 0;
		unsigned long addr = (unsigned long)a1;

		if (current_brk == 0)
			current_brk = 0x8FC00000UL; /* past loaded binaries (~0x8fab0000), aligned with kernel syscall.c */
#if TP_TRACE_ALLOC
		uart_putc('B');
		uart_putc(':');
		uart_hex8(addr);
		uart_putc(':');
#endif
		if (addr == 0)
			return (long)current_brk;
		if (addr >= current_brk)
			current_brk = addr;
#if TP_TRACE_ALLOC
		uart_hex8(current_brk);
		uart_putc('\n');
#endif
		return (long)current_brk;
	}

	/* ioctl and fcntl now routed through kernel gate above */

	/* chdir now routed through kernel gate above */

	case __NR_setsid:
		return 1;

	case __NR_rt_sigaction:
	case __NR_rt_sigprocmask:
		return 0;

	case __NR_sigtimedwait:
		return -EAGAIN;

	case __NR_getpid:
	case __NR_gettid:
	case __NR_set_tid_address:
		return 1;

	case __NR_getppid:
		return 0;

	case __NR_getuid:
	case __NR_geteuid:
	case __NR_getgid:
	case __NR_getegid:
		return 0;

	case __NR_clock_gettime: {
		long *tp = (long *)a2;
		if (tp) {
			tp[0] = 0;
			tp[1] = 0;
		}
		return 0;
	}

	/* waitid/wait4 now routed through kernel gate above */

	case __NR_mmap2: {
		/* Simple bump allocator for NOMMU anonymous mmap.
		 * Needed by uClibc-ng malloc to extend the heap beyond
		 * the 256-byte static initial pool.  Mirror the kernel-side
		 * arena layout so userspace probes don't allocate inside the
		 * historical 0x80200000-0x80400000 kernel overlap window. */
		long args[6];

		args[0] = a1;
		args[1] = a2;
		args[2] = a3;
		args[3] = a4;
		args[4] = a5;
		args[5] = a6;
		return __transputer_mmap2_packed(args);
	}

	case __NR_munmap:
		return 0;

	/* vfork now routed through kernel gate above */

	/* getcwd now routed through kernel gate above */

	case __NR_pipe:
	case __NR_pipe2:
		return -ENOSYS;

	case __NR_umask: {
		static int current_umask = 022;
		int old = current_umask;

		current_umask = (int)a1 & 0777;
		return old;
	}

	default: {
		/*
		 * Under the kernel: forward unknown syscalls to the gate so
		 * multi-applet BusyBox (ls/getdents, uname, tar, …) works as
		 * the kernel grows handlers. Bare-metal (no gate): ENOSYS.
		 */
		tp_syscall_fn_t gate =
			*(volatile tp_syscall_fn_t *)TP_SYSCALL_GATE_ADDR;
		if (gate)
			return gate(number, a1, a2, a3, a4, a5, a6);
		return -ENOSYS;
	}
	}
}
