.text
.globl ___muldi3;
___muldi3:
	ajw -24
	ldl 25
	stl 22
	ldl 26
	stl 23
	ldl 27
	stl 0
	ldl 28
	stl 1
	ldl 22
	stl 4
	ldl 23
	stl 5
	ldl 0
	stl 6
	ldl 1
	stl 7
	ldl 4
	stl 16
	ldc 65535
	ldl 16
	and
	stl 17
	ldl 16
	ldc 16
	shr
	stl 14
	ldl 6
	stl 18
	ldc 65535
	ldl 18
	and
	stl 19
	ldl 18
	ldc 16
	shr
	stl 15
	ldl 19
	ldl 17
	prod
	stl 10
	ldl 15
	ldl 17
	prod
	stl 11
	ldl 19
	ldl 14
	prod
	stl 12
	ldl 15
	ldl 14
	prod
	stl 13
	ldl 10
	ldc 16
	shr
	ldl 11
	add
	stl 11
	ldl 12
	ldl 11
	add
	stl 11
	ldc 0
	ldl 11
	ldl 12
	ldiff
	rev
	cj L@4
	ldl 13
	ldnlp 16384
	stl 13
L@4:
	ldl 11
	ldc 16
	shr
	ldl 13
	add
	stl 9
	ldc 65535
	ldl 11
	and
	ldc 65536
	prod
	ldc 65535
	ldl 10
	and
	or
	stl 8
	j L@2
L@2:
	ldl 8
	stl 2
	ldl 9
	stl 3
	ldl 7
	ldl 4
	prod
	stl 20
	ldl 6
	ldl 5
	prod
	stl 21
	ldl 21
	ldl 20
	add
	stl 21
	ldl 3
	ldl 21
	add
	stl 21
	ldl 21
	stl 3
	ldl 21
	ldl 2
	ajw +24
	ret
