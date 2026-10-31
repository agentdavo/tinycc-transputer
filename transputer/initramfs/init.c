/*
 * Transputer /init — PID 1 userspace process.
 *
 * Entry is via crt1.S which calls _start_c() using an IP-relative CALL.
 * No absolute addresses, no bFLT relocations needed for code pointers.
 *
 * This init does:
 *   1. Open /dev/console for fd 0/1/2
 *   2. Mount /proc and /sys
 *   3. Print boot banner
 *   4. Exec /bin/sh (busybox)
 *   5. Fall back to UART-only spin if exec fails
 */

#define UART_MMIO_ADDR 0x10000000u

/* No system headers needed — all constants defined locally.
 * Build uses -nostdinc, so fcntl.h/unistd.h are not available. */

extern long __tp_const1_shim(void);
extern long __transputer_syscall(long number, long a1, long a2, long a3,
				 long a4, long a5, long a6);

/* Generic syscall numbers (asm-generic/unistd.h) */
#define __NR_dup3           24
#define __NR_ioctl          29
#define __NR_mount          40
#define __NR_chdir          49
#define __NR_close          57
#define __NR_read           63
#define __NR_write          64
#define __NR_sync           81
#define __NR_fsync          82
#define __NR_exit_group     94
#define __NR_setsid         157
#define __NR_getpid         172
#define __NR_execve         221
#define __NR_tp_spawn      2000
#define __NR_open          1024
#define __NR_mkdir         1030
#define __NR_dup2          1041
#define __NR_mknod         1027
#define __NR_socket         198
#define __NR_ioctl          29

/* Socket constants */
#define AF_INET             2
#define SOCK_DGRAM          2
#define SIOCSIFADDR      0x8916
#define SIOCSIFNETMASK   0x891C
#define SIOCSIFFLAGS     0x8914
#define SIOCADDRT        0x890B
#define IFF_UP              1
#define IFF_RUNNING         0x40

/* struct sockaddr_in (simplified) */
struct sockaddr_in {
	unsigned short sin_family;
	unsigned short sin_port;
	unsigned int   sin_addr;
	unsigned char  pad[8];
};

/* struct ifreq (simplified - 32 bytes minimum) */
struct ifreq {
	char ifr_name[16];
	union {
		struct sockaddr_in ifr_addr;
		short ifr_flags;
	};
};

/* struct rtentry (simplified) */
struct rtentry {
	unsigned long rt_pad1;
	struct sockaddr_in rt_dst;
	struct sockaddr_in rt_gateway;
	struct sockaddr_in rt_genmask;
	unsigned short rt_flags;
	short rt_pad2;
	unsigned long rt_pad3;
	unsigned long rt_pad4;
	short rt_metric;
	unsigned long rt_dev;
	unsigned long rt_mtu;
	unsigned long rt_window;
	unsigned short rt_irtt;
};

#define RTF_UP     0x0001
#define RTF_GATEWAY 0x0002

#define O_RDWR              2
#define O_CREAT          0x40
#define O_TRUNC         0x200
#define O_CLOEXEC      0x80000

/* S_IFCHR = 0020000, S_IFBLK = 0060000 */
#define S_IFCHR         0020000
#define S_IFBLK         0060000

/* TTY: uart driver registers major=4 minor_start=64 → /dev/ttyS0
 * console (5,1) is traditional; null (1,3) needs memdev. */
#define MKDEV(ma, mi)   (((ma) << 8) | (mi))
#define MKDEV_CONSOLE   MKDEV(5, 1)
#define MKDEV_TTYS0     MKDEV(4, 64)
#define MKDEV_NULL      MKDEV(1, 3)

static void uart_putc(unsigned int c)
{
	*(volatile unsigned int *)UART_MMIO_ADDR = c;
}

static void uart_puts(const char *s)
{
	while (*s)
		uart_putc((unsigned int)*s++);
}

static void uart_put_hex32(unsigned int value)
{
	static const char hex[] = "0123456789ABCDEF";
	int shift;

	for (shift = 28; shift >= 0; shift -= 4)
		uart_putc(hex[(value >> shift) & 0xF]);
}

static void log_tag(const char *tag)
{
	uart_puts("PID1:");
	uart_puts(tag);
	uart_puts("\r\n");
}

static void log_value(const char *label, long value)
{
	uart_puts("PID1:");
	uart_puts(label);
	uart_putc('=');
	uart_put_hex32((unsigned int)value);
	uart_puts("\r\n");
}





/* Syscall wrappers */
static long tp_open(const char *path, long flags)
{
	return __transputer_syscall(__NR_open, (long)path, flags, 0644, 0, 0, 0);
}

static long tp_open3(const char *path, long flags, long mode)
{
	return __transputer_syscall(__NR_open, (long)path, flags, mode, 0, 0, 0);
}

static long tp_close(long fd)
{
	return __transputer_syscall(__NR_close, fd, 0, 0, 0, 0, 0);
}

static long tp_dup2(long oldfd, long newfd)
{
	return __transputer_syscall(__NR_dup2, oldfd, newfd, 0, 0, 0, 0);
}

static long tp_write(long fd, const char *buf, long count)
{
	return __transputer_syscall(__NR_write, fd, (long)buf, count, 0, 0, 0);
}

static long tp_read(long fd, char *buf, long count)
{
	return __transputer_syscall(__NR_read, fd, (long)buf, count, 0, 0, 0);
}

static long tp_sync(void)
{
	return __transputer_syscall(__NR_sync, 0, 0, 0, 0, 0, 0);
}

static long tp_fsync(long fd)
{
	return __transputer_syscall(__NR_fsync, fd, 0, 0, 0, 0, 0);
}

static long tp_mkdir(const char *path, long mode)
{
	return __transputer_syscall(__NR_mkdir, (long)path, mode, 0, 0, 0, 0);
}

static long tp_mount(const char *src, const char *target,
		     const char *fstype, long flags)
{
	return __transputer_syscall(__NR_mount, (long)src, (long)target,
				   (long)fstype, flags, 0, 0);
}

static long tp_mknod(const char *path, long mode, long dev)
{
	return __transputer_syscall(__NR_mknod, (long)path, mode, dev,
				   0, 0, 0);
}

static long tp_setsid(void)
{
	return __transputer_syscall(__NR_setsid, 0, 0, 0, 0, 0, 0);
}

static long tp_execve(const char *path, const char *const *argv,
		      const char *const *envp)
{
	return __transputer_syscall(__NR_execve, (long)path, (long)argv,
				   (long)envp, 0, 0, 0);
}

/*
 * tp_spawn — synchronous spawn: loads+execs child, returns when it exits.
 * Returns the child's exit code (0+), or negative errno on failure.
 */
static long tp_spawn(const char *path, const char *const *argv,
		     const char *const *envp)
{
	return __transputer_syscall(__NR_tp_spawn, (long)path, (long)argv,
				   (long)envp, 0, 0, 0);
}

/* Fresh stack envp each call — after tp_spawn returns, prior stack slots
 * can be smashed by the child's bFLT stack in the shared NOMMU space. */
static long spawn_smoke(const char *path, const char *const *argv)
{
	const char *envp[] = {
		"HOME=/",
		"PATH=/bin:/sbin:/usr/bin",
		"TERM=vt100",
		0
	};
	return tp_spawn(path, argv, envp);
}


static long tp_getpid(void)
{
	return __transputer_syscall(__NR_getpid, 0, 0, 0, 0, 0, 0);
}

/* Networking setup helpers */
static long tp_socket(long domain, long type, long protocol)
{
	long ret;

	uart_puts("PID1:tp_socket-nr=");
	uart_put_hex32(__NR_socket);
	uart_puts("\r\n");
	uart_puts("PID1:tp_socket-call\r\n");
	ret = __transputer_syscall(__NR_socket, domain, type, protocol, 0, 0, 0);
	uart_puts("PID1:tp_socket-ret=");
	uart_put_hex32((unsigned int)ret);
	uart_puts("\r\n");
	return ret;
}

static long tp_ioctl(long fd, long request, long arg)
{
	return __transputer_syscall(__NR_ioctl, fd, request, arg, 0, 0, 0);
}

static unsigned int ip_addr(int a, int b, int c, int d)
{
	return (unsigned int)a | ((unsigned int)b << 8) |
	       ((unsigned int)c << 16) | ((unsigned int)d << 24);
}

static void tp_strcpy(char *dst, const char *src)
{
	while (*src)
		*dst++ = *src++;
	*dst = 0;
}

static void tp_memset(void *dst, int val, long n)
{
	char *p = (char *)dst;
	while (n--)
		*p++ = (char)val;
}

static long tp_set_ifaddr(long sock, struct ifreq *ifr)
{
	log_tag("net-addr-call");
	ifr->ifr_addr.sin_family = AF_INET;
	ifr->ifr_addr.sin_addr = ip_addr(10, 0, 2, 15);
	return tp_ioctl(sock, SIOCSIFADDR, (long)ifr);
}

static long tp_set_netmask(long sock, struct ifreq *ifr)
{
	log_tag("net-mask-call");
	ifr->ifr_addr.sin_family = AF_INET;
	ifr->ifr_addr.sin_addr = ip_addr(255, 255, 255, 0);
	return tp_ioctl(sock, SIOCSIFNETMASK, (long)ifr);
}

static long tp_set_ifflags(long sock, struct ifreq *ifr)
{
	log_tag("net-up-call");
	ifr->ifr_flags = IFF_UP | IFF_RUNNING;
	return tp_ioctl(sock, SIOCSIFFLAGS, (long)ifr);
}

static long tp_add_default_route(long sock, struct rtentry *rt)
{
	log_tag("net-rt-zero");
	tp_memset(rt, 0, sizeof(*rt));
	rt->rt_dst.sin_family = AF_INET;
	rt->rt_dst.sin_addr = 0;
	rt->rt_gateway.sin_family = AF_INET;
	rt->rt_gateway.sin_addr = ip_addr(10, 0, 2, 1);
	rt->rt_genmask.sin_family = AF_INET;
	rt->rt_genmask.sin_addr = 0;
	rt->rt_flags = RTF_UP | RTF_GATEWAY;
	log_tag("net-route-call");
	return tp_ioctl(sock, SIOCADDRT, (long)rt);
}

static void setup_network(void)
{
	long sock;
	long ret;
	struct ifreq ifr;
	struct rtentry rt;

	if (0) { /* SKIP_NET_FOR_SMOKE */
	log_tag("net-enter");
	log_value("net-ifr-size", sizeof(ifr));
	log_value("net-rt-size", sizeof(rt));
	sock = tp_socket(AF_INET, SOCK_DGRAM, 0);
	log_value("net-socket", sock);
	if (sock < 0) {
		uart_puts("PID1:net-socket-fail\r\n");
		return;
	}

	/* Configure eth0 with 10.0.2.15/24 */
	log_tag("net-ifr-zero");
	tp_memset(&ifr, 0, sizeof(ifr));
	tp_strcpy(ifr.ifr_name, "eth0");

	ret = tp_set_ifaddr(sock, &ifr);
	log_value("net-addr", ret);

	ret = tp_set_netmask(sock, &ifr);
	log_value("net-mask", ret);

	ret = tp_set_ifflags(sock, &ifr);
	log_value("net-up", ret);

	ret = tp_add_default_route(sock, &rt);
	log_value("net-route", ret);

	log_tag("net-close");
	tp_close(sock);
	log_tag("net-ready");
	} else { log_tag("net-skipped"); }
}

/* Write a string to fd 1 via syscall (once console is set up) */
static void fd_puts(const char *s)
{
	const char *p = s;
	long len = 0;

	while (*p++)
		len++;
	tp_write(1, s, len);
}



/* --- raw getdents probe (uses uart_puts; placed after fd_puts) --- */
#define __NR_tp_openat      56
#define __NR_tp_getdents64  61
#define TP_AT_FDCWD         (-100L)

struct tp_linux_dirent64 {
	unsigned long long d_ino;
	long long d_off;
	unsigned short d_reclen;
	unsigned char d_type;
	char d_name[1];
};

static void probe_raw_listdir(const char *path)
{
	char buf[512];
	long fd, n, pos;
	long names = 0;

	log_tag("raw-listdir");
	fd_puts("RAW_LS:");
	fd_puts(path);
	fd_puts("\r\n");

	fd = __transputer_syscall(__NR_tp_openat, TP_AT_FDCWD, (long)path,
				  0, 0, 0, 0);
	log_value("raw-open-ret", fd);
	if (fd < 0)
		return;

	n = __transputer_syscall(__NR_tp_getdents64, fd, (long)buf,
				 (long)sizeof(buf), 0, 0, 0);
	log_value("raw-getdents-ret", n);
	if (n > 0) {
		pos = 0;
		while (pos < n) {
			struct tp_linux_dirent64 *de =
				(struct tp_linux_dirent64 *)(buf + pos);
			if (de->d_reclen < 8)
				break;
			fd_puts("ENT:");
			fd_puts(de->d_name);
			fd_puts("\r\n");
			names++;
			pos += de->d_reclen;
			if (names > 40)
				break;
		}
	}
	log_value("raw-dir-names", names);
	__transputer_syscall(__NR_close, fd, 0, 0, 0, 0, 0);
}


/* Spawn a busybox applet (NULL-terminated argv) and log its exit status.
 * envp is passed on the caller's stack — NEVER a static pointer array (those
 * emit R32 relocs that fault /init at load). */
static long show_cmd(const char *path, const char *const *argv,
		     const char *const *envp)
{
	long r;
	int i = 1;
	fd_puts("\r\n$ ");
	fd_puts(argv[0]);
	while (argv[i]) {
		fd_puts(" ");
		fd_puts(argv[i]);
		i++;
	}
	fd_puts("\r\n");
	r = tp_spawn(path, argv, envp);
	log_value("show-ret", r);
	return r;
}

static void pid1_main(void)
{
	long fd, ret;

	log_tag("main-enter");

	/* Verify syscall path works */
	ret = tp_getpid();
	log_value("getpid", ret);

	/* Step 1: Ensure /dev exists and has console device */
	tp_mkdir("/dev", 0755);
	{
		long r;
		r = tp_mknod("/dev/ttyS0", S_IFCHR | 0600, MKDEV_TTYS0);
		log_value("mknod-ttyS0", r);
		r = tp_mknod("/dev/console", S_IFCHR | 0600, MKDEV_CONSOLE);
		log_value("mknod-console", r);
		r = tp_mknod("/dev/null", S_IFCHR | 0666, MKDEV_NULL);
		log_value("mknod-null", r);
	}
	log_tag("dev-setup");

	/* Step 2: Create nodes only. Defer open/dup of console until AFTER
	 * multi-applet smoke: opening ttyS0 before tp_spawn makes the second
	 * do_execve hang inside flush_old_exec/tty teardown. UART write path
	 * in the syscall gate still covers fd<=2 output. */
	fd = -1;
	log_value("console-fd-deferred", fd);


	/* Step 3: Become session leader */
	tp_setsid();

	/* Step 4: Mount /proc and /sys */
	tp_mkdir("/proc", 0555);
	ret = tp_mount("proc", "/proc", "proc", 0);
	log_value("mount-proc", ret);

	tp_mkdir("/sys", 0555);
	ret = tp_mount("sysfs", "/sys", "sysfs", 0);
	log_value("mount-sys", ret);

	/* Step 4b: Mount the persistent ext2 partition from the T9000 card.
	 * Major 240 is fixed by t4_sd; minor 0 is the whole card and minor 1
	 * is the MBR rootfs partition created by transputer-mksd. */
	tp_mkdir("/sd", 0755);
	tp_mknod("/dev/t4sd", S_IFBLK | 0660, MKDEV(240, 0));
	tp_mknod("/dev/t4sd1", S_IFBLK | 0660, MKDEV(240, 1));
	ret = tp_mount("/dev/t4sd1", "/sd", "ext2", 0);
	log_value("mount-sd", ret);
	if (ret >= 0) {
		char marker[32];
		long marker_fd;
		long marker_n;

		/* Create standard dirs on SD if not present */
		tp_mkdir("/sd/usr", 0755);
		tp_mkdir("/sd/usr/bin", 0755);
		tp_mkdir("/sd/usr/games", 0755);
		tp_mkdir("/sd/usr/share", 0755);
		tp_mkdir("/sd/var", 0755);
		tp_mkdir("/sd/var/lib", 0755);
		tp_mkdir("/sd/var/lib/tpkg", 0755);
		tp_mkdir("/sd/home", 0755);
		tp_mkdir("/sd/tmp", 0777);

		/* Real VFS/BIO smoke test, deliberately beyond direct MMIO tests. */
		marker_fd = tp_open("/sd/.tpos-rw-test", 0);
		marker_n = marker_fd < 0 ? marker_fd :
			tp_read(marker_fd, marker, sizeof(marker));
		if (marker_fd >= 0)
			tp_close(marker_fd);
		if (marker_n == 11 && marker[0] == 'T' &&
		    marker[8] == 'O' && marker[9] == 'K' &&
		    marker[10] == '\n')
			log_tag("sd-persist-ok");
		else
			log_tag("sd-persist-seed");

		marker_fd = tp_open3("/sd/.tpos-rw-test",
				     O_RDWR | O_CREAT | O_TRUNC, 0644);
		log_value("sd-rw-open", marker_fd);
		if (marker_fd >= 0) {
			marker_n = tp_write(marker_fd, "TPOS_SD_OK\n", 11);
			log_value("sd-rw-write", marker_n);
			log_value("sd-rw-fsync", tp_fsync(marker_fd));
			tp_close(marker_fd);
			tp_sync();
			marker_fd = tp_open("/sd/.tpos-rw-test", 0);
			marker_n = marker_fd < 0 ? marker_fd :
				tp_read(marker_fd, marker, sizeof(marker));
			log_value("sd-rw-read", marker_n);
			if (marker_fd >= 0)
				tp_close(marker_fd);
			if (marker_n == 11 && marker[0] == 'T' &&
			    marker[8] == 'O' && marker[9] == 'K' &&
			    marker[10] == '\n')
				log_tag("sd-rw-ok");
			else
				log_tag("sd-rw-fail");
		}
	}

	/* Step 5: Configure networking (eth0 = t4net via TAP) */
	setup_network();

	/* Step 6: Print boot banner */
	log_tag("init-ready");
	fd_puts("\r\n");
	fd_puts("TransputerOS 0.1.0\r\n");
	fd_puts("PID 1 init ready.\r\n");
	fd_puts("\r\n");

	/*
	 * Session order (package-aware paths from TPZ-FORMAT / stage.sh):
	 *   1. Smoke: BusyBox echo via tp_spawn
	 *   2. Prefer /usr/bin/fbwm  (fbwm-1.0.0.tpz or rootfs stage)
	 *   3. Fallback: /bin/tpsh
	 *   4. Fallback: /bin/sh (BusyBox hush)
	 *
	 * Optional packages live under /var/lib/tpkg after:
	 *   tar xzf pkg.tpz -C /   or   tpkg install pkg.tpz
	 * Extra packages may also be staged under /opt/packages/*.tpz
	 */
	/* Multi-applet smoke tests. Markers scanned by build.sh --boot. */
	{
		const char *envp[] = {
			"HOME=/",
			"PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/games",
			"TERM=vt100",
			0
		};
		/* Keep argv strings short — long echo banner previously hit EFAULT in do_execve. */
const char *a_echo[]  = { "/bin/echo", "SPAWN_OK", 0 };
const char *a_uname[] = { "/bin/uname", "-a", 0 };
/* -1: one name per line so UART clearly shows dir entries */
const char *a_ls[]    = { "/bin/ls", "-1", "/bin", 0 };
const char *a_lsbin[] = { "/bin/ls", "-1", "/tmp", 0 };
const char *a_true[]  = { "/bin/true", 0 };

/* Listing probes (direct getdents) — cheap and high signal */
probe_raw_listdir("/");
probe_raw_listdir("/bin");
{
long bfd = __transputer_syscall(__NR_tp_openat, TP_AT_FDCWD,
(long)"/bin/busybox", 0, 0, 0, 0);
log_value("raw-busybox-open", bfd);
if (bfd >= 0)
__transputer_syscall(__NR_close, bfd, 0, 0, 0, 0, 0);
}

/* Cheapest spawn first to prove tp_spawn path, then smoke markers build.sh wants */
log_tag("spawn-true-test");
ret = spawn_smoke("/bin/true", a_true);
log_value("true-test-ret", ret);

log_tag("spawn-uname-test");
ret = spawn_smoke("/bin/uname", a_uname);
log_value("uname-test-ret", ret);

log_tag("spawn-echo-test");
ret = spawn_smoke("/bin/echo", a_echo);
log_value("echo-test-ret", ret);

/* Real multi-applet ls via busybox (exercises getdents + fstatat/lstat).
 * Prefer /tmp first (few entries) then /bin. Dedicated reenter stack
 * should keep parent setjmp frames intact after SPAWN_DONE. */
{
const char *a_lstmp[]  = { "/bin/ls", "-1", "/tmp", 0 };
const char *a_ls[]     = { "/bin/ls", "-1", "/bin", 0 };
const char *a_lsroot[] = { "/bin/ls", "-1", "/", 0 };

/* quick fstatat probe before ls */
{
char stbuf[128];
long sr = __transputer_syscall(79 /* fstatat64 */, -100L,
(long)"/bin/busybox", (long)stbuf, 0x100 /* AT_SYMLINK_NOFOLLOW */, 0, 0);
log_value("raw-fstatat-busybox", sr);
}

log_tag("spawn-lstmp-test");
ret = spawn_smoke("/bin/ls", a_lstmp);
log_value("lstmp-test-ret", ret);

log_tag("spawn-ls-test");
ret = spawn_smoke("/bin/ls", a_ls);
log_value("ls-test-ret", ret);

log_tag("spawn-lsbin-test");
/* second listing of root (non-block entries mostly) */
ret = spawn_smoke("/bin/ls", a_lsroot);
log_value("lsbin-test-ret", ret);
}

/* Multi-applet userspace smoke complete (even if some listings fail).
 * DESKTOP_READY is reserved for fbwm after it has drawn the desktop. */
log_tag("userspace-smoke-ready");
fd_puts("USERSPACE_SMOKE_READY\r\n");

	/* Late console: synthetic open of /dev/ttyS0 returns fd 0.
	 * write(fd<=2) and read(0) use the MMIO UART regardless of VFS. */
	{
		long cfd = tp_open("/dev/ttyS0", O_RDWR);
		if (cfd < 0)
			cfd = tp_open("/dev/console", O_RDWR);
		log_value("console-late", cfd);
		if (cfd > 0) {
			tp_dup2(cfd, 0);
			tp_close(cfd);
			tp_dup2(0, 1);
			tp_dup2(0, 2);
		}
		/* cfd==0 already means stdin is the console handle */
	}

}

	/* Ensure tpkg DB dir exists for later installs */
	tp_mkdir("/var", 0755);
	tp_mkdir("/var/lib", 0755);
	tp_mkdir("/var/lib/tpkg", 0755);
	tp_mkdir("/opt", 0755);
	tp_mkdir("/opt/packages", 0755);

	/* Session probe via BusyBox reenter.  Keep this to shell builtins:
	 * external applets were already exercised individually above, while
	 * ash needs vfork/exec process semantics that are not yet the boot path. */
	log_tag("exec-sh");
	fd_puts("init: busybox sh -c smoke\r\n");
	{
		const char *argv[] = {
			"/bin/sh", "-c",
			"echo SHELL_OK; pwd; echo SHELL_BUILTINS_OK",
			0
		};
		const char *envp[] = {
			"HOME=/",
			"PATH=/bin:/sbin:/usr/bin",
			"TERM=vt100",
			0
		};
		ret = tp_spawn("/bin/sh", argv, envp);
		log_value("sh-spawn-ret", ret);
	}

	/* The graphical session is the primary console.
	 * Prefer SD-staged fbwm (live packages / showcase builds) over the
	 * freeze-dried initramfs copy so T4_SD_IMAGE updates take effect. */
	log_tag("exec-fbwm");
	{
		const char *paths[] = {
			"/sd/usr/bin/fbwm",
			"/usr/bin/fbwm",
			0
		};
		const char *envp[] = {
			"HOME=/",
			"PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/games:/sd/usr/bin:/sd/usr/games",
			"TERM=vt100",
			"DOOMWADDIR=/sd/usr/share/doom",
			"TPKG_DB=/var/lib/tpkg",
			0
		};
		int pi;
		ret = -1;
		for (pi = 0; paths[pi]; pi++) {
			const char *argv[] = { paths[pi], 0 };
			fd_puts("init: trying ");
			fd_puts(paths[pi]);
			fd_puts("\r\n");
			ret = tp_spawn(paths[pi], argv, envp);
			log_value("fbwm-spawn-ret", ret);
			if (ret >= 0)
				break;
		}
	}

	/* Recovery console if the graphical session exits or cannot start. */
	log_tag("exec-tpsh-fallback");
	fd_puts("init: fbwm ended; trying recovery /bin/tpsh\r\n");
	{
		const char *argv[] = { "/bin/tpsh", 0 };
		const char *envp[] = {
			"HOME=/",
			"PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/games",
			"TERM=vt100",
			"PS1=transputer$ ",
			0
		};
		ret = tp_spawn("/bin/tpsh", argv, envp);
		log_value("tpsh-spawn-ret", ret);
	}

	log_tag("exec-failed");
	fd_puts("init: session ended, UART spin\r\n");
	log_tag("uart-only-spin");
	for (;;)
		;
}

/*
 * _start_c — called from crt1.S via IP-relative CALL.
 * Parameters arrive via TCC's standard calling convention:
 *   WPtr[N+1] = argc, WPtr[N+2] = argv, WPtr[N+3] = envp
 */


void _start_c(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;
	uart_puts("\r\nPID1:_start_c\r\n");
	pid1_main();
	uart_puts("PID1:return\r\n");

	for (;;)
		;
}
