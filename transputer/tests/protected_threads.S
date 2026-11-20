/* The parent returns from clone normally. The child starts with a fresh
 * workspace and must call its entry point before attempting a return. */
.section .text,"ax"
.global tp_thread_clone
tp_thread_clone:
	ldl 1
	ldc 220
	.byte 0x60, 0xf8	/* syscall */
	stl 3
	ldl 3
	cj .Lchild
	ldl 3
	ret
.Lchild:
	call tp_thread_child
	stopp

/* Keep a double live in FA and initialize the block-move state before
 * letting the other thread overwrite both sets of live registers. */
.global tp_thread_probe
tp_thread_probe:
	ldl 1
	fpldnldb
	ldl 1
	ldnl 4		/* source stride */
	ldl 1
	ldnl 5		/* destination stride */
	ldl 1
	ldnl 6		/* row count */
	move2dinit
	ldl 1
	call tp_thread_switch
	ldl 1
	ldnlp 2
	fpstnldb
	ldl 1
	ldnlp 7		/* source */
	ldl 1
	ldnlp 19	/* destination */
	ldc 4
	move2dall
	ret
