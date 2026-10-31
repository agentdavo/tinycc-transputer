.globl _sdiv_qrnnd;
.globl _sdiv_qrnnd;
.globl _sdiv_qrnnd;
.globl _sdiv_qrnnd;
.text
.globl ___udiv_w_sdiv;
___udiv_w_sdiv:
	ajw -10
	ldc 0
	ldl 14
	gt
	eqc 0
	cj L@1
	ldl 14
	ldl 12
	diff
	ldl 13
	ldc 31
	shr
	diff
	ldc 0
	rev
	ldl 12
	rev
	ldiff
	rev
	cj L@2
	ldl 14
	stl 1
	ldl 13
	stl 0
	ldl 12
	ldl 3
	ldl 2
	call _sdiv_qrnnd
	j L@8
L@2:
	ldl 14
	ldc 31
	shl
	ldl 13
	rev
	diff
	stl 7
	ldl 14
	ldc 1
	shr
	ldl 12
	rev
	diff
	stl 9
	ldc 0
	ldl 13
	ldl 7
	ldiff
	rev
	ldl 9
	rev
	diff
	stl 5
	ldl 7
	stl 4
	j L@5
L@5:
	ldl 14
	stl 1
	ldl 4
	stl 0
	ldl 5
	ldl 3
	ldl 2
	call _sdiv_qrnnd
	ldl 2
	ldnlp -536870912
	stl 2
	j L@8
L@1:
	ldl 14
	ldc 1
	shr
	stl 6
	ldl 12
	ldc 1
	shr
	stl 5
	ldl 12
	ldc 31
	shl
	ldl 13
	ldc 1
	shr
	add
	stl 4
	ldc 0
	ldl 12
	ldl 6
	ldiff
	rev
	cj L@9
	ldl 6
	stl 1
	ldl 4
	stl 0
	ldl 5
	ldl 3
	ldl 2
	call _sdiv_qrnnd
	ldc 2
	ldl 3
	prod
	ldc 1
	ldl 13
	and
	add
	stl 3
	ldc 1
	ldl 14
	and
	cj L@8
	ldc 0
	ldl 3
	ldl 2
	ldiff
	rev
	eqc 0
	cj L@11
	ldl 3
	ldl 2
	diff
	stl 3
	j L@8
L@11:
	ldl 2
	ldl 3
	diff
	ldc 0
	rev
	ldl 14
	rev
	ldiff
	rev
	eqc 0
	cj L@13
	ldl 3
	ldl 2
	diff
	ldl 14
	add
	stl 3
	ldl 2
	adc -1
	stl 2
	j L@8
L@13:
	ldl 3
	ldl 2
	diff
	ldc 2
	ldl 14
	prod
	add
	stl 3
	ldl 2
	adc -2
	stl 2
	j L@8
L@9:
	ldc 0
	ldl 5
	ldl 6
	ldiff
	rev
	cj L@16
	ldl 6
	ldl 5
	adc 1
	diff
	stl 5
	ldl 4
	not
	stl 4
	ldl 6
	stl 1
	ldl 4
	stl 0
	ldl 5
	ldl 3
	ldl 2
	call _sdiv_qrnnd
	ldl 2
	not
	stl 2
	ldl 6
	ldl 3
	adc 1
	diff
	stl 3
	ldc 2
	ldl 3
	prod
	ldc 1
	ldl 13
	and
	add
	stl 3
	ldc 1
	ldl 14
	and
	cj L@8
	ldc 0
	ldl 3
	ldl 2
	ldiff
	rev
	eqc 0
	cj L@18
	ldl 3
	ldl 2
	diff
	stl 3
	j L@8
L@18:
	ldl 2
	ldl 3
	diff
	ldc 0
	rev
	ldl 14
	rev
	ldiff
	rev
	eqc 0
	cj L@20
	ldl 3
	ldl 2
	diff
	ldl 14
	add
	stl 3
	ldl 2
	adc -1
	stl 2
	j L@8
L@20:
	ldl 3
	ldl 2
	diff
	ldc 2
	ldl 14
	prod
	add
	stl 3
	ldl 2
	adc -2
	stl 2
	j L@8
L@16:
	ldc 0
	ldl 13
	ldl 14
	not
	adc 1
	ldiff
	rev
	eqc 0
	cj L@23
	ldc -1
	stl 2
	ldl 14
	ldl 13
	add
	stl 3
	j L@8
L@23:
	ldc -2
	stl 2
	ldc 2
	ldl 14
	prod
	ldl 13
	add
	stl 3
L@8:
	ldl 3
	ldl 11
	stnl 0
	ldl 2
	ajw +10
	ret
