.section .text,"ax"
.global low64
low64:
 ldl 1
 fpb32tor64
 ldl 2
 fpstnldb
 ret
.global low32
low32:
 ldl 1
 fpb32tor64
 ldc 13
 fpentry
 ldl 2
 fpstnlsn
 ret
.global high64
high64:
 ldl 1
 ldnlp 1
 fpi32tor64
 ldc 10
 fpentry
 ldl 2
 fpstnldb
 ret
.global high32
high32:
 ldl 1
 ldnlp 1
 fpi32tor64
 ldc 10
 fpentry
 ldc 13
 fpentry
 ldl 2
 fpstnlsn
 ret
.global nonround
nonround:
 ldl 1
 fpldnldb
 ldc 13
 fpentry
 ldl 2
 fpstnlsn
 ret
