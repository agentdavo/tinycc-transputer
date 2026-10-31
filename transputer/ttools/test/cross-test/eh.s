.data
	.align 4;
_except_table_pos:
	.word 0
	.align 4;
_except_pc:
	.word 0
	.align 4;
_exception_table_list:
	.word 0
.text
_find_exception_table:
	ajw -1
	ldc _exception_table_list-LF5
	ldpi
LF5:
	ldnl 0
	stl 0
L@1:
	ldl 0
	eqc 0
	cj L@4
	j L@2
L@4:
	ldc 0
	ldl 2
	ldl 0
	ldnl 1
	ldiff
	rev
	eqc 0
	cj L@3
	ldc 0
	ldl 2
	ldl 0
	ldnl 2
	ldiff
	rev
	eqc 0
	cj L@6
L@3:
	ldl 0
	ldnl 3
	stl 0
	j L@1
L@2:
	ldc 0
	ajw +1
	ret
L@6:
	ldl 0
	ldnl 0
	ajw +1
	ret
.globl ___find_first_exception_table_match;
___find_first_exception_table_match:
	ajw -8
	ldl 9
	call _find_exception_table
	stl 0
	ldc 0
	stl 1
	ldc 0
	stl 2
	ldl 0
	eqc 0
	cj L@7
	ldc 0
	ajw +8
	ret
L@7:
	ldl 9
	ldc _except_pc-LF74
	ldpi
LF74:
	stnl 0
	ldl 0
	stl 7
L@8:
	ldc 12
	ldl 7
	add
	stl 7
	ldl 1
	adc 1
	stl 1
	ldl 7
	ldnl 2
	eqc -1
	cj L@10
	j L@9
L@10:
	ldc _except_pc-LF102
	ldpi
LF102:
	ldl 7
	ldnl 0
	stl 3
	ldnl 0
	stl 4
	ldc 0
	ldl 4
	ldl 3
	ldiff
	rev
	eqc 0
	cj L@8
	ldl 7
	ldnl 1
	stl 5
	ldc 0
	ldl 4
	ldl 5
	ldiff
	rev
	cj L@8
	ldl 2
	cj L@13
	ldc 12
	ldl 2
	prod
	ldl 0
	add
	stl 6
	ldc 0
	ldl 6
	ldnl 1
	ldl 5
	ldiff
	rev
	eqc 0
	cj L@8
	ldc 0
	ldl 3
	ldl 6
	ldnl 0
	ldiff
	rev
	cj L@13
	j L@8
L@13:
	ldl 1
	stl 2
	j L@8
L@9:
	ldl 2
	cj L@14
	ldc 12
	ldl 2
	prod
	ldl 0
	add
	ldnl 2
	ajw +8
	ret
L@14:
	ldc 0
	ajw +8
	ret
.globl _strcmp;
.globl ___throw_type_match;
___throw_type_match:
	ldl 2
	ldl 1
	call _strcmp
	ret
.globl _malloc;
.globl ___register_exceptions;
___register_exceptions:
	ajw -4
	ldc 16
	call _malloc
	stl 0
	ldl 5
	ldl 0
	stnl 0
	ldl 5
	ldnl 3
	ldl 0
	stnl 1
	ldl 5
	ldnl 4
	ldl 0
	stnl 2
	ldl 5
	ldnlp 6
	stl 1
L@15:
	ldl 1
	ldnl 0
	stl 2
	ldl 2
	eqc -1
	cj L@18
	j L@16
L@18:
	ldc 0
	ldl 2
	ldl 0
	ldnl 1
	ldiff
	rev
	cj L@19
	ldl 2
	ldl 0
	stnl 1
L@19:
	ldl 1
	ldnl 1
	stl 3
	ldc 0
	ldl 3
	ldl 0
	ldnl 2
	ldiff
	rev
	cj L@17
	ldl 3
	ldl 0
	stnl 2
L@17:
	ldl 1
	ldnlp 3
	stl 1
	j L@15
L@16:
	ldc _exception_table_list-LF259
	ldpi
LF259:
	ldnl 0
	ldl 0
	stnl 3
	ldl 0
	ldc _exception_table_list-LF264
	ldpi
LF264:
	stnl 0
	ajw +4
	ret
