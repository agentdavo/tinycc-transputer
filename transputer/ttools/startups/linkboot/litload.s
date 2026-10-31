.globl _boot_link_in;
.globl _lit_load_segsize;
.head2
	.align 4;
_lit_load_segsize:
	.word 0
.globl _boot_link_out;
.head2
.globl _lit_load_segment;
_lit_load_segment:
	ajw -27
	ldc 0
	ldc _lit_load_segsize-LF9
	ldpi
LF9:
	stnl 0
L@3:
	call _lit_load_in_number
	stl 2
	ldc 0
	ldc -1
	ldl 2
	ldnlp -536870912
	ldiff
	rev
	cj L@6
	ldc _boot_link_out-LF27
	ldpi
LF27:
	ldnl 0
	stl 19
	ldl 19
	ldc -1431699386
	outword
	stopp
L@6:
	ldl 2
	ldc 0
	gt
	cj L@7
	ldl 2
	ldl 29
	gt
	cj L@8
	ldc _boot_link_out-LF45
	ldpi
LF45:
	ldnl 0
	stl 20
	ldl 20
	ldc -1431699379
	outword
	stopp
L@8:
	ldl 28
	ldc _boot_link_in-LF55
	ldpi
LF55:
	ldnl 0
	ldl 2
	in
	ldl 2
	ldl 28
	add
	stl 28
	ldl 29
	ldl 2
	diff
	stl 29
	ldc _lit_load_segsize-LF69
	ldpi
LF69:
	ldnl 0
	ldl 2
	add
	ldc _lit_load_segsize-LF73
	ldpi
LF73:
	stnl 0
	j L@3
L@7:
	ldl 2
	ldnlp 2
	stl 18
	ldc 0
	ldc 8
	ldl 18
	ldiff
	rev
	eqc 0
	cj L@51
	ldl 18
	ldc L@52-LF89
	ldpi
LF89:
	wsub
	ldnl 0
	adc L@52-LF92
	ldpi
LF92:
	gcall
	.align 4;
L@52:
	.word L@41-L@52
	.word L@51-L@52
	.word L@51-L@52
	.word L@47-L@52
	.word L@51-L@52
	.word L@51-L@52
	.word L@39-L@52
	.word L@13-L@52
	.word L@3-L@52
L@13:
	call _lit_load_in_number
	stl 4
	ldl 31
	ldl 30
	call _lit_load_in_expr
	stl 5
	ldl 31
	ldl 30
	call _lit_load_in_expr
	stl 6
	ldl 31
	ldl 30
	call _lit_load_in_expr
	stl 7
	ldc 0
	ldl 6
	gt
	cj L@14
	ldc _boot_link_out-LF128
	ldpi
LF128:
	ldnl 0
	stl 21
	ldl 21
	ldc -1431699356
	outword
	stopp
L@14:
	ldl 4
	ldl 6
	prod
	ldl 7
	gt
	cj L@15
	ldc _boot_link_out-LF143
	ldpi
LF143:
	ldnl 0
	stl 22
	ldl 22
	ldc -1431699348
	outword
	stopp
L@15:
	ldl 7
	ldl 29
	gt
	cj L@16
	ldc _boot_link_out-LF156
	ldpi
LF156:
	ldnl 0
	stl 23
	ldl 23
	ldc -1431699346
	outword
	stopp
L@16:
	ldl 4
	eqc 2
	eqc 0
	cj L@23
	ldl 4
	ldc 2
	gt
	eqc 0
	cj L@35
	ldl 4
	eqc 1
	eqc 0
	cj L@28
	j L@33
L@35:
	ldl 4
	eqc 4
	cj L@33
	ldc 0
	stl 3
	ldl 5
	xdble
	stl 9
	stl 10
	ldl 10
	ldl 9
	ldc 8
	lshr
	stl 8
L@19:
	ldl 6
	ldl 3
	gt
	cj L@17
	ldl 5
	ldl 28
	sb
	ldl 28
	adc 1
	stl 28
	ldl 8
	ldl 28
	sb
	ldl 28
	adc 1
	stl 28
	ldl 10
	ldl 9
	ldc 16
	lshr
	ldl 28
	sb
	ldl 28
	adc 1
	stl 28
	ldl 10
	ldl 9
	ldc 24
	lshr
	ldl 28
	sb
	ldl 28
	adc 1
	stl 28
	ldl 3
	adc 1
	stl 3
	j L@19
L@23:
	ldc 0
	stl 3
	ldl 5
	xdble
	ldc 8
	lshr
	stl 11
L@24:
	ldl 6
	ldl 3
	gt
	cj L@17
	ldl 5
	ldl 28
	sb
	ldl 28
	adc 1
	stl 28
	ldl 11
	ldl 28
	sb
	ldl 28
	adc 1
	stl 28
	ldl 3
	adc 1
	stl 3
	j L@24
L@28:
	ldc 0
	stl 3
L@29:
	ldl 6
	ldl 3
	gt
	cj L@17
	ldl 5
	ldl 28
	sb
	ldl 28
	adc 1
	stl 28
	ldl 3
	adc 1
	stl 3
	j L@29
L@33:
	ldc _boot_link_out-LF307
	ldpi
LF307:
	ldnl 0
	stl 24
	ldl 24
	ldc -1431699322
	outword
	stopp
L@17:
	ldl 6
	ldl 4
	prod
	ldl 7
	rev
	diff
	stl 3
L@36:
	ldl 3
	adc -1
	stl 3
	ldl 3
	eqc -1
	eqc 0
	cj L@37
	ldl 5
	ldl 28
	sb
	ldl 28
	adc 1
	stl 28
	j L@36
L@37:
	ldl 29
	ldl 7
	diff
	stl 29
	ldc _lit_load_segsize-LF347
	ldpi
LF347:
	ldnl 0
	ldl 7
	add
	ldc _lit_load_segsize-LF351
	ldpi
LF351:
	stnl 0
	j L@3
L@39:
	call _lit_load_in_number
	stl 12
	ldl 31
	ldl 30
	call _lit_load_in_expr
	stl 13
	ldl 31
	ldl 30
	call _lit_load_in_expr
	stl 14
	ldl 14
	ldl 29
	gt
	cj L@40
	ldc _boot_link_out-LF380
	ldpi
LF380:
	ldnl 0
	stl 25
	ldl 25
	ldc -1431699294
	outword
	stopp
L@40:
	ldl 14
	stl 0
	ldl 13
	ldl 12
	ldl 28
	call _lit_load_cmd_generate
	ldl 14
	ldl 28
	add
	stl 28
	ldl 29
	ldl 14
	diff
	stl 29
	ldc _lit_load_segsize-LF406
	ldpi
LF406:
	ldnl 0
	ldl 14
	add
	ldc _lit_load_segsize-LF410
	ldpi
LF410:
	stnl 0
	j L@3
L@41:
	ldc _last_number_len-LF418
	ldpi
LF418:
	ldnl 0
	stl 15
	call _lit_load_in_number
	ldc _last_number_len-LF422
	ldpi
LF422:
	ldnl 0
	ldl 15
	add
	stl 15
	call _lit_load_in_number
	ldc _last_number_len-LF428
	ldpi
LF428:
	ldnl 0
	ldl 15
	add
	stl 15
L@42:
	ldl 15
	ldc 7
	gt
	eqc 0
	cj L@43
	ldc 0
	stl 1
	ldlp 1
	ldc _boot_link_in-LF448
	ldpi
LF448:
	ldnl 0
	ldc 1
	in
	ldl 15
	adc 1
	stl 15
	j L@42
L@43:
	ldc 0
	ldc _lit_load_segname-LF463
	ldpi
LF463:
	sb
	ldc 0
	ajw +27
	ret
L@47:
	call _lit_load_in_number
	stl 16
	ldl 16
	ldc 32
	gt
	cj L@48
	ldc _boot_link_out-LF480
	ldpi
LF480:
	ldnl 0
	stl 17
	ldl 17
	ldc -1431699251
	outword
	stopp
L@48:
	ldc _lit_load_segname-LF489
	ldpi
LF489:
	ldc _boot_link_in-LF490
	ldpi
LF490:
	ldnl 0
	ldl 16
	in
	call _lit_load_in_number
	ldc 1
	ajw +27
	ret
L@51:
	ldc _boot_link_out-LF503
	ldpi
LF503:
	ldnl 0
	stl 26
	ldl 26
	ldc -1431699244
	outword
	stopp
	j L@3
_lit_load_cmd_generate:
	ajw -6
	ldl 10
	ldl 7
	add
	stl 1
	ldc 0
	ldl 7
	ldl 1
	ldiff
	rev
	eqc 0
	cj L@53
	ldc _boot_link_out-LF535
	ldpi
LF535:
	ldnl 0
	stl 2
	ldl 2
	ldc -1431699230
	outword
	stopp
L@53:
	ldl 1
	adc -1
	stl 1
	ldc 15
	ldl 9
	and
	ldl 8
	or
	ldl 1
	sb
	ldc 0
	ldl 9
	gt
	eqc 0
	cj L@54
L@55:
	ldl 9
	ldc 4
	shr
	stl 9
	ldl 9
	cj L@59
	ldc 0
	ldl 7
	ldl 1
	ldiff
	rev
	eqc 0
	cj L@58
	ldc _boot_link_out-LF578
	ldpi
LF578:
	ldnl 0
	stl 3
	ldl 3
	ldc -1431699226
	outword
	stopp
L@58:
	ldl 1
	adc -1
	stl 1
	ldc 15
	ldl 9
	and
	ldc 32
	or
	ldl 1
	sb
	j L@55
L@54:
	ldl 9
	not
	stl 9
L@60:
	ldl 9
	ldc 4
	shr
	stl 9
	ldl 9
	ldc 15
	gt
	cj L@61
	ldc 0
	ldl 7
	ldl 1
	ldiff
	rev
	eqc 0
	cj L@63
	ldc _boot_link_out-LF626
	ldpi
LF626:
	ldnl 0
	stl 4
	ldl 4
	ldc -1431699219
	outword
	stopp
L@63:
	ldl 1
	adc -1
	stl 1
	ldc 15
	ldl 9
	not
	and
	ldc 32
	or
	ldl 1
	sb
	j L@60
L@61:
	ldc 0
	ldl 7
	ldl 1
	ldiff
	rev
	eqc 0
	cj L@64
	ldc _boot_link_out-LF659
	ldpi
LF659:
	ldnl 0
	stl 5
	ldl 5
	ldc -1431699216
	outword
	stopp
L@64:
	ldl 1
	adc -1
	stl 1
	ldc 15
	ldl 9
	and
	ldc 96
	or
	ldl 1
	sb
L@59:
L@65:
	ldc 0
	ldl 7
	ldl 1
	ldiff
	rev
	cj L@66
	ldl 1
	adc -1
	stl 1
	ldc 32
	ldl 1
	sb
	j L@65
L@66:
	ldl 10
	ajw +6
	ret
_lit_load_in_number:
	ajw -12
	ldc 0
	stl 2
	ldc 0
	stl 3
	ldl 3
	stl 1
	ldc _boot_link_in-LF719
	ldpi
LF719:
	ldnl 0
	ldlp 1
	stl 7
	ldl 7
	rev
	ldc 1
	in
	ldl 1
	stl 10
	ldc 1
	ldc _last_number_len-LF733
	ldpi
LF733:
	stnl 0
	ldl 10
	eqc 255
	cj L@70
	ldc 1
	stl 2
	ldl 3
	stl 1
	ldl 7
	ldc _boot_link_in-LF747
	ldpi
LF747:
	ldnl 0
	ldc 1
	in
	ldl 1
	stl 10
	ldc _last_number_len-LF755
	ldpi
LF755:
	ldnl 0
	adc 1
	ldc _last_number_len-LF758
	ldpi
LF758:
	stnl 0
L@70:
	ldl 10
	adc -251
	stl 6
	ldc 0
	ldc 4
	ldl 6
	ldiff
	rev
	eqc 0
	cj L@93
	ldl 6
	ldc L@94-LF773
	ldpi
LF773:
	wsub
	ldnl 0
	adc L@94-LF776
	ldpi
LF776:
	gcall
	.align 4;
L@94:
	.word L@90-L@94
	.word L@85-L@94
	.word L@76-L@94
	.word L@75-L@94
	.word L@74-L@94
L@74:
	ldc _boot_link_out-LF783
	ldpi
LF783:
	ldnl 0
	stl 4
	ldl 4
	ldc -1431699187
	outword
	stopp
	ldc 0
	ajw +12
	ret
L@75:
	ldc _boot_link_out-LF796
	ldpi
LF796:
	ldnl 0
	stl 5
	ldl 5
	ldc -1431699184
	outword
	stopp
	ldc 0
	ajw +12
	ret
L@76:
	ldc 0
	stl 1
	ldc _boot_link_in-LF814
	ldpi
LF814:
	ldnl 0
	ldc 1
	ldlp 1
	stl 8
	ldl 8
	rev
	stl -1
	rev
	ldl -1
	in
	ldl 1
	ldl 3
	or
	stl 3
	ldc 0
	stl 1
	ldl 8
	ldc _boot_link_in-LF837
	ldpi
LF837:
	ldnl 0
	ldc 1
	in
	ldl 1
	ldc 8
	shl
	ldl 3
	or
	stl 3
	ldc 0
	stl 1
	ldl 8
	ldc _boot_link_in-LF855
	ldpi
LF855:
	ldnl 0
	ldc 1
	in
	ldl 1
	ldc 16
	shl
	ldl 3
	or
	stl 3
	ldc 0
	stl 1
	ldl 8
	ldc _boot_link_in-LF873
	ldpi
LF873:
	ldnl 0
	ldc 1
	in
	ldl 1
	ldc 24
	shl
	ldl 3
	or
	stl 3
	ldc _last_number_len-LF886
	ldpi
LF886:
	ldnl 0
	ldnlp 1
	ldc _last_number_len-LF889
	ldpi
LF889:
	stnl 0
	j L@73
L@85:
	ldc 0
	stl 1
	ldc _boot_link_in-LF900
	ldpi
LF900:
	ldnl 0
	ldc 1
	ldlp 1
	stl 9
	ldl 9
	rev
	stl -1
	rev
	ldl -1
	in
	ldl 1
	ldl 3
	or
	stl 3
	ldc 0
	stl 1
	ldl 9
	ldc _boot_link_in-LF923
	ldpi
LF923:
	ldnl 0
	ldc 1
	in
	ldl 1
	ldc 8
	shl
	ldl 3
	or
	stl 3
	ldc _last_number_len-LF936
	ldpi
LF936:
	ldnl 0
	adc 2
	ldc _last_number_len-LF939
	ldpi
LF939:
	stnl 0
	j L@73
L@90:
	ldc 0
	stl 1
	ldlp 1
	ldc _boot_link_in-LF949
	ldpi
LF949:
	ldnl 0
	ldc 1
	in
	ldl 1
	ldl 3
	or
	stl 3
	ldc _last_number_len-LF960
	ldpi
LF960:
	ldnl 0
	adc 1
	ldc _last_number_len-LF963
	ldpi
LF963:
	stnl 0
	j L@73
L@93:
	ldl 10
	stl 3
L@73:
	ldl 2
	cj L@95
	ldl 3
	not
	stl 3
L@95:
	ldl 3
	ajw +12
	ret
_lit_load_in_expr:
	ajw -59
	ldc 0
	stl 13
	ldlp 1
	stl 15
L@96:
	call _lit_load_in_number
	stl 11
	ldl 11
	eqc 250
	eqc 0
	cj L@97
	ldc 0
	ldc 27
	ldl 11
	ldiff
	rev
	cj L@99
	ldc _boot_link_out-LF1012
	ldpi
LF1012:
	ldnl 0
	stl 46
	ldl 46
	ldc -1431699142
	outword
	stopp
L@99:
	ldc 0
	ldc 1
	ldl 11
	ldiff
	rev
	eqc 0
	ldl 11
	eqc 25
	or
	stl 14
	ldl 14
	eqc 0
	cj L@101
	ldl 14
	ldc 1
	ldl 11
	adc -26
	ldiff
	rev
	eqc 0
	cj L@100
L@101:
	ldl 13
	ldc 9
	gt
	cj L@102
	ldc _boot_link_out-LF1047
	ldpi
LF1047:
	ldnl 0
	stl 47
	ldl 47
	ldc -1431699138
	outword
	stopp
L@102:
	ldl 11
	eqc 1
	eqc 0
	cj L@105
	ldl 11
	ldc 1
	gt
	eqc 0
	cj L@111
	ldl 11
	cj L@104
	j L@96
L@111:
	ldl 11
	eqc 25
	eqc 0
	cj L@108
	ldl 11
	eqc 26
	eqc 0
	cj L@107
	j L@96
L@104:
	call _lit_load_in_number
	ldl 13
	ldl 15
	wsub
	stnl 0
	ldl 13
	adc 1
	stl 13
	j L@96
L@105:
	call _lit_load_in_number
	stl 12
	ldl 61
	ldl 12
	gt
	eqc 0
	cj L@106
	ldc _boot_link_out-LF1098
	ldpi
LF1098:
	ldnl 0
	stl 48
	ldl 48
	ldc -1431699129
	outword
	stopp
L@106:
	ldl 13
	ldl 15
	wsub
	ldl 12
	ldl 60
	wsub
	ldnl 0
	rev
	stnl 0
	ldl 13
	adc 1
	stl 13
	j L@96
L@107:
	ldc _boot_link_out-LF1122
	ldpi
LF1122:
	ldnl 0
	stl 49
	ldl 49
	ldc -1431699124
	outword
	stopp
	j L@96
L@108:
	ldc _boot_link_out-LF1133
	ldpi
LF1133:
	ldnl 0
	stl 50
	ldl 50
	ldc -1431699120
	outword
	stopp
	j L@96
L@100:
	ldc 0
	ldc 1
	ldl 11
	adc -2
	ldiff
	rev
	eqc 0
	ldl 11
	eqc 4
	or
	cj L@113
	ldl 13
	ldc 0
	gt
	eqc 0
	cj L@114
	ldc _boot_link_out-LF1161
	ldpi
LF1161:
	ldnl 0
	stl 51
	ldl 51
	ldc -1431699113
	outword
	stopp
L@114:
	ldl 11
	eqc 3
	eqc 0
	cj L@117
	ldl 11
	ldc 3
	gt
	eqc 0
	cj L@121
	ldl 11
	eqc 2
	eqc 0
	cj L@116
	j L@119
L@121:
	ldl 11
	eqc 4
	eqc 0
	cj L@118
	j L@119
L@116:
	ldl 13
	ldl 15
	wsub
	stl 16
	ldl 16
	ldnl -1
	not
	adc 1
	ldl 16
	stnl -1
	j L@96
L@117:
	ldl 13
	ldl 15
	wsub
	stl 17
	ldl 17
	ldnl -1
	eqc 0
	ldl 17
	stnl -1
	j L@96
L@118:
	ldl 13
	ldl 15
	wsub
	stl 18
	ldl 18
	ldnl -1
	not
	ldl 18
	stnl -1
	j L@96
L@119:
	ldc _boot_link_out-LF1231
	ldpi
LF1231:
	ldnl 0
	stl 52
	ldl 52
	ldc -1431699100
	outword
	stopp
	j L@96
L@113:
	ldl 11
	eqc 23
	cj L@123
	call _lit_load_in_number
	stl 11
	ldl 11
	eqc 24
	eqc 0
	cj L@124
	ldc _boot_link_out-LF1252
	ldpi
LF1252:
	ldnl 0
	stl 53
	ldl 53
	ldc -1431699093
	outword
	stopp
L@124:
	ldl 13
	ldc 2
	gt
	eqc 0
	cj L@125
	ldc _boot_link_out-LF1265
	ldpi
LF1265:
	ldnl 0
	stl 54
	ldl 54
	ldc -1431699091
	outword
	stopp
L@125:
	ldl 13
	ldl 15
	wsub
	stl 19
	ldl 19
	ldnlp -3
	stl 20
	ldl 19
	ldnl -3
	cj L@126
	ldl 19
	ldnl -2
	stl 21
	j L@127
L@126:
	ldl 13
	ldl 15
	wsub
	ldnl -1
	stl 21
L@127:
	ldl 21
	ldl 20
	stnl 0
	ldl 13
	adc -2
	stl 13
	j L@96
L@123:
	ldl 13
	ldc 1
	gt
	eqc 0
	cj L@129
	ldc _boot_link_out-LF1312
	ldpi
LF1312:
	ldnl 0
	stl 55
	ldl 55
	ldc -1431699083
	outword
	stopp
L@129:
	ldl 11
	adc -5
	stl 44
	ldc 0
	ldc 17
	ldl 44
	ldiff
	rev
	eqc 0
	cj L@154
	ldl 44
	ldc L@155-LF1331
	ldpi
LF1331:
	wsub
	ldnl 0
	adc L@155-LF1334
	ldpi
LF1334:
	gcall
	.align 4;
L@155:
	.word L@131-L@155
	.word L@132-L@155
	.word L@133-L@155
	.word L@134-L@155
	.word L@135-L@155
	.word L@136-L@155
	.word L@138-L@155
	.word L@140-L@155
	.word L@141-L@155
	.word L@142-L@155
	.word L@143-L@155
	.word L@144-L@155
	.word L@145-L@155
	.word L@146-L@155
	.word L@147-L@155
	.word L@148-L@155
	.word L@149-L@155
	.word L@151-L@155
L@131:
	ldl 13
	ldl 15
	wsub
	stl 22
	ldl 22
	ldnl -2
	ldl 22
	ldnl -1
	diff
	ldl 22
	stnl -2
	j L@130
L@132:
	ldl 13
	ldl 15
	wsub
	stl 23
	ldl 23
	ldnl -1
	ldl 23
	ldnl -2
	add
	ldl 23
	stnl -2
	j L@130
L@133:
	ldl 13
	ldl 15
	wsub
	stl 24
	ldl 24
	ldnl -2
	ldl 24
	ldnl -1
	shl
	ldl 24
	stnl -2
	j L@130
L@134:
	ldl 13
	ldl 15
	wsub
	stl 25
	ldl 25
	ldnl -2
	xdble
	ldl 25
	ldnl -1
	lshr
	ldl 25
	stnl -2
	j L@130
L@135:
	ldl 13
	ldl 15
	wsub
	stl 26
	ldl 26
	ldnl -1
	ldl 26
	ldnl -2
	prod
	ldl 26
	stnl -2
	j L@130
L@136:
	ldl 13
	ldl 15
	wsub
	ldnl -1
	eqc 0
	cj L@137
	ldc _boot_link_out-LF1424
	ldpi
LF1424:
	ldnl 0
	stl 56
	ldl 56
	ldc -1431699063
	outword
	stopp
L@137:
	ldl 13
	ldl 15
	wsub
	stl 27
	ldl 27
	ldnl -2
	ldl 27
	ldnl -1
	div
	ldl 27
	stnl -2
	j L@130
L@138:
	ldl 13
	ldl 15
	wsub
	ldnl -1
	eqc 0
	cj L@139
	ldc _boot_link_out-LF1454
	ldpi
LF1454:
	ldnl 0
	stl 57
	ldl 57
	ldc -1431699058
	outword
	stopp
L@139:
	ldl 13
	ldl 15
	wsub
	stl 28
	ldl 28
	ldnl -2
	ldl 28
	ldnl -1
	rem
	ldl 28
	stnl -2
	j L@130
L@140:
	ldl 13
	ldl 15
	wsub
	stl 29
	ldl 29
	ldnl -1
	ldl 29
	ldnl -2
	and
	ldl 29
	stnl -2
	j L@130
L@141:
	ldl 13
	ldl 15
	wsub
	stl 30
	ldl 30
	ldnl -1
	ldl 30
	ldnl -2
	xor
	ldl 30
	stnl -2
	j L@130
L@142:
	ldl 13
	ldl 15
	wsub
	stl 31
	ldl 31
	ldnl -1
	ldl 31
	ldnl -2
	or
	ldl 31
	stnl -2
	j L@130
L@143:
	ldl 13
	ldl 15
	wsub
	stl 32
	ldl 32
	ldnl -1
	ldl 32
	ldnl -2
	gt
	ldl 32
	stnl -2
	j L@130
L@144:
	ldl 13
	ldl 15
	wsub
	stl 33
	ldl 33
	ldnl -2
	ldl 33
	ldnl -1
	gt
	eqc 0
	ldl 33
	stnl -2
	j L@130
L@145:
	ldl 13
	ldl 15
	wsub
	stl 34
	ldl 34
	ldnl -2
	ldl 34
	ldnl -1
	gt
	ldl 34
	stnl -2
	j L@130
L@146:
	ldl 13
	ldl 15
	wsub
	stl 35
	ldl 35
	ldnl -1
	ldl 35
	ldnl -2
	gt
	eqc 0
	ldl 35
	stnl -2
	j L@130
L@147:
	ldl 13
	ldl 15
	wsub
	stl 36
	ldl 36
	ldnl -2
	ldl 36
	ldnl -1
	diff
	eqc 0
	ldl 36
	stnl -2
	j L@130
L@148:
	ldl 13
	ldl 15
	wsub
	stl 37
	ldl 37
	ldnl -2
	ldl 37
	ldnl -1
	diff
	eqc 0
	eqc 0
	ldl 37
	stnl -2
	j L@130
L@149:
	ldl 13
	ldl 15
	wsub
	stl 38
	ldl 38
	ldnlp -2
	stl 39
	ldc 0
	stl 40
	ldl 38
	ldnl -2
	cj L@150
	ldl 38
	ldnl -1
	eqc 0
	eqc 0
	stl 40
L@150:
	ldl 40
	ldl 39
	stnl 0
	j L@130
L@151:
	ldl 13
	ldl 15
	wsub
	stl 41
	ldl 41
	ldnlp -2
	stl 42
	ldc 0
	stl 43
	ldl 41
	ldnl -2
	eqc 0
	cj L@153
	ldl 41
	ldnl -1
	cj L@152
L@153:
	ldc 1
	stl 43
L@152:
	ldl 43
	ldl 42
	stnl 0
	j L@130
L@154:
	ldc _boot_link_out-LF1670
	ldpi
LF1670:
	ldnl 0
	stl 58
	ldl 58
	ldc -1431699021
	outword
	stopp
L@130:
	ldl 13
	adc -1
	stl 13
	j L@96
L@97:
	ldl 13
	eqc 1
	eqc 0
	cj L@156
	ldc _boot_link_out-LF1689
	ldpi
LF1689:
	ldnl 0
	stl 45
	ldl 45
	ldc -1431699010
	outword
	stopp
L@156:
	ldl 1
	ajw +59
	ret
.globl _lit_load_segname; _lit_load_segname: .byte ?[33];
.head2
	.align 4
_last_number_len:
	.byte ?[4]
