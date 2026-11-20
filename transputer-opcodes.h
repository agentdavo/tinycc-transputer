#ifndef _TRANSPUTER_OPCODES_H
#define _TRANSPUTER_OPCODES_H

/* Core constants */
#define TP_MIN_INT 0x80000000

/* ------------------------------------------------------------------
 * Direct Functions (High nibble of instruction byte)
 * ------------------------------------------------------------------ */
#define TP_OP_J        0x0
#define TP_OP_LDLP     0x1
#define TP_OP_PFIX     0x2
#define TP_OP_LDNL     0x3
#define TP_OP_LDC      0x4
#define TP_OP_LDNLP    0x5
#define TP_OP_NFIX     0x6
#define TP_OP_LDL      0x7
#define TP_OP_ADC      0x8
#define TP_OP_CALL     0x9
#define TP_OP_CJ       0xA
#define TP_OP_AJW      0xB
#define TP_OP_EQC      0xC
#define TP_OP_STL      0xD
#define TP_OP_STNL     0xE
#define TP_OP_OPR      0xF

/* ------------------------------------------------------------------
 * Operations (Arguments to OPR instruction)
 * ------------------------------------------------------------------ */

/* Core Stack / Memory */
#define TP_OPR_REV          0x00
#define TP_OPR_LB           0x01
#define TP_OPR_BSUB         0x02
#define TP_OPR_WSUB         0x0A
#define TP_OPR_SB           0x3B
#define TP_OPR_SAVEL        0x3D
#define TP_OPR_SAVEH        0x3E
#define TP_OPR_WCNT         0x3F
#define TP_OPR_MOVE         0x4A
#define TP_OPR_DUP          0x5A /* T800 */

/* Integer Arithmetic (Checked - throws error on overflow) */
#define TP_OPR_ADD          0x05
#define TP_OPR_SUB          0x0C
#define TP_OPR_MUL          0x53
#define TP_OPR_DIV          0x2C
#define TP_OPR_REM          0x1F
#define TP_OPR_SETERR       0x10
#define TP_OPR_LADD         0x16
#define TP_OPR_LDIV         0x1A
#define TP_OPR_LEND         0x21

/* Integer Arithmetic (Unchecked / Modulo - C standard behavior) */
#define TP_OPR_SUM          0x52
#define TP_OPR_DIFF         0x04
#define TP_OPR_PROD         0x08

/* Logic & Shifts */
#define TP_OPR_AND          0x46
#define TP_OPR_OR           0x4B
#define TP_OPR_XOR          0x33
#define TP_OPR_NOT          0x32
#define TP_OPR_SHL          0x41
#define TP_OPR_SHR          0x40
#define TP_OPR_BCNT         0x34
#define TP_OPR_LSHR         0x35 /* Long shift right */
#define TP_OPR_LSHL         0x36 /* Long shift left */
#define TP_OPR_LSUM         0x37 /* Long sum with carry */
#define TP_OPR_LSUB         0x38 /* Long subtract with borrow */
#define TP_OPR_RUNP         0x39

/* Multi-precision Arithmetic */
#define TP_OPR_LMUL         0x31 /* Long multiply: 32x32 -> 64 bit result */
#define TP_OPR_LDIFF        0x4F /* Long difference */

/* Comparisons */
#define TP_OPR_GT           0x09
#define TP_OPR_CSUB0        0x13 /* Bounds check (0 <= A < B) */
#define TP_OPR_CCNT1        0x4D /* Count check (1 <= A <= B) */

/* FPU Operations (T800) */
#define TP_OPR_FPADD        0x87
#define TP_OPR_FPSUB        0x89
#define TP_OPR_FPMUL        0x8B
#define TP_OPR_FPDIV        0x8C
#define TP_OPR_FPEQ         0x95
#define TP_OPR_FPGT         0x94
#define TP_OPR_FPI32        0x9D  /* Float to Int32 */
#define TP_OPR_I32FP        0x96  /* Int32 to Float */

/* Conversion */
#define TP_OPR_XWORD        0x3A /* Sign extend word */
#define TP_OPR_XDBLE        0x1D /* Sign extend to double */
#define TP_OPR_STLB         0x17
#define TP_OPR_STHF         0x18
#define TP_OPR_NORM         0x19
#define TP_OPR_LDPI         0x1B
#define TP_OPR_STLF         0x1C

/* Process / Scheduler */
#define TP_OPR_STARTP       0x0D
#define TP_OPR_ENDP         0x03
#define TP_OPR_STOPP        0x15
#define TP_OPR_LDPRI        0x1E
#define TP_OPR_MINT         0x42
#define TP_OPR_ALT          0x43
#define TP_OPR_ALTWT        0x44
#define TP_OPR_ALTEND       0x45
#define TP_OPR_TALT         0x4E
#define TP_OPR_TALTWT       0x51
#define TP_OPR_ENBT         0x47
#define TP_OPR_ENBC         0x48
#define TP_OPR_ENBS         0x49
#define TP_OPR_STHB         0x50
#define TP_OPR_STOPERR      0x55
#define TP_OPR_CWORD        0x56
#define TP_OPR_CSNGL        0x4C

/* Channels */
#define TP_OPR_IN           0x07
#define TP_OPR_OUT          0x0B
#define TP_OPR_OUTWORD      0x0F
#define TP_OPR_OUTBYTE      0x0E
#define TP_OPR_RESETCH      0x12

/* Timer */
#define TP_OPR_LDTIMER      0x22
#define TP_OPR_STTIMER      0x54
#define TP_OPR_TIN          0x2B
#define TP_OPR_TESTLDS      0x23
#define TP_OPR_TESTLDE      0x24
#define TP_OPR_TESTLDD      0x25
#define TP_OPR_TESTSTS      0x26
#define TP_OPR_TESTSTE      0x27
#define TP_OPR_TESTSTD      0x28
#define TP_OPR_TESTERR      0x29
#define TP_OPR_TESTPRANAL   0x2A
#define TP_OPR_TESTHARDCHAN 0x2D
#define TP_OPR_DIST         0x2E
#define TP_OPR_DISC         0x2F
#define TP_OPR_DISS         0x30
#define TP_OPR_CLRHALTERR   0x57
#define TP_OPR_SETHALTERR   0x58
#define TP_OPR_TESTHALTERR  0x59
#define TP_OPR_MOVE2DINIT   0x5B
#define TP_OPR_MOVE2DALL    0x5C
#define TP_OPR_MOVE2DNONZERO 0x5D
#define TP_OPR_MOVE2DZERO   0x5E
#define TP_OPR_GTU          0x5F
#define TP_OPR_UNPACKSN     0x63
#define TP_OPR_POSTNORMSN   0x6C
#define TP_OPR_ROUNDSN      0x6D
#define TP_OPR_LDINF        0x71
#define TP_OPR_FMUL         0x72
#define TP_OPR_CFLERR       0x73
#define TP_OPR_CRCWORD      0x74
#define TP_OPR_CRCBYTE      0x75
#define TP_OPR_BITCNT       0x76
#define TP_OPR_BITREVWORD   0x77
#define TP_OPR_BITREVNBITS  0x78
#define TP_OPR_POP          0x79
#define TP_OPR_TIMERDISABLEH 0x7A
#define TP_OPR_TIMERDISABLEL 0x7B
#define TP_OPR_TIMERENABLEH  0x7C
#define TP_OPR_TIMERENABLEL  0x7D
#define TP_OPR_LDMEMSTARTVAL 0x7E
#define TP_OPR_FPSTTEST     0x80
#define TP_OPR_WSUBDB       0x81

/* Control */
#define TP_OPR_RET          0x20
#define TP_OPR_GCALL        0x06
#define TP_OPR_GAJW         0x3C
#define TP_OPR_FPTESTERR    0x9C

/* ------------------------------------------------------------------
 * FPU Operations (T800) - Direct OPR codes
 * These are executed as: opr 0x87 (for fpadd)
 * ------------------------------------------------------------------ */
#define TP_FPU_LDNLSN       0x8E
#define TP_FPU_LDNLDB       0x8A
#define TP_FPU_STNLSN       0x88
#define TP_FPU_STNLDB       0x84
#define TP_FPU_ADD          0x87
#define TP_FPU_SUB          0x89
#define TP_FPU_MUL          0x8B
#define TP_FPU_DIV          0x8C
#define TP_FPU_GT           0x94
#define TP_FPU_EQ           0x95
#define TP_FPU_REV          0xA4
#define TP_FPU_DUP          0xA3
#define TP_FPU_NAN          0x91
#define TP_FPU_ORDERED      0x92
#define TP_FPU_NOTFINITE    0x93
#define TP_FPU_CHKERR       0x83
#define TP_FPU_TESTERR      0x9C
/* Fused load-and-operate (T800): load from Mem[AReg] and combine with FAreg */
#define TP_FPU_LDNLADDSN    0xAA
#define TP_FPU_LDNLADDDB    0xA6
#define TP_FPU_LDNLMULSN    0xAC
#define TP_FPU_LDNLMULDB    0xA8

/* Type Conversion */
#define TP_FPU_RTOI32       0x9D /* Real to Int32 */
#define TP_FPU_I32TOR32     0x96 /* Int32 to Real32 */
#define TP_FPU_I32TOR64     0x98
#define TP_FPU_STNLI32      0x9E /* Store Non-Local Int32 from FPU */

/* ------------------------------------------------------------------
 * FPU Indirect Operations (fpentry)
 * These are executed as: ldc <OP>; opr 0xAB (fpentry)
 * ------------------------------------------------------------------ */
#define TP_OPR_FPENTRY      0xAB

/* Codes to load into Areg before calling fpentry */
#define TP_FPENTRY_SQRTFIRST 0x01
#define TP_FPENTRY_SQRTSTEP  0x02
#define TP_FPENTRY_SQRTLAST  0x03
#define TP_FPENTRY_RP        0x04 /* Round Positive */
#define TP_FPENTRY_RM        0x05 /* Round Minus */
#define TP_FPENTRY_RZ        0x06 /* Round Zero */
#define TP_FPENTRY_R32TOR64  0x07
#define TP_FPENTRY_R64TOR32  0x08
#define TP_FPENTRY_EXPDEC32  0x09 /* FA / 2^32 */
#define TP_FPENTRY_EXPINC32  0x0A /* FA * 2^32 */
#define TP_FPENTRY_ABS       0x0B
#define TP_FPENTRY_URN       0x22 /* Round Nearest */
#define TP_FPENTRY_USETERR   0x23 /* set the FP error flag */
#define TP_FPENTRY_UCLRERR   0x9C /* clear the FP error flag */

/* FPU indirect operations (ldc <op>; opr fpentry) */
#define TP_FPU_OP_SQRTFIRST  0x20001
#define TP_FPU_OP_SQRTSTEP   0x20002
#define TP_FPU_OP_SQRTLAST   0x20003
#define TP_FPU_OP_RP         0x20004
#define TP_FPU_OP_RM         0x20005
#define TP_FPU_OP_RZ         0x20006
#define TP_FPU_OP_R32TOR64   0x20007
#define TP_FPU_OP_R64TOR32   0x20008
#define TP_FPU_OP_EXPDEC32   0x20009
#define TP_FPU_OP_EXPINC32   0x2000A
#define TP_FPU_OP_ABS        0x2000B
#define TP_FPU_OP_NOROUND    0x2000D
#define TP_FPU_OP_CHKI32     0x2000E
#define TP_FPU_OP_CHKI64     0x2000F
#define TP_FPU_OP_DIVBY2     0x20011
#define TP_FPU_OP_MULBY2     0x20012
#define TP_FPU_OP_RN         0x20022
#define TP_FPU_OP_SETERR     0x20023
#define TP_FPU_OP_CLRERR     0x2009C

/* ------------------------------------------------------------------
 * Additional FPU OPR codes (TCWG/temu opcodes.h)
 * ------------------------------------------------------------------ */
#define TP_OPR_FPLDNLDBI     0x82
#define TP_OPR_FPLDTEST      0x85
#define TP_OPR_FPLDNLSNI     0x86
#define TP_OPR_FPRANGE       0x8D /* T9000 only */
#define TP_OPR_FPREMFIRST    0x8F
#define TP_OPR_FPREMSTEP     0x90
#define TP_OPR_FPNAN         0x91
#define TP_OPR_FPORDERED     0x92
#define TP_OPR_FPNOTFINITE   0x93
#define TP_OPR_FPI32TOR32    0x96
#define TP_OPR_FPGE          0x97 /* T9000 only */
#define TP_OPR_FPI32TOR64    0x98
#define TP_OPR_FPB32TOR64    0x9A
#define TP_OPR_FPLG          0x9B /* T9000 only */
#define TP_OPR_FPRTOI32      0x9D
#define TP_OPR_FPSTNLI32     0x9E
#define TP_OPR_FPLDZEROSN    0x9F
#define TP_OPR_FPLDZERODB    0xA0
#define TP_OPR_FPINT         0xA1
#define TP_OPR_FPDUP         0xA3
#define TP_OPR_FPREV         0xA4
#define TP_OPR_FPLDNLADDDB   0xA6
#define TP_OPR_FPLDNLMULDB   0xA8
#define TP_OPR_FPLDNLADDSN   0xAA
#define TP_OPR_FPLDNLMULSN   0xAC

/* T9000-only and extended operations */
#define TP_OPR_SETTIMESLICE  0xB0
#define TP_OPR_BREAK         0xB1 /* T425 only */
#define TP_OPR_CLRJ0BREAK    0xB2 /* T425 only */
#define TP_OPR_SETJ0BREAK    0xB3 /* T425 only */
#define TP_OPR_TESTJ0BREAK   0xB4 /* T425 only */
#define TP_OPR_LDFLAGS       0xB6 /* T9000 only */
#define TP_OPR_STFLAGS       0xB7 /* T9000 only */
#define TP_OPR_XBWORD        0xB8 /* T9000 only */
#define TP_OPR_LBX           0xB9 /* T9000 only */
#define TP_OPR_CB            0xBA /* T9000 only */
#define TP_OPR_CBU           0xBB /* T9000 only */
#define TP_OPR_INSPHDR       0xBC /* T9000 only */
#define TP_OPR_READBFR       0xBD /* T9000 only */
#define TP_OPR_LDCONF        0xBE /* T9000 only */
#define TP_OPR_STCONF        0xBF /* T9000 only */
#define TP_OPR_LDCNT         0xC0 /* T9000 only */
#define TP_OPR_SSUB          0xC1 /* T9000 only */
#define TP_OPR_LDTH          0xC2 /* T9000 only */
#define TP_OPR_LDCHSTATUS    0xC3 /* T9000 only */
#define TP_OPR_INTDIS        0xC4 /* T9000 only */
#define TP_OPR_INTENB        0xC5 /* T9000 only */
#define TP_OPR_CIR           0xC7 /* T9000 only */
#define TP_OPR_SS            0xC8 /* T9000 only */
#define TP_OPR_CHANTYPE      0xC9 /* T9000 only */
#define TP_OPR_LS            0xCA /* T9000 only */
/* T9000 virtual channel processor instructions (ISM 12.8-12.9, App.B):
 * negative operations, encoded nfix k; opr x - see tp_emit_opr(). */
#define TP_OPR_STOPCH      (-0x12)
#define TP_OPR_VOUT        (-0x13)
#define TP_OPR_VIN         (-0x14)
#define TP_OPR_SWAPBFR     (-0x17)
#define TP_OPR_SETHDR      (-0x18)
#define TP_OPR_SETCHMODE   (-0x19)
#define TP_OPR_INITVLCB    (-0x1A)
#define TP_OPR_WRITEHDR    (-0x1B)
#define TP_OPR_READHDR     (-0x1C)
/* T9000 semaphores (ISM 8.6) and resource channels (ISM 8.8, 12.7) */
#define TP_OPR_SIGNAL      (-0x0C)
#define TP_OPR_WAIT        (-0x0B)
#define TP_OPR_DISG        (-0x1D)
#define TP_OPR_ENBG        (-0x1E)
#define TP_OPR_GRANT       (-0x1F)
#define TP_OPR_UNMKRC      (-0x23)
#define TP_OPR_MKRC        (-0x24)
#define TP_OPR_IRDSQ       (-0x25)
#define TP_OPR_ERDSQ       (-0x26)
#define TP_OPR_STRESPTR    (-0x27)
#define TP_OPR_LDRESPTR    (-0x28)
#define TP_FPU_GE            0x97 /* T9000 only: IEEE >= (NaN -> false) */
#define TP_OPR_FPRN          0xD0 /* T9000 only: set round-to-nearest */
#define TP_OPR_FPDIVBY2      0xD1 /* T9000 only */
#define TP_OPR_FPMULBY2      0xD2 /* T9000 only */
#define TP_OPR_FPSQRT        0xD3 /* T9000 only */
#define TP_OPR_FPRZ          0xD6 /* T9000 only: set round-to-zero */
#define TP_OPR_FPR32TOR64    0xD7 /* T9000 only */
#define TP_OPR_FPR64TOR32    0xD8 /* T9000 only */
#define TP_OPR_FPABS         0xDB /* T9000 only */
#define TP_OPR_FPSETERR      0xCB /* T9000 only */
#define TP_OPR_CIRU          0xCC /* T9000 only */
#define TP_OPR_FPREM         0xCF /* T9000 only */
#define TP_OPR_FPRN          0xD0 /* T9000 only */
#define TP_OPR_FPDIVBY2      0xD1 /* T9000 only */
#define TP_OPR_FPMULBY2      0xD2 /* T9000 only */
#define TP_OPR_FPSQRT        0xD3 /* T9000 only */
#define TP_OPR_FPRP          0xD4 /* T9000 only */
#define TP_OPR_FPRM          0xD5 /* T9000 only */
#define TP_OPR_FPRZ          0xD6 /* T9000 only */
#define TP_OPR_FPR32TOR64    0xD7 /* T9000 only */
#define TP_OPR_FPR64TOR32    0xD8 /* T9000 only */
#define TP_OPR_FPEXPDEC32    0xD9 /* T9000 only */
#define TP_OPR_FPEXPINC32    0xDA /* T9000 only */
#define TP_OPR_FPABS         0xDB /* T9000 only */
#define TP_OPR_FPCLRERR      0xDC /* T9000 only */
#define TP_OPR_FPADDDBSN     0xDD /* T9000 only */
#define TP_OPR_FPCHKI32      0xDE /* T9000 only */
#define TP_OPR_FPCHKI64      0xDF /* T9000 only */
#define TP_OPR_DEVLB         0xF0 /* T9000 only */
#define TP_OPR_DEVSB         0xF1 /* T9000 only */
#define TP_OPR_DEVLS         0xF2 /* T9000 only */
#define TP_OPR_DEVSS         0xF3 /* T9000 only */
#define TP_OPR_DEVLW         0xF4 /* T9000 only */
#define TP_OPR_DEVSW         0xF5 /* T9000 only */
#define TP_OPR_XSWORD        0xF8 /* T9000 only */
#define TP_OPR_LSX           0xF9 /* T9000 only */
#define TP_OPR_CS            0xFA /* T9000 only */
#define TP_OPR_CSU           0xFB /* T9000 only */
/* ------------------------------------------------------------------
 * TVX - Transputer Vector eXtension (MXFP4 / bfloat16)
 * Encoded as OPR with a 2-nibble PFIX prefix: 0x1Cx -> pfix 1; pfix C; opr x
 * See transputer/TVX-ISA.md for the full specification.
 * ------------------------------------------------------------------ */
#define TP_OPR_BF16TOR32     0x1C0 /* A := f32bits(bf16 A[15:0])            */
#define TP_OPR_R32TOBF16     0x1C1 /* A := bf16bits(f32 A), round-near-even */
#define TP_OPR_BF16PACK      0x1C2 /* A := (bf16(B)<<16)|bf16(A); pop 1     */
#define TP_OPR_BF16UNPKH     0x1C3 /* A := f32bits(bf16 A[31:16])           */
#define TP_OPR_MXFP4TOR32    0x1C4 /* A := f32bits(E2M1 nibble A[3:0])      */
#define TP_OPR_R32TOMXFP4    0x1C5 /* A := E2M1(f32 A), RNE + saturate      */
#define TP_OPR_MXE8M0TOR32   0x1C6 /* A := f32bits(E8M0 scale byte A[7:0])  */
#define TP_OPR_MXE8M0OF      0x1C7 /* A := E8M0 scale byte for amax f32 A   */
#define TP_OPR_MXFP4DOT      0x1C8 /* FA += scale*dot(block A, act B); pop2 */
#define TP_OPR_BF16DOT       0x1C9 /* FA += dot(bf16 A, bf16 B, C); pop 3   */
#define TP_OPR_MXFP4GEMV     0x1CA /* FA += dot over C blocks; pop 3        */
/* Sub-word SIMD: 8 MXFP4 lanes or 2 bfloat16 lanes per 32-bit word */
#define TP_OPR_MXFP4UNPK8    0x1CB /* 8 nibbles in A -> 8 f32 at B; pop 2   */
#define TP_OPR_R32TOMXFP4X8  0x1CC /* 8 f32 at A -> packed 8 nibbles in A   */
#define TP_OPR_BF16MUL2      0x1CD /* A = B * A, 2 packed bf16 lanes; pop 1 */
#define TP_OPR_BF16ADD2      0x1CE /* A = B + A, 2 packed bf16 lanes; pop 1 */
/* Vector kernels for transformer inference.  Length comes from VSETLEN,
   in the MOVE2DINIT tradition (the 3-deep stack cannot carry a 4th operand). */
#define TP_OPR_VSETLEN       0x1CF /* A = element count for vector ops      */
#define TP_OPR_MXFP4GEMVBF   0x1D0 /* like MXFP4GEMV but bf16 activations   */
#define TP_OPR_VADDSN        0x1D1 /* A[] = B[] + C[]                       */
#define TP_OPR_VMULSN        0x1D2 /* A[] = B[] * C[]                       */
#define TP_OPR_VSILUSN       0x1D3 /* A[] = silu(B[])                       */
#define TP_OPR_VRMSNORM      0x1D4 /* A[] = rmsnorm(B[]) * C[]              */
#define TP_OPR_VSOFTMAXSN    0x1D5 /* A[] = softmax(B[])                    */
/* IEEE binary16 (ggml_half / __fp16) and GGUF Q8_0 */
#define TP_OPR_F16TOR32      0x1D6 /* A := f32bits(binary16 A[15:0])        */
#define TP_OPR_R32TOF16      0x1D7 /* A := binary16 bits of f32 A, RNE      */
#define TP_OPR_Q8DOT         0x1D8 /* FA += dot(q8_0 A, f32 B, C blocks)    */
#define TP_OPR_Q8DOTBF       0x1D9 /* FA += dot(q8_0 A, bf16 B, C blocks)   */
/* GGUF i-quants (grid codebooks), 256 elements per block */
#define TP_OPR_IQ1MDOT       0x1DA /* FA += dot(iq1_m A,   f32 B, C blocks) */
#define TP_OPR_IQ2XXSDOT     0x1DB /* FA += dot(iq2_xxs A, f32 B, C blocks) */
#define TP_OPR_IQ3XXSDOT     0x1DC /* FA += dot(iq3_xxs A, f32 B, C blocks) */
/* Generic GGUF quantised dot - any ggml type, no new opcode per format */
#define TP_OPR_QSETFMT       0x1DD /* A = ggml type id for QDOT             */
#define TP_OPR_QDOT          0x1DE /* FA += dot(quant A, f32 B, C blocks)   */
/* Block rotation (BRQ): 32-point randomised Hadamard, VSETLEN elements.
 * The rotation dimension is the MX block size, so it never crosses a block. */
#define TP_OPR_VHAD32        0x1DF /* A[] = had32(B[], sign mask C)         */
#define TP_OPR_MXQROT        0x1E0 /* A[] = mxfp4(had32(f32 B[], mask C))   */
#define TP_OPR_MXQROTBF      0x1E1 /* A[] = mxfp4(had32(bf16 B[], mask C))  */
/* TVX-GFX - Floating-point Transform & Project (T&L geometry) */
#define TP_OPR_FTSETVP       0x1E2 /* latch viewport {sx,sy,ox,oy,guard}=A[] */
#define TP_OPR_FTRANSP       0x1E3 /* A[]=project(C[] 4x4, B[] xyz); A=code  */
#define TP_OPR_FTRANSPB      0x1E4 /* block form; VSETLEN verts; A=AND codes */
#define TP_OPR_FSINCOS       0x1E5 /* A[] = {sin,cos}(BAMS angle B); pop 2   */
#define TP_OPR_FRSQRT        0x1E6 /* A = bits(1/sqrt(f32 A))                */
#define TP_OPR_FRECIP        0x1E7 /* A = bits(1/f32 A)                      */
#define TP_OPR_FSINCOSB      0x1E8 /* A[] = {sin,cos}(u16 B[]); VSETLEN      */
#define TP_OPR_VSETSTRIDE    0x1E9 /* latch element stride A, stream mask B  */
#define TP_OPR_FTCULL        0x1EA /* A[]=cullcodes(planes C[], xyz B[])     */
#define TP_OPR_TSKIN         0x1EB /* A[]=skin(joints B[], param C); VSETLEN */
#define TP_OPR_VSETFMT       0x1EC /* latch position fmt A: 0 f32/1 bf16/2 f16 */
#define TP_OPR_FTSETLIT      0x1ED /* latch light {L,ambient,intensity}=A[]  */
#define TP_OPR_FLITB         0x1EE /* record[5]=light(C[] 3x3, B[] normals)  */
#define TP_OPR_QGEMM         0x1EF /* A[]=W(param B)*acts; rows=VSETLEN, C=nb*/
#define TP_OPR_VBLEND8       0x1F0 /* A[]=blend(A[], B[], alpha C); VSETLEN  */
#define TP_OPR_VFILL         0x1F1 /* fill C bytes at A with pattern B       */
#define TP_OPR_VFILL2D       0x1F2 /* 2D fill; BMReg rows/stride, C=width    */
#define TP_OPR_CSUM          0x1F3 /* A=inet csum(A src, B init, C bytes)    */
#define TP_OPR_VBLEND2D      0x1F4 /* 2D blend; BMRegs + VSETLEN px/row      */
#define TP_OPR_STVXSTATE     0x1F5 /* store 15-word latch state to A; pop 1  */
#define TP_OPR_CSUMCPY       0x1F6 /* A=csum; copy C bytes B->A, folded sum  */
#define TP_OPR_VQ8K          0x1F7 /* A[]=block_q8_K(B[]); VSETLEN, VSETFMT  */
#define TP_OPR_VQ80          0x1F8 /* A[]=block_q8_0(B[]); VSETLEN, VSETFMT  */
#define TP_OPR_VCVT          0x1F9 /* A[]=B[] as format C; VSETLEN, VSETFMT  */
#define TP_OPR_VXFER         0x1FA /* B VCP transfers {chan,buf,len} at A    */

/*
 * T9 FPU architectural Temps T0-T7 and the block fill/spill family.
 *
 * These are the *T9 core* (gf180mcu ip/cpu, ip/fpu) secondaries, taken from
 * ip/cpu/rtl/t9_cpu_pkg.sv and ip/cpu/ISA.md.  Do not confuse them with the
 * 0x1xx TVX block above, which is the vector coprocessor's own operand space
 * (TP_OPR_FRSQRT 0x1E6 is TVX; the T9 core's own frsqrt is 0xEC).
 *
 * The T9000 ISM's own assignments always win: B0 is settimeslice (ISM App B
 * Table B.3), so tld moved to the ISM-unassigned 0xAB (2026-08-16; it briefly
 * shadowed settimeslice at 0xB0).  B1-B5 remain classic-debug bytes the T9000
 * ISM leaves unassigned (break, clr/set/testj0break); the TnL block parks
 * tst/txf/tclr/tpush/fmax0 there and moves out if the ISM ever claims them.
 * 0x28 is deliberately left free for a future f4stm.
 *
 * Operand conventions (the RTL is the spec; see t9_fpu_bridge.sv need_idx_*
 * and t9_core.sv's FPU retire path for the pops):
 *
 *   tld    AB  2A FB  FA <- T[A&7]            pushes the FP stack; A is NOT popped
 *   tst    B1  2B F1  T[A&7] <- FA            FA stays valid; A is NOT popped
 *   txf    B2  2B F2  swap FA <-> T[A&7]      A is NOT popped
 *   tclr   B3  2B F3  T[A&7].valid <- 0       A is NOT popped
 *   tpush  B4  2B F4  T[A&7] <- REAL32_V2{C,B}
 *   f2ldm  A2  2A F2  A=addr of lo word (hi at A+4), B[2:0]=Ti -> TPUSH T[i]
 *   f4ldm  A9  2A F9  A=base of 4 words,      B[2:0]=Ti -> TPUSH T[i], T[i+1]
 *   f2ldma AF  2A FF  async f2ldm; the core fences the next Temps op on it
 *   f2stm  27  22 F7  [A]<-T[B&7].lo, [A+4]<-T[B&7].hi; pops BOTH A and B
 *
 * A Temp is a full 64-bit FP slot with a format tag, so it holds either one
 * REAL64, one REAL32, or a REAL32_V2 pair (data[31:0]=lo, data[63:32]=hi).
 * f2ldm/f4ldm always deposit REAL32_V2.
 *
 * Fences: is_fp_fill_fence() in t9_cpu_pkg.sv stalls tld/tst/txf/tclr/tpush/
 * fdot4/fmvmul4/fpoly3/f2pack/f2unpk and any further fill while a private
 * (f2ldma) fill is in flight.  So an async fill only pays if it is issued
 * well ahead of the Temps op that consumes it.
 */
#define TP_OPR_TLD           0xAB /* FA <- T[A&7] (was 0xB0 = settimeslice) */
#define TP_OPR_TST           0xB1 /* T[A&7] <- FA                           */
#define TP_OPR_TXF           0xB2 /* swap FA <-> T[A&7]                     */
#define TP_OPR_TCLR          0xB3 /* T[A&7].valid <- 0                      */
#define TP_OPR_TPUSH         0xB4 /* T[A&7] <- REAL32_V2 {C,B}              */
#define TP_OPR_F2LDM         0xA2 /* T[B&7] <- {[A+4],[A]}                  */
#define TP_OPR_F4LDM         0xA9 /* T[B&7],T[B&7+1] <- 4 words at A        */
#define TP_OPR_F2LDMA        0xAF /* async f2ldm (private fill pool)        */
#define TP_OPR_F2STM         0x27 /* [A],[A+4] <- T[B&7]; pops A and B      */

/* T9 T&L / MXFP4 microcode that the Temps exist to feed (t9_cpu_pkg.sv). */
#define TP_OPR_FMAD          0xE8 /* FA <- FB*FA+FC                         */
#define TP_OPR_FDOT2         0xE9 /* v2 . v2 -> REAL32                      */
#define TP_OPR_FDOT4         0xEA /* {FA,FB} . T[A&7],T[A&7+1] -> REAL32    */
#define TP_OPR_FMVMUL4       0xEB /* row x vertex alias of FDOT4            */
#define TP_OPR_T9FRSQRT      0xEC /* rsqrt seed (NOT the TVX 0x1E6)         */
#define TP_OPR_T9FRSQRTN     0xED /* Newton rsqrt refine                    */
#define TP_OPR_F2PACK        0xEE /* FA <- REAL32_V2(lo=FB, hi=FA)          */
#define TP_OPR_F2UNPK        0xEF /* FA <- hi, push lo                      */
#define TP_OPR_FPOLY3        0xF6 /* Horner, coeffs T[A&7..A&7+2]           */

#define TP_OPR_START         0x1FF
#define TP_OPR_LDDEVID       0x17C

#endif /* _TRANSPUTER_OPCODES_H */
