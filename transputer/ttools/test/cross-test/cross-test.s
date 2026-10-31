.globl ___main;
.globl _foo;
.globl _dfoo;
.globl _discard;
.globl _ddiscard;
.text
	.align 4;
LC@0:
	.word 0x0, 0x0	// double 0
	.align 4;
LC@1:
	.word 0x0	// float 0
.globl _main;
_main:
	ajw -18
	call ___main
	call _foo
	stl 2
	call _foo
	stl 3
	call _foo
	stl 4
	call _foo
	stl 5
	call _dfoo
	fpur64tor32
	ldlp 6
	fpstnlsn
	call _dfoo
	fpur64tor32
	ldlp 7
	fpstnlsn
	call _dfoo
	ldlp 8
	fpstnldb
	call _dfoo
	ldlp 10
	fpstnldb
	ldl 3
	ldl 2
	prod
	call _discard
	ldl 2
	ldl 3
	div
	call _discard
	ldl 2
	ldl 3
	rem
	call _discard
	ldc 0
	ldl 4
	ldl 5
	ldiv
	call _discard
	ldc 0
	ldl 4
	ldl 5
	ldiv
	rev
	call _discard
	ldl 2
	xdble
	ldl 3
	lshr
	call _discard
	ldl 2
	ldl 3
	shl
	call _discard
	ldl 4
	ldl 5
	shr
	call _discard
	ldl 4
	ldl 5
	shl
	call _discard
	ldlp 8
	fpldnldb
	ldlp 10
	fpldnldb
	fpadd
	ldlp 0
	fpstnldb
	ldl 1
	ldl 0
	call _ddiscard
	ldlp 8
	fpldnldb
	ldlp 10
	fpldnldb
	fpsub
	ldlp 0
	fpstnldb
	ldl 1
	ldl 0
	call _ddiscard
	ldlp 8
	fpldnldb
	ldlp 10
	fpldnldb
	fpmul
	ldlp 0
	fpstnldb
	ldl 1
	ldl 0
	call _ddiscard
	ldlp 8
	fpldnldb
	ldlp 10
	fpldnldb
	fpdiv
	ldlp 0
	fpstnldb
	ldl 1
	ldl 0
	call _ddiscard
	fpldzerodb
	ldlp 8
	fpldnldb
	fpsub
	ldlp 0
	fpstnldb
	ldl 1
	ldl 0
	call _ddiscard
	ldlp 6
	fpldnlsn
	ldlp 7
	fpldnlsn
	fpadd
	fpur32tor64
	ldlp 0
	fpstnldb
	ldl 1
	ldl 0
	call _ddiscard
	ldlp 6
	fpldnlsn
	ldlp 7
	fpldnlsn
	fpsub
	fpur32tor64
	ldlp 0
	fpstnldb
	ldl 1
	ldl 0
	call _ddiscard
	ldlp 6
	fpldnlsn
	ldlp 7
	fpldnlsn
	fpmul
	fpur32tor64
	ldlp 0
	fpstnldb
	ldl 1
	ldl 0
	call _ddiscard
	ldlp 6
	fpldnlsn
	ldlp 7
	fpldnlsn
	fpdiv
	fpur32tor64
	ldlp 0
	fpstnldb
	ldl 1
	ldl 0
	call _ddiscard
	fpldzerosn
	ldlp 6
	fpldnlsn
	fpsub
	fpur32tor64
	ldlp 0
	fpstnldb
	ldl 1
	ldl 0
	call _ddiscard
	ldlp 8
	fpldnldb
	fpurz
	fpint
	ldlp 0
	fpstnli32
	ldl 0
	call _discard
	ldlp 6
	fpldnlsn
	fpurz
	fpint
	ldlp 0
	fpstnli32
	ldl 0
	call _discard
	ldl 2
	stl 0
	ldlp 0
	fpi32tor64
	ldlp 0
	fpstnldb
	ldl 1
	ldl 0
	call _ddiscard
	ldl 2
	stl 0
	ldlp 0
	fpi32tor32
	fpur32tor64
	ldlp 0
	fpstnldb
	ldl 1
	ldl 0
	call _ddiscard
	ldlp 8
	fpldnldb
	fpur64tor32
	fpur32tor64
	ldlp 0
	fpstnldb
	ldl 1
	ldl 0
	call _ddiscard
	ldlp 8
	fpldnldb
	ldlp 10
	fpldnldb
	fpeq
	stl 12
	ldl 12
	call _discard
	ldlp 10
	fpldnldb
	ldlp 8
	fpldnldb
	fpgt
	stl 13
	ldl 13
	call _discard
	ldlp 8
	fpldnldb
	ldlp 10
	fpldnldb
	fpgt
	stl 14
	ldl 14
	call _discard
	ldl 12
	eqc 0
	call _discard
	ldl 14
	eqc 0
	call _discard
	ldl 13
	eqc 0
	call _discard
	ldlp 6
	fpldnlsn
	ldlp 7
	fpldnlsn
	fpeq
	stl 15
	ldl 15
	call _discard
	ldlp 7
	fpldnlsn
	ldlp 6
	fpldnlsn
	fpgt
	stl 16
	ldl 16
	call _discard
	ldlp 6
	fpldnlsn
	ldlp 7
	fpldnlsn
	fpgt
	stl 17
	ldl 17
	call _discard
	ldl 15
	eqc 0
	call _discard
	ldl 17
	eqc 0
	call _discard
	ldl 16
	eqc 0
	call _discard
	ldc 0
	ajw +18
	ret
.globl _discard;
_discard:
	ret
.globl _ddiscard;
_ddiscard:
	ret
.data
	.align 4;
_table@@8:
	.word 20
	.word 69
	.word 4
	.word 12
.bss
_idx@@9:
	.align 4
	.byte ?[4]
.text
.globl _foo;
_foo:
	ldc _idx@@9-LF343
	ldpi
LF343:
	ldnl 0
	dup
	adc 1
	ldc _idx@@9-LF347
	ldpi
LF347:
	stnl 0
	ldc _table@@8-LF349
	ldpi
LF349:
	wsub
	ldnl 0
	ret
.data
	.align 4;
_table@@12:
	.word 0x66666666, 0x40346666	// double 20.399999999999998579
	.word 0xa3d70a3d, 0x40517d70	// double 69.959999999999993747
	.word 0x9999999a, 0x40119999	// double 4.4000000000000003553
	.word 0xc8b43958, 0x40694676	// double 202.20199999999999818
.bss
_idx@@13:
	.align 4
	.byte ?[4]
.text
.globl _dfoo;
_dfoo:
	ldc _idx@@13-LF362
	ldpi
LF362:
	ldnl 0
	dup
	adc 1
	ldc _idx@@13-LF366
	ldpi
LF366:
	stnl 0
	ldc _table@@12-LF368
	ldpi
LF368:
	fpldnldbi
	ret
