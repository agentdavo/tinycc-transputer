.section .text,"ax"
.global tcwg_down
tcwg_down:
    ldl 1
    dup
    fpb32tor64
    ldc 13
    fpentry
    ldnlp 1
    fpi32tor64
    ldc 10
    fpentry
    ldc 13
    fpentry
    ldc 5
    fpentry
    fpadd
    ldl 2
    fpstnlsn
    ret
.global tcwg_current
tcwg_current:
    ldl 1
    dup
    fpb32tor64
    ldnlp 1
    fpi32tor64
    ldc 10
    fpentry
    fpadd
    ldc 8
    fpentry
    ldl 2
    fpstnlsn
    ret
