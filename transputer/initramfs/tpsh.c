/*
 * tpsh.c — Transputer Shell
 *
 * A minimal shell for NOMMU transputer Linux that does NOT use the
 * BusyBox NOMMU re-exec trick (which is the current hush blocker).
 *
 * Built-in commands: cd, exit, echo, pwd, export
 * External commands: vfork() + execve()
 *
 * Compiles with TCC transputer cross-compiler, links against uClibc-ng.
 * No dependency on printf/vsnprintf — uses direct UART for diagnostics
 * and write(1,...) for normal output.
 */

extern long __transputer_syscall(long number, long a1, long a2, long a3,
                                 long a4, long a5, long a6);

/* ---- Syscall numbers (asm-generic/unistd.h) ---- */
#define __NR_dup3           24
#define __NR_chdir          49
#define __NR_close          57
#define __NR_read           63
#define __NR_write          64
#define __NR_exit_group     94
#define __NR_waitid         95
#define __NR_wait4          260
#define __NR_execve         221
#define __NR_getcwd        1029
#define __NR_vfork         1071
#define __NR_tp_spawn      2000
#define __NR_tp_hw_spawn   2001

/* waitid si_code values */
#define P_PID               1
#define WEXITED             4

/* ---- Constants ---- */
#define UART_ADDR       0x10000000u
#define MAX_LINE        256
#define MAX_ARGS        32
#define MAX_ENV         32
#define MAX_PATH        128

/* ---- UART direct output (diagnostics only) ---- */
static void uart_putc(unsigned int c)
{
    *(volatile unsigned int *)UART_ADDR = c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc((unsigned int)*s++);
}

/* ---- String utilities ---- */
static int tp_strlen(const char *s)
{
    int n = 0;
    while (*s++)
        n++;
    return n;
}

static int tp_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int tp_strncmp(const char *a, const char *b, int n)
{
    while (n > 0 && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0)
        return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

static void tp_strcpy(char *dst, const char *src)
{
    while (*src)
        *dst++ = *src++;
    *dst = 0;
}

static void tp_strcat(char *dst, const char *src)
{
    while (*dst)
        dst++;
    while (*src)
        *dst++ = *src++;
    *dst = 0;
}

static char *tp_strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c)
            return (char *)s;
        s++;
    }
    return (char *)0;
}

/* ---- Syscall wrappers ---- */
static long tp_write(int fd, const char *buf, long count)
{
    return __transputer_syscall(__NR_write, fd, (long)buf, count, 0, 0, 0);
}

static long tp_read(int fd, char *buf, long count)
{
    return __transputer_syscall(__NR_read, fd, (long)buf, count, 0, 0, 0);
}

static long tp_chdir(const char *path)
{
    return __transputer_syscall(__NR_chdir, (long)path, 0, 0, 0, 0, 0);
}

static long tp_getcwd(char *buf, long size)
{
    return __transputer_syscall(__NR_getcwd, (long)buf, size, 0, 0, 0, 0);
}

static long tp_execve(const char *path, const char *const *argv,
                      const char *const *envp)
{
    return __transputer_syscall(__NR_execve, (long)path, (long)argv,
                               (long)envp, 0, 0, 0);
}

static long tp_vfork(void)
{
    return __transputer_syscall(__NR_vfork, 0, 0, 0, 0, 0, 0);
}

static long tp_wait4(long pid, int *status, long options, long rusage)
{
    return __transputer_syscall(__NR_wait4, pid, (long)status,
                               options, rusage, 0, 0);
}

/*
 * tp_spawn — synchronous spawn using T800 hardware process scheduling.
 *
 * Uses __NR_tp_hw_spawn (STOPP parent, RUNP child). The parent process
 * is descheduled by the T800 hardware and the child runs on the same
 * processor. When the child exits, the kernel RUNPs the parent.
 *
 * Falls back to cooperative tp_spawn (setjmp/longjmp) if hw_spawn
 * returns -ENOSYS (older kernel without hw_spawn support).
 *
 * Returns: child's exit code (0+), or negative errno on failure.
 */
static long tp_spawn(const char *path, const char *const *argv,
                     const char *const *envp)
{
    long ret;
    /* Try hardware spawn first (true T800 process scheduling) */
    ret = __transputer_syscall(__NR_tp_hw_spawn, (long)path, (long)argv,
                               (long)envp, 0, 0, 0);
    if (ret == -38) {
        /* ENOSYS — kernel doesn't have hw_spawn, fall back */
        ret = __transputer_syscall(__NR_tp_spawn, (long)path, (long)argv,
                                   (long)envp, 0, 0, 0);
    }
    return ret;
}

static void tp_exit(int code)
{
    __transputer_syscall(__NR_exit_group, code, 0, 0, 0, 0, 0);
    for (;;) ;
}

/* ---- Output helpers ---- */
static void fd_puts(int fd, const char *s)
{
    int len = tp_strlen(s);
    if (len > 0)
        tp_write(fd, s, len);
}

static void fd_putc(int fd, char c)
{
    tp_write(fd, &c, 1);
}

/* ---- Environment ---- */
static char env_storage[MAX_ENV][MAX_LINE];
static char *env_ptrs[MAX_ENV + 1]; /* NULL-terminated */
static int env_count;

static void env_init(char **envp)
{
    int i;

    env_count = 0;
    if (!envp)
        return;

    for (i = 0; envp[i] && i < MAX_ENV; i++) {
        tp_strcpy(env_storage[i], envp[i]);
        env_ptrs[i] = env_storage[i];
        env_count++;
    }
    env_ptrs[env_count] = (char *)0;
}

/*
 * Match name against the "KEY=" prefix of an env entry.
 * Returns pointer to value (after '='), or NULL if no match.
 * Uses char-by-char comparison to avoid string literal
 * relocation issues in bFLT binaries on transputer.
 */
static char *env_match(const char *entry, const char *name)
{
    const char *e = entry;
    const char *n = name;

    while (*n) {
        if (*e != *n)
            return (char *)0;
        e++;
        n++;
    }
    if (*e == '=')
        return (char *)(e + 1);
    return (char *)0;
}

static char *env_get(const char *name)
{
    int i;
    char *val;

    for (i = 0; i < env_count; i++) {
        val = env_match(env_storage[i], name);
        if (val)
            return val;
    }
    return (char *)0;
}

/*
 * Build "NAME=VALUE" into dst buffer without using string literal "=".
 * Avoids bFLT relocation issues with string literal addresses.
 */
static void env_build(char *dst, const char *name, const char *value)
{
    while (*name)
        *dst++ = *name++;
    *dst++ = '=';
    while (*value)
        *dst++ = *value++;
    *dst = 0;
}

/*
 * Add a new env var — separate function to avoid TCC's
 * layout-sensitive codegen issues in the parent function.
 */
static void env_add_new(const char *name, const char *value)
{
    int idx = env_count;
    char *dst = env_storage[idx];
    env_build(dst, name, value);
    env_ptrs[idx] = dst;
    env_count = idx + 1;
    env_ptrs[env_count] = (char *)0;
}

static void env_set(const char *name, const char *value)
{
    int i;

    /* Update existing */
    for (i = 0; i < env_count; i++) {
        if (env_match(env_storage[i], name)) {
            env_build(env_storage[i], name, value);
            return;
        }
    }

    /* Add new */
    if (env_count < MAX_ENV)
        env_add_new(name, value);
}

/* ---- Variable expansion ---- */
/*
 * Expand $VAR and $? in a token. Returns pointer to static buffer.
 * Only handles simple $NAME (no ${NAME}, no $(...)).
 */
static int last_exit_status;

static char expand_buf[MAX_LINE];

static char *expand_vars(const char *token)
{
    char *out = expand_buf;
    char *end = expand_buf + MAX_LINE - 1;
    const char *p = token;
    char varname[64];
    char *val;
    int vi;

    while (*p && out < end) {
        if (*p == '$') {
            p++;
            if (*p == '?') {
                /* $? = last exit status */
                p++;
                /* Simple int-to-string for 0-255 */
                if (last_exit_status == 0) {
                    *out++ = '0';
                } else {
                    int v = last_exit_status;
                    char digits[12];
                    int di = 0;
                    if (v < 0) {
                        *out++ = '-';
                        v = -v;
                    }
                    while (v > 0 && di < 11) {
                        digits[di++] = '0' + (v % 10);
                        v = v / 10;
                    }
                    while (di > 0 && out < end)
                        *out++ = digits[--di];
                }
            } else {
                /* Collect variable name */
                vi = 0;
                while ((*p >= 'A' && *p <= 'Z') ||
                       (*p >= 'a' && *p <= 'z') ||
                       (*p >= '0' && *p <= '9') ||
                       *p == '_') {
                    if (vi < 63)
                        varname[vi++] = *p;
                    p++;
                }
                varname[vi] = 0;
                val = env_get(varname);
                if (val) {
                    while (*val && out < end)
                        *out++ = *val++;
                }
            }
        } else {
            *out++ = *p++;
        }
    }
    *out = 0;
    return expand_buf;
}

/* ---- Line reading ---- */
static int read_line(char *buf, int size)
{
    int pos = 0;
    char c;
    long r;

    while (pos < size - 1) {
        r = tp_read(0, &c, 1);
        if (r <= 0) {
            /* EOF */
            if (pos == 0)
                return -1;
            break;
        }
        if (c == '\n' || c == '\r')
            break;
        if (c == '\b' || c == 127) {
            /* Backspace */
            if (pos > 0) {
                pos--;
                fd_puts(1, "\b \b");
            }
            continue;
        }
        if (c == 3) {
            /* Ctrl-C: cancel line */
            fd_puts(1, "^C\r\n");
            pos = 0;
            break;
        }
        buf[pos++] = c;
        /* Echo character */
        fd_putc(1, c);
    }
    buf[pos] = 0;
    if (pos > 0)
        fd_puts(1, "\r\n");
    return pos;
}

/* ---- Command parsing ---- */
static int parse_args(char *line, char **argv, int max_args)
{
    int argc = 0;
    char *p = line;

    while (*p && argc < max_args - 1) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;

        if (*p == '"') {
            /* Quoted argument */
            p++;
            argv[argc++] = p;
            while (*p && *p != '"')
                p++;
            if (*p == '"')
                *p++ = 0;
        } else {
            /* Unquoted argument */
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t')
                p++;
            if (*p)
                *p++ = 0;
        }
    }
    argv[argc] = (char *)0;
    return argc;
}

/* ---- Built-in commands ---- */
static int builtin_cd(int argc, char **argv)
{
    const char *dir;
    long ret;

    if (argc < 2) {
        dir = env_get("HOME");
        if (!dir)
            dir = "/";
    } else {
        dir = argv[1];
    }

    ret = tp_chdir(dir);
    if (ret < 0) {
        fd_puts(2, "cd: ");
        fd_puts(2, dir);
        fd_puts(2, ": No such directory\r\n");
        return 1;
    }
    return 0;
}

static int builtin_pwd(void)
{
    char cwd[MAX_PATH];
    long ret;

    ret = tp_getcwd(cwd, MAX_PATH);
    if (ret >= 0) {
        fd_puts(1, cwd);
        fd_puts(1, "\r\n");
    } else {
        fd_puts(2, "pwd: error\r\n");
        return 1;
    }
    return 0;
}

static int builtin_echo(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (i > 1)
            fd_putc(1, ' ');
        fd_puts(1, argv[i]);
    }
    fd_puts(1, "\r\n");
    return 0;
}

static int builtin_export(int argc, char **argv)
{
    int i;
    char *eq;

    for (i = 1; i < argc; i++) {
        eq = tp_strchr(argv[i], '=');
        if (eq) {
            /* Split NAME=VALUE */
            *eq = 0;
            env_set(argv[i], eq + 1);
            *eq = '='; /* restore */
        }
        /* export without = just marks existing var; we always export all */
    }
    return 0;
}

/* ---- External command execution ---- */

/*
 * Build candidate paths for a command by searching PATH.
 * Stores up to MAX_CANDIDATES paths in candidates array.
 * Returns number of candidates found.
 */
#define MAX_CANDIDATES 8
static char candidate_bufs[MAX_CANDIDATES][MAX_PATH];

static int build_candidates(const char *cmd, char candidates[][MAX_PATH])
{
    char *path_env;
    char *p;
    char path_copy[MAX_LINE];
    int cmd_len, dir_len;
    int count = 0;

    /* If command contains '/', use it directly */
    if (tp_strchr(cmd, '/')) {
        tp_strcpy(candidates[0], cmd);
        return 1;
    }

    path_env = env_get("PATH");
    if (!path_env)
        path_env = "/bin:/sbin:/usr/bin:/usr/sbin";

    tp_strcpy(path_copy, path_env);
    cmd_len = tp_strlen(cmd);

    p = path_copy;
    while (*p && count < MAX_CANDIDATES) {
        char *dir = p;
        char *colon = tp_strchr(p, ':');
        if (colon) {
            *colon = 0;
            p = colon + 1;
        } else {
            p = p + tp_strlen(p); /* advance to end */
        }

        dir_len = tp_strlen(dir);
        if (dir_len == 0)
            continue;
        if (dir_len + 1 + cmd_len >= MAX_PATH)
            continue;

        tp_strcpy(candidates[count], dir);
        tp_strcat(candidates[count], "/");
        tp_strcat(candidates[count], cmd);
        count++;
    }

    return count;
}

static int run_external(int argc, char **argv)
{
    int ncand, i;
    long ret;

    (void)argc;
    ncand = build_candidates(argv[0], candidate_bufs);
    if (ncand == 0) {
        fd_puts(2, "tpsh: ");
        fd_puts(2, argv[0]);
        fd_puts(2, ": command not found\r\n");
        return 127;
    }

    /*
     * Try each PATH candidate using tp_spawn().
     * tp_spawn is a synchronous fork+exec+wait: the kernel loads
     * the child binary, enters it, and when the child calls exit()
     * the kernel longjmps back and returns the exit code here.
     *
     * No vfork/wait needed — it's all done in one syscall.
     */
    for (i = 0; i < ncand; i++) {
        ret = tp_spawn(candidate_bufs[i],
                       (const char *const *)argv,
                       (const char *const *)env_ptrs);

        if (ret == -2) {
            /* ENOENT — file not found, try next candidate */
            if (i + 1 < ncand)
                continue;
        }

        if (ret < 0) {
            /* Spawn failed (ENOENT, ENOEXEC, etc.) */
            if (i + 1 < ncand)
                continue;
            fd_puts(2, "tpsh: ");
            fd_puts(2, argv[0]);
            fd_puts(2, ": exec failed\r\n");
            return 127;
        }

        /* Success — ret is the child's exit code */
        return (int)ret;
    }

    fd_puts(2, "tpsh: ");
    fd_puts(2, argv[0]);
    fd_puts(2, ": command not found\r\n");
    return 127;
}

/* ---- Execute a single command line ---- */
static char line_buf[MAX_LINE];
static char *argv_buf[MAX_ARGS];

/*
 * Builtin dispatch — kept as a separate small function to avoid
 * TCC's large-function tail miscompilation on transputer.
 * Returns 1 if a builtin was handled, 0 if not.
 */
static int try_builtin(int nargs, char **av)
{
    char *cmd = av[0];

    if (cmd[0] == 'e' && cmd[1] == 'x' && cmd[2] == 'i' && cmd[3] == 't' && cmd[4] == 0) {
        int code = 0;
        if (nargs > 1) {
            char *s = av[1];
            while (*s >= '0' && *s <= '9') {
                code = code * 10 + (*s - '0');
                s++;
            }
        }
        fd_puts(1, "exit\r\n");
        tp_exit(code);
        return 1;
    }
    if (cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == 0) {
        last_exit_status = builtin_cd(nargs, av);
        return 1;
    }
    if (cmd[0] == 'p' && cmd[1] == 'w' && cmd[2] == 'd' && cmd[3] == 0) {
        last_exit_status = builtin_pwd();
        return 1;
    }
    if (cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'h' && cmd[3] == 'o' && cmd[4] == 0) {
        last_exit_status = builtin_echo(nargs, av);
        return 1;
    }
    if (cmd[0] == 'e' && cmd[1] == 'x' && cmd[2] == 'p' && cmd[3] == 'o' &&
        cmd[4] == 'r' && cmd[5] == 't' && cmd[6] == 0) {
        last_exit_status = builtin_export(nargs, av);
        return 1;
    }
    /* help */
    if (cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'l' && cmd[3] == 'p' && cmd[4] == 0) {
        fd_puts(1, "tpsh builtins: cd pwd echo export exit help\r\n");
        fd_puts(1, "operators:     ;  >  >>  <\r\n");
        fd_puts(1, "SD card apps:  doom ttop tpkg tpkg-ui fbwm\r\n");
        fd_puts(1, "network:       ping ifconfig route netstat wget\r\n");
        last_exit_status = 0;
        return 1;
    }
    return 0;
}

/* Expand variables in argv (separate function) */
static void expand_argv(int nargs, char **av)
{
    int i;
    char *expanded;

    for (i = 0; i < nargs; i++) {
        if (tp_strchr(av[i], '$')) {
            expanded = expand_vars(av[i]);
            tp_strcpy(av[i], expanded);
        }
    }
}

/*
 * Parse redirect operators from argv.
 * Scans for >, >>, < tokens and extracts filenames.
 * Returns adjusted argc with redirect tokens removed.
 */
static char *redir_out_file;
static char *redir_in_file;
static int redir_append;

static int parse_redirects(int nargs, char **av)
{
    int i, j;
    redir_out_file = (char *)0;
    redir_in_file = (char *)0;
    redir_append = 0;

    for (i = 0; i < nargs; i++) {
        if (av[i][0] == '>' && av[i][1] == '>' && av[i][2] == 0) {
            /* >> file */
            redir_append = 1;
            if (i + 1 < nargs) redir_out_file = av[i + 1];
            /* Remove >> and filename from args */
            for (j = i; j + 2 < nargs; j++) av[j] = av[j + 2];
            nargs -= 2;
            i--;
        } else if (av[i][0] == '>' && av[i][1] == 0) {
            /* > file */
            redir_append = 0;
            if (i + 1 < nargs) redir_out_file = av[i + 1];
            for (j = i; j + 2 < nargs; j++) av[j] = av[j + 2];
            nargs -= 2;
            i--;
        } else if (av[i][0] == '<' && av[i][1] == 0) {
            /* < file */
            if (i + 1 < nargs) redir_in_file = av[i + 1];
            for (j = i; j + 2 < nargs; j++) av[j] = av[j + 2];
            nargs -= 2;
            i--;
        }
    }
    if (nargs >= 0) av[nargs] = (char *)0;
    return nargs;
}

/* Apply output redirect: open file, dup2 to stdout */
static int saved_stdout = -1;

static void apply_redirects(void)
{
    if (redir_out_file) {
        int flags = 0x41;  /* O_WRONLY | O_CREAT */
        if (redir_append) flags |= 0x400; /* O_APPEND */
        else flags |= 0x200; /* O_TRUNC */
        int fd = __transputer_syscall(1024, (long)redir_out_file, flags, 0644, 0, 0, 0);
        if (fd >= 0) {
            saved_stdout = __transputer_syscall(__NR_dup3, 1, 100, 0, 0, 0, 0); /* save fd 1 to 100 */
            __transputer_syscall(__NR_dup3, fd, 1, 0, 0, 0, 0); /* redirect stdout */
            __transputer_syscall(__NR_close, fd, 0, 0, 0, 0, 0);
        }
    }
}

static void restore_redirects(void)
{
    if (saved_stdout >= 0) {
        __transputer_syscall(__NR_dup3, saved_stdout, 1, 0, 0, 0, 0);
        __transputer_syscall(__NR_close, saved_stdout, 0, 0, 0, 0, 0);
        saved_stdout = -1;
    }
}

static int exec_line(char *line)
{
    int nargs;

    if (line[0] == 0 || line[0] == '#')
        return 0;

    nargs = parse_args(line, argv_buf, MAX_ARGS);
    if (nargs == 0)
        return 0;

    expand_argv(nargs, argv_buf);
    nargs = parse_redirects(nargs, argv_buf);
    if (nargs <= 0)
        return 0;

    apply_redirects();

    if (!try_builtin(nargs, argv_buf))
        last_exit_status = run_external(nargs, argv_buf);

    restore_redirects();

    return last_exit_status;
}

/* ---- Execute semicolon-separated commands ---- */
static int exec_commands(char *cmds)
{
    char *p = cmds;
    char *start;

    while (*p) {
        /* Skip leading whitespace */
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;

        start = p;

        /* Find end of command (semicolon or end of string) */
        while (*p && *p != ';')
            p++;

        if (*p == ';')
            *p++ = 0;

        /* Copy to line_buf so parse_args can modify in place */
        tp_strcpy(line_buf, start);
        exec_line(line_buf);
    }

    return last_exit_status;
}

/*
 * Write default env vars directly byte-by-byte into env_storage.
 * This avoids ALL string literal / LDC_ABS relocation issues
 * by never passing a string literal address to any function.
 */
static void env_set_defaults(void)
{
    char *p;

    /* env_storage[0] = "HOME=/" */
    p = env_storage[0];
    *p++ = 'H'; *p++ = 'O'; *p++ = 'M'; *p++ = 'E';
    *p++ = '='; *p++ = '/'; *p = 0;
    env_ptrs[0] = env_storage[0];

    /* env_storage[1] = "PATH=/bin:/sbin:/usr/bin:/usr/sbin:/sd/usr/bin:/sd/usr/games" */
    p = env_storage[1];
    *p++ = 'P'; *p++ = 'A'; *p++ = 'T'; *p++ = 'H'; *p++ = '=';
    *p++ = '/'; *p++ = 'b'; *p++ = 'i'; *p++ = 'n'; *p++ = ':';
    *p++ = '/'; *p++ = 's'; *p++ = 'b'; *p++ = 'i'; *p++ = 'n'; *p++ = ':';
    *p++ = '/'; *p++ = 'u'; *p++ = 's'; *p++ = 'r'; *p++ = '/';
    *p++ = 'b'; *p++ = 'i'; *p++ = 'n'; *p++ = ':';
    *p++ = '/'; *p++ = 'u'; *p++ = 's'; *p++ = 'r'; *p++ = '/';
    *p++ = 's'; *p++ = 'b'; *p++ = 'i'; *p++ = 'n'; *p++ = ':';
    /* SD card paths */
    *p++ = '/'; *p++ = 's'; *p++ = 'd'; *p++ = '/';
    *p++ = 'u'; *p++ = 's'; *p++ = 'r'; *p++ = '/';
    *p++ = 'b'; *p++ = 'i'; *p++ = 'n'; *p++ = ':';
    *p++ = '/'; *p++ = 's'; *p++ = 'd'; *p++ = '/';
    *p++ = 'u'; *p++ = 's'; *p++ = 'r'; *p++ = '/';
    *p++ = 'g'; *p++ = 'a'; *p++ = 'm'; *p++ = 'e'; *p++ = 's';
    *p = 0;
    env_ptrs[1] = env_storage[1];

    /* env_storage[2] = "PS1=transputer$ " */
    p = env_storage[2];
    *p++ = 'P'; *p++ = 'S'; *p++ = '1'; *p++ = '=';
    *p++ = 't'; *p++ = 'r'; *p++ = 'a'; *p++ = 'n';
    *p++ = 's'; *p++ = 'p'; *p++ = 'u'; *p++ = 't';
    *p++ = 'e'; *p++ = 'r'; *p++ = '$'; *p++ = ' '; *p = 0;
    env_ptrs[2] = env_storage[2];

    env_count = 3;
    env_ptrs[3] = (char *)0;
}

/* ---- Shell initialization (separate function to avoid large-main TCC bug) ---- */
static void shell_init(char **envp)
{
    env_init(envp);

    /* If envp was empty, set sensible defaults */
    if (env_count == 0)
        env_set_defaults();

    last_exit_status = 0;
}

/* ---- Find -c argument (separate function) ---- */
static char *find_cmd_arg(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        if (tp_strcmp(argv[i], "-c") == 0 && i + 1 < argc)
            return argv[i + 1];
    }
    return (char *)0;
}

/* ---- Non-interactive command execution (separate function) ---- */
static char cmd_copy_buf[MAX_LINE];

static int run_command_string(char *cmd_string)
{
    tp_strcpy(cmd_copy_buf, cmd_string);
    exec_commands(cmd_copy_buf);
    return last_exit_status;
}

/* ---- Interactive shell loop (separate function) ---- */
static int shell_loop(void)
{
    char *ps1;

    uart_puts("tpsh: interactive\r\n");

    for (;;) {
        ps1 = env_get("PS1");
        if (!ps1)
            ps1 = "$ ";
        fd_puts(1, ps1);

        if (read_line(line_buf, MAX_LINE) < 0) {
            fd_puts(1, "\r\nexit\r\n");
            break;
        }

        exec_line(line_buf);
    }

    return last_exit_status;
}

/* ---- Main entry point (kept small to avoid TCC large-function bug) ---- */
int main(int argc, char **argv, char **envp)
{
    char *cmd_string;

    uart_puts("tpsh: starting\r\n");
    shell_init(envp);

    cmd_string = find_cmd_arg(argc, argv);
    if (!cmd_string)
        cmd_string = env_get("TPSH_CMD");

    fd_puts(1, "Transputer Shell (tpsh)\r\n");

    if (cmd_string)
        return run_command_string(cmd_string);

    return shell_loop();
}
