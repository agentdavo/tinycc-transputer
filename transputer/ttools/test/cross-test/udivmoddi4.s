.text
___clz_tab:
	.byte '\0'
	.byte '\1'
	.byte '\2'
	.byte '\2'
	.byte '\3'
	.byte '\3'
	.byte '\3'
	.byte '\3'
	.byte '\4'
	.byte '\4'
	.byte '\4'
	.byte '\4'
	.byte '\4'
	.byte '\4'
	.byte '\4'
	.byte '\4'
	.byte '\5'
	.byte '\5'
	.byte '\5'
	.byte '\5'
	.byte '\5'
	.byte '\5'
	.byte '\5'
	.byte '\5'
	.byte '\5'
	.byte '\5'
	.byte '\5'
	.byte '\5'
	.byte '\5'
	.byte '\5'
	.byte '\5'
	.byte '\5'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\6'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\7'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
	.byte '\10'
.globl ___udivmoddi4;
___udivmoddi4:
	ajw -77
	ldl 78
	stl 0
	ldl 79
	stl 1
	ldl 80
	stl 2
	ldl 81
	stl 3
	ldl 0
	stl 6
	ldl 1
	stl 7
	ldl 2
	stl 8
	ldl 3
	stl 9
	ldl 8
	stl 12
	ldl 3
	stl 13
	ldl 6
	stl 14
	ldl 1
	stl 15
	ldl 13
	eqc 0
	cj L@1
	ldl 13
	ldl 15
	ldl 12
	ldiff
	rev
	cj L@2
	ldl 12
	stl 20
	ldl 13
	ldc 65535
	ldl 12
	ldiff
	rev
	eqc 0
	cj L@7
	ldl 13
	ldc 255
	ldl 12
	ldiff
	rev
	cj L@9
	ldc 8
	stl 21
	j L@13
L@9:
	ldc 0
	stl 21
	j L@13
L@7:
	ldl 13
	ldc 16777215
	ldl 12
	ldiff
	rev
	eqc 0
	cj L@11
	ldc 16
	stl 21
	j L@13
L@11:
	ldc 24
	stl 21
	j L@13
L@13:
	ldl 20
	ldl 21
	shr
	adc ___clz_tab-LF109
	ldpi
LF109:
	lb
	ldl 21
	add
	ldc 32
	rev
	diff
	stl 19
	j L@4
L@4:
	ldl 19
	cj L@20
	ldl 12
	ldl 19
	shl
	stl 12
	ldl 15
	ldl 19
	shl
	ldc 32
	ldl 19
	diff
	ldl 14
	rev
	shr
	or
	stl 15
	ldl 14
	ldl 19
	shl
	stl 14
L@20:
	ldl 12
	ldc 16
	shr
	stl 22
	ldc 65535
	ldl 12
	and
	stl 23
	ldc 0
	ldl 15
	ldl 22
	ldiv
	rev
	stl 26
	ldc 0
	ldl 15
	ldl 22
	ldiv
	stl 24
	ldl 23
	ldl 24
	prod
	stl 28
	ldc 65536
	ldl 26
	prod
	ldl 14
	ldc 16
	shr
	or
	stl 26
	ldc 0
	ldl 26
	ldl 28
	ldiff
	rev
	cj L@24
	ldl 24
	adc -1
	stl 24
	ldl 12
	ldl 26
	add
	stl 26
	ldc 0
	ldl 26
	ldl 12
	ldiff
	rev
	eqc 0
	cj L@24
	ldc 0
	ldl 26
	ldl 28
	ldiff
	rev
	cj L@24
	ldl 24
	adc -1
	stl 24
	ldl 12
	ldl 26
	add
	stl 26
L@24:
	ldl 26
	ldl 28
	diff
	stl 26
	ldc 0
	ldl 26
	ldl 22
	ldiv
	rev
	stl 27
	ldc 0
	ldl 26
	ldl 22
	ldiv
	stl 25
	ldl 23
	ldl 25
	prod
	stl 28
	ldc 65536
	ldl 27
	prod
	ldc 65535
	ldl 14
	and
	or
	stl 27
	ldc 0
	ldl 27
	ldl 28
	ldiff
	rev
	cj L@27
	ldl 25
	adc -1
	stl 25
	ldl 12
	ldl 27
	add
	stl 27
	ldc 0
	ldl 27
	ldl 12
	ldiff
	rev
	eqc 0
	cj L@27
	ldc 0
	ldl 27
	ldl 28
	ldiff
	rev
	cj L@27
	ldl 25
	adc -1
	stl 25
	ldl 12
	ldl 27
	add
	stl 27
L@27:
	ldc 65536
	ldl 24
	prod
	ldl 25
	or
	stl 16
	ldl 27
	ldl 28
	diff
	stl 14
	j L@22
L@22:
	ldc 0
	stl 17
	j L@31
L@2:
	ldl 12
	eqc 0
	cj L@32
	ldc 0
	ldc 1
	ldl 13
	ldiv
	stl 12
L@32:
	ldl 12
	stl 29
	ldl 13
	ldc 65535
	ldl 12
	ldiff
	rev
	eqc 0
	cj L@37
	ldl 13
	ldc 255
	ldl 12
	ldiff
	rev
	cj L@39
	ldc 8
	stl 30
	j L@43
L@39:
	ldc 0
	stl 30
	j L@43
L@37:
	ldl 13
	ldc 16777215
	ldl 12
	ldiff
	rev
	eqc 0
	cj L@41
	ldc 16
	stl 30
	j L@43
L@41:
	ldc 24
	stl 30
	j L@43
L@43:
	ldl 29
	ldl 30
	shr
	adc ___clz_tab-LF384
	ldpi
LF384:
	lb
	ldl 30
	add
	ldc 32
	rev
	diff
	stl 19
	j L@34
L@34:
	ldl 19
	eqc 0
	cj L@50
	ldl 15
	ldl 12
	diff
	stl 15
	ldc 1
	stl 17
	j L@51
L@50:
	ldc 32
	ldl 19
	diff
	stl 18
	ldl 12
	ldl 19
	shl
	stl 12
	ldl 15
	ldl 18
	shr
	stl 71
	ldl 15
	ldl 19
	shl
	stl 72
	ldl 14
	ldl 18
	shr
	stl 76
	ldl 76
	ldl 72
	or
	stl 15
	ldl 14
	ldl 19
	shl
	stl 14
	ldl 12
	ldc 16
	shr
	stl 31
	ldc 65535
	ldl 12
	and
	stl 32
	ldl 71
	stl 73
	ldc 0
	stl 74
	ldl 74
	ldl 73
	ldl 31
	ldiv
	rev
	stl 35
	ldl 71
	stl 73
	ldc 0
	stl 74
	ldl 74
	ldl 73
	ldl 31
	ldiv
	stl 33
	ldl 32
	ldl 33
	prod
	stl 37
	ldc 65536
	ldl 35
	prod
	stl 73
	ldl 15
	ldc 16
	shr
	stl 76
	ldl 76
	ldl 73
	or
	stl 35
	ldc 0
	ldl 35
	ldl 37
	ldiff
	rev
	stl 76
	ldl 76
	cj L@55
	ldl 33
	adc -1
	stl 33
	ldl 12
	ldl 35
	add
	stl 35
	ldc 0
	ldl 35
	ldl 12
	ldiff
	rev
	eqc 0
	cj L@55
	ldc 0
	ldl 35
	ldl 37
	ldiff
	rev
	cj L@55
	ldl 33
	adc -1
	stl 33
	ldl 12
	ldl 35
	add
	stl 35
L@55:
	ldl 35
	ldl 37
	diff
	stl 35
	ldc 0
	ldl 35
	ldl 31
	ldiv
	rev
	stl 36
	ldc 0
	ldl 35
	ldl 31
	ldiv
	stl 34
	ldl 32
	ldl 34
	prod
	stl 37
	ldc 65536
	ldl 36
	prod
	ldc 65535
	ldl 15
	and
	or
	stl 36
	ldc 0
	ldl 36
	ldl 37
	ldiff
	rev
	cj L@58
	ldl 34
	adc -1
	stl 34
	ldl 12
	ldl 36
	add
	stl 36
	ldc 0
	ldl 36
	ldl 12
	ldiff
	rev
	eqc 0
	cj L@58
	ldc 0
	ldl 36
	ldl 37
	ldiff
	rev
	cj L@58
	ldl 34
	adc -1
	stl 34
	ldl 12
	ldl 36
	add
	stl 36
L@58:
	ldc 65536
	ldl 33
	prod
	ldl 34
	or
	stl 17
	ldl 36
	ldl 37
	diff
	stl 15
	j L@51
L@51:
	ldl 12
	ldc 16
	shr
	stl 38
	ldc 65535
	ldl 12
	and
	stl 39
	ldc 0
	ldl 15
	ldl 38
	ldiv
	rev
	stl 42
	ldc 0
	ldl 15
	ldl 38
	ldiv
	stl 40
	ldl 39
	ldl 40
	prod
	stl 44
	ldc 65536
	ldl 42
	prod
	ldl 14
	ldc 16
	shr
	or
	stl 42
	ldc 0
	ldl 42
	ldl 44
	ldiff
	rev
	cj L@65
	ldl 40
	adc -1
	stl 40
	ldl 12
	ldl 42
	add
	stl 42
	ldc 0
	ldl 42
	ldl 12
	ldiff
	rev
	eqc 0
	cj L@65
	ldc 0
	ldl 42
	ldl 44
	ldiff
	rev
	cj L@65
	ldl 40
	adc -1
	stl 40
	ldl 12
	ldl 42
	add
	stl 42
L@65:
	ldl 42
	ldl 44
	diff
	stl 42
	ldc 0
	ldl 42
	ldl 38
	ldiv
	rev
	stl 43
	ldc 0
	ldl 42
	ldl 38
	ldiv
	stl 41
	ldl 39
	ldl 41
	prod
	stl 44
	ldc 65536
	ldl 43
	prod
	ldc 65535
	ldl 14
	and
	or
	stl 43
	ldc 0
	ldl 43
	ldl 44
	ldiff
	rev
	cj L@68
	ldl 41
	adc -1
	stl 41
	ldl 12
	ldl 43
	add
	stl 43
	ldc 0
	ldl 43
	ldl 12
	ldiff
	rev
	eqc 0
	cj L@68
	ldc 0
	ldl 43
	ldl 44
	ldiff
	rev
	cj L@68
	ldl 41
	adc -1
	stl 41
	ldl 12
	ldl 43
	add
	stl 43
L@68:
	ldc 65536
	ldl 40
	prod
	ldl 41
	or
	stl 16
	ldl 43
	ldl 44
	diff
	stl 14
	j L@31
L@31:
	ldl 82
	cj L@73
	ldl 14
	ldl 19
	shr
	stl 10
	ldc 0
	stl 11
	ldl 10
	ldl 82
	stnl 0
	ldl 11
	ldl 82
	stnl 1
	j L@73
L@1:
	ldc 0
	ldl 15
	ldl 13
	ldiff
	rev
	stl 71
	ldl 71
	cj L@74
	ldc 0
	stl 16
	ldc 0
	stl 17
	ldl 82
	cj L@73
	ldl 14
	stl 10
	ldl 15
	stl 11
	ldl 10
	ldl 82
	stnl 0
	ldl 15
	ldl 82
	stnl 1
	j L@73
L@74:
	ldl 13
	stl 46
	ldc 0
	ldc 65535
	ldl 13
	ldiff
	rev
	eqc 0
	cj L@81
	ldc 0
	ldc 255
	ldl 13
	ldiff
	rev
	cj L@83
	ldc 8
	stl 47
	j L@87
L@83:
	ldc 0
	stl 47
	j L@87
L@81:
	ldc 0
	ldc 16777215
	ldl 13
	ldiff
	rev
	eqc 0
	cj L@85
	ldc 16
	stl 47
	j L@87
L@85:
	ldc 24
	stl 47
	j L@87
L@87:
	ldl 46
	ldl 47
	shr
	adc ___clz_tab-LF891
	ldpi
LF891:
	lb
	ldl 47
	add
	ldc 32
	rev
	diff
	stl 19
	j L@78
L@78:
	ldl 19
	eqc 0
	cj L@94
	ldl 19
	ldl 13
	ldl 15
	ldiff
	rev
	stl 48
	ldl 19
	ldl 14
	ldl 12
	ldiff
	rev
	stl 71
	ldl 71
	eqc 0
	stl 71
	ldl 71
	ldl 48
	or
	stl 71
	ldl 71
	cj L@95
	ldc 1
	stl 16
	ldl 14
	ldl 12
	diff
	stl 50
	ldl 15
	ldl 13
	diff
	stl 76
	ldl 19
	ldl 14
	ldl 50
	ldiff
	rev
	stl 73
	ldl 76
	ldl 73
	diff
	stl 15
	ldl 50
	stl 14
	j L@100
L@95:
	ldc 0
	stl 16
L@100:
	ldc 0
	stl 17
	ldl 82
	cj L@73
	ldl 14
	stl 10
	ldl 15
	stl 11
	ldl 10
	ldl 82
	stnl 0
	ldl 15
	ldl 82
	stnl 1
	j L@73
L@94:
	ldc 32
	ldl 19
	diff
	stl 18
	ldl 13
	ldl 19
	shl
	stl 73
	ldl 12
	ldl 18
	shr
	stl 76
	ldl 76
	ldl 73
	or
	stl 13
	ldl 12
	ldl 19
	shl
	stl 12
	ldl 15
	ldl 18
	shr
	stl 71
	ldl 15
	ldl 19
	shl
	stl 73
	ldl 14
	ldl 18
	shr
	stl 76
	ldl 76
	ldl 73
	or
	stl 15
	ldl 14
	ldl 19
	shl
	stl 14
	ldl 13
	ldc 16
	shr
	stl 53
	ldc 65535
	ldl 13
	and
	stl 54
	ldl 71
	stl 73
	ldc 0
	stl 74
	ldl 74
	ldl 73
	ldl 53
	ldiv
	rev
	stl 57
	ldl 71
	stl 73
	ldc 0
	stl 74
	ldl 74
	ldl 73
	ldl 53
	ldiv
	stl 55
	ldl 54
	ldl 55
	prod
	stl 59
	ldc 65536
	ldl 57
	prod
	stl 73
	ldl 15
	ldc 16
	shr
	stl 76
	ldl 76
	ldl 73
	or
	stl 57
	ldc 0
	ldl 57
	ldl 59
	ldiff
	rev
	stl 76
	ldl 76
	cj L@106
	ldl 55
	adc -1
	stl 55
	ldl 13
	ldl 57
	add
	stl 57
	ldc 0
	ldl 57
	ldl 13
	ldiff
	rev
	eqc 0
	cj L@106
	ldc 0
	ldl 57
	ldl 59
	ldiff
	rev
	cj L@106
	ldl 55
	adc -1
	stl 55
	ldl 13
	ldl 57
	add
	stl 57
L@106:
	ldl 57
	ldl 59
	diff
	stl 57
	ldc 0
	ldl 57
	ldl 53
	ldiv
	rev
	stl 58
	ldc 0
	ldl 57
	ldl 53
	ldiv
	stl 56
	ldl 54
	ldl 56
	prod
	stl 59
	ldc 65536
	ldl 58
	prod
	ldc 65535
	ldl 15
	and
	or
	stl 58
	ldc 0
	ldl 58
	ldl 59
	ldiff
	rev
	cj L@109
	ldl 56
	adc -1
	stl 56
	ldl 13
	ldl 58
	add
	stl 58
	ldc 0
	ldl 58
	ldl 13
	ldiff
	rev
	eqc 0
	cj L@109
	ldc 0
	ldl 58
	ldl 59
	ldiff
	rev
	cj L@109
	ldl 56
	adc -1
	stl 56
	ldl 13
	ldl 58
	add
	stl 58
L@109:
	ldc 65536
	ldl 55
	prod
	ldl 56
	or
	stl 16
	ldl 58
	ldl 59
	diff
	stl 15
	j L@104
L@104:
	ldc 65535
	ldl 16
	and
	stl 66
	ldl 16
	ldc 16
	shr
	stl 64
	ldc 65535
	ldl 12
	and
	stl 67
	ldl 12
	ldc 16
	shr
	stl 65
	ldl 67
	ldl 66
	prod
	stl 60
	ldl 65
	ldl 66
	prod
	stl 61
	ldl 67
	ldl 64
	prod
	stl 62
	ldl 65
	ldl 64
	prod
	stl 63
	ldl 60
	ldc 16
	shr
	ldl 61
	add
	stl 61
	ldl 62
	ldl 61
	add
	stl 61
	ldc 0
	ldl 61
	ldl 62
	ldiff
	rev
	cj L@116
	ldl 63
	ldnlp 16384
	stl 63
L@116:
	ldl 61
	ldc 16
	shr
	ldl 63
	add
	stl 51
	ldc 65535
	ldl 61
	and
	ldc 65536
	prod
	ldc 65535
	ldl 60
	and
	add
	stl 52
	j L@114
L@114:
	ldc 0
	ldl 15
	ldl 51
	ldiff
	rev
	eqc 0
	cj L@119
	ldl 51
	ldl 15
	diff
	stl 76
	ldl 76
	eqc 0
	stl 76
	ldc 0
	ldl 14
	ldl 52
	ldiff
	rev
	stl 73
	ldl 73
	ldl 76
	and
	stl 76
	ldl 76
	eqc 0
	cj L@119
	j L@118
L@119:
	ldl 16
	adc -1
	stl 16
	ldl 52
	ldl 12
	diff
	stl 68
	ldl 51
	ldl 13
	diff
	stl 71
	ldc 0
	ldl 52
	ldl 68
	ldiff
	rev
	stl 76
	ldl 71
	ldl 76
	diff
	stl 51
	ldl 68
	stl 52
	j L@118
L@118:
	ldc 0
	stl 17
	ldl 82
	cj L@73
	ldl 14
	ldl 52
	diff
	stl 69
	ldl 15
	ldl 51
	diff
	stl 76
	ldl 17
	ldl 14
	ldl 69
	ldiff
	rev
	stl 73
	ldl 76
	ldl 73
	diff
	stl 15
	ldl 69
	stl 14
	j L@126
L@126:
	ldl 15
	ldl 18
	shl
	stl 71
	ldl 14
	ldl 19
	shr
	stl 70
	ldl 70
	ldl 71
	or
	stl 10
	ldl 15
	ldl 19
	shr
	stl 75
	ldl 75
	stl 11
	ldl 10
	ldl 82
	stnl 0
	ldl 75
	ldl 82
	stnl 1
L@73:
	ldl 16
	stl 4
	ldl 17
	stl 5
	ldl 17
	ldl 4
	ajw +77
	ret
