# Static pthreads on TransputerOS

This is an opt-in pthread library for single-CPU, protected T9000 bFLT applications. It provides joinable threads with the default attributes, normal mutexes, and untimed condition variables. Each worker uses a 32 KiB mapping for its descriptor and workspace. `pthread_join` waits for the kernel's exit notification before releasing that mapping.

Use Linux 7 commit `4e9c0c158b9f` and uClibc-ng commit `9432c09`, or descendants of those commits.

Build uClibc-ng with `-D__TPOS_PTHREAD__` in `UCLIBC_EXTRA_CFLAGS` for thread-local `errno` and internal malloc and stdio locking. Use the same define when compiling the application and any objects using uClibc's `FILE` layout. Build the library with `bash transputer/pthread/build.sh OUTPUT_DIRECTORY`, using `TCC` and `UCLIBC` to select the target compiler and libc. Add `transputer/pthread` to the include path and link the resulting `libpthread.a` before `libc.a`. Build the syscall stub with `TP_USE_T9K_SYSCALL=1`.

Place mutexes and condition variables in writable storage. For file-scope locks in the current single-segment bFLT build, use zero-initialized storage and call `pthread_mutex_init` or `pthread_cond_init` at runtime. The converter can place explicit static initializers inside the image's protected text boundary.

The profile protects the allocator's shared heaps, individual streams, and stdio's open-file list. Use `flockfile` for a group of operations that must stay together, and when accessing a shared stream through `*_unlocked` functions. Other libc services may have their own synchronization requirements. The pthread subset covers the APIs declared in `pthread.h`.

`transputer/tests/static_pthread.c` exercises contention, condition wait and wake, return values, and separate libc `errno`. Define `TP_TEST_LIBC_STRESS` and link `transputer/tests/threaded_libc.c` to add the three-worker allocator and shared-stream test. For the CoreMark bFLT build, set `COREMARK_THREADS=2`, select this uClibc variant, and point `TP_PTHREAD_LIB` at the archive. The existing CoreMark build defaults to one context.

With Linux commit `1ab3352de352` and T4 emulator commit `1684545` or later, define `TP_TEST_EVENT_TIME` and link `transputer/tests/protected_event_time.c` to exercise epoll and POSIX timers from uClibc. The test consumes timer notifications through `sigtimedwait` and through a `SA_SIGINFO` handler during both an epoll wait and a loop without syscalls. The clock is serviced at syscall, idle and timeslice boundaries. The emulator takes protected timeslice traps at `j` and `lend` scheduling points. Signal handlers enter on the current user workspace. Relink applications that install protected handlers with the updated syscall stub, which supplies the `rt_sigreturn` trampoline.

Two protected workers use `SIGEV_THREAD_ID` timers and the same signal number. The test verifies the recipient thread and timer ID on each delivery and requires their delivery sequences to overlap. Each worker checks the integer stack, mixed-precision FP stack and rounding mode, and all three MOVE2D setup registers across 16 deliveries. The handlers write distinct state and check it across `clock_gettime`. Each worker then uses its restored MOVE2D setup for a strided copy and checks both the copied bytes and the destination padding.

Two protected threads each use targeted timers in the nested test. Each outer handler arms a `SIGUSR2` one-shot timer inside a `SIGUSR1` timer handler. The two original loops and all four handlers have distinct FP and MOVE2D state. The handlers check their receiving thread IDs. The test checks the saved integer registers and signal masks at each handler entry, then checks the FP state, MOVE2D settings and signal mask after each `rt_sigreturn`.
