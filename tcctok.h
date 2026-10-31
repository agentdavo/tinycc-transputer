/*********************************************************************/
/* keywords */
     DEF(TOK_IF, "if")
     DEF(TOK_ELSE, "else")
     DEF(TOK_WHILE, "while")
     DEF(TOK_FOR, "for")
     DEF(TOK_DO, "do")
     DEF(TOK_CONTINUE, "continue")
     DEF(TOK_BREAK, "break")
     DEF(TOK_RETURN, "return")
     DEF(TOK_GOTO, "goto")
     DEF(TOK_SWITCH, "switch")
     DEF(TOK_CASE, "case")
     DEF(TOK_DEFAULT, "default")
     DEF(TOK_ASM1, "asm")
     DEF(TOK_ASM2, "__asm")
     DEF(TOK_ASM3, "__asm__")

     DEF(TOK_EXTERN, "extern")
     DEF(TOK_STATIC, "static")
     DEF(TOK_UNSIGNED, "unsigned")
     DEF(TOK__Atomic, "_Atomic")
     DEF(TOK_CONST1, "const")
     DEF(TOK_CONST2, "__const") /* gcc keyword */
     DEF(TOK_CONST3, "__const__") /* gcc keyword */
     DEF(TOK_VOLATILE1, "volatile")
     DEF(TOK_VOLATILE2, "__volatile") /* gcc keyword */
     DEF(TOK_VOLATILE3, "__volatile__") /* gcc keyword */
     DEF(TOK_REGISTER, "register")
     DEF(TOK_SIGNED1, "signed")
     DEF(TOK_SIGNED2, "__signed") /* gcc keyword */
     DEF(TOK_SIGNED3, "__signed__") /* gcc keyword */
     DEF(TOK_AUTO, "auto")
#ifdef TCC_TARGET_TRANSPUTER
     DEF(TOK_AUTO_TYPE, "__auto_type") /* GNU inferred type */
#endif
     DEF(TOK_INLINE1, "inline")
     DEF(TOK_INLINE2, "__inline") /* gcc keyword */
     DEF(TOK_INLINE3, "__inline__") /* gcc keyword */
     DEF(TOK_RESTRICT1, "restrict")
     DEF(TOK_RESTRICT2, "__restrict")
     DEF(TOK_RESTRICT3, "__restrict__")
     DEF(TOK_EXTENSION, "__extension__") /* gcc keyword */
     DEF(TOK_THREAD_LOCAL, "_Thread_local") /* C11 thread-local storage */
     DEF(TOK___thread, "__thread") /* GCC thread-local storage extension */

     DEF(TOK_GENERIC, "_Generic")
     DEF(TOK_STATIC_ASSERT, "_Static_assert")

     DEF(TOK_VOID, "void")
     DEF(TOK_CHAR, "char")
     DEF(TOK_INT, "int")
     DEF(TOK_FLOAT, "float")
     DEF(TOK_DOUBLE, "double")
     DEF(TOK_BOOL, "_Bool")
     DEF(TOK_COMPLEX, "_Complex")
     DEF(TOK_SHORT, "short")
     DEF(TOK_LONG, "long")
     DEF(TOK_STRUCT, "struct")
     DEF(TOK_UNION, "union")
     DEF(TOK_TYPEDEF, "typedef")
     DEF(TOK_ENUM, "enum")
     DEF(TOK_SIZEOF, "sizeof")
     DEF(TOK_ATTRIBUTE1, "__attribute")
     DEF(TOK_ATTRIBUTE2, "__attribute__")
     DEF(TOK_ALIGNOF1, "__alignof")
     DEF(TOK_ALIGNOF2, "__alignof__")
     DEF(TOK_ALIGNOF3, "_Alignof")
     DEF(TOK_ALIGNAS, "_Alignas")
     DEF(TOK_TYPEOF1, "typeof")
     DEF(TOK_TYPEOF2, "__typeof")
     DEF(TOK_TYPEOF3, "__typeof__")
     DEF(TOK_LABEL, "__label__")

/*********************************************************************/
/* the following are not keywords. They are included to ease parsing */
/* preprocessor only */
     DEF(TOK_DEFINE, "define")
     DEF(TOK_INCLUDE, "include")
     DEF(TOK_INCLUDE_NEXT, "include_next")
     DEF(TOK_IFDEF, "ifdef")
     DEF(TOK_IFNDEF, "ifndef")
     DEF(TOK_ELIF, "elif")
     DEF(TOK_ENDIF, "endif")
     DEF(TOK_DEFINED, "defined")
     DEF(TOK_UNDEF, "undef")
     DEF(TOK_ERROR, "error")
     DEF(TOK_WARNING, "warning")
     DEF(TOK_LINE, "line")
     DEF(TOK_PRAGMA, "pragma")
     DEF(TOK___LINE__, "__LINE__")
     DEF(TOK___FILE__, "__FILE__")
     DEF(TOK___DATE__, "__DATE__")
     DEF(TOK___TIME__, "__TIME__")
     DEF(TOK___FUNCTION__, "__FUNCTION__")
     DEF(TOK___VA_ARGS__, "__VA_ARGS__")
     DEF(TOK___COUNTER__, "__COUNTER__")
     DEF(TOK___HAS_INCLUDE, "__has_include")
     DEF(TOK___HAS_INCLUDE_NEXT, "__has_include_next")

/* special identifiers */
     DEF(TOK___FUNC__, "__func__")

/* special floating point values */
     DEF(TOK___NAN__, "__nan__")
     DEF(TOK___SNAN__, "__snan__")
     DEF(TOK___INF__, "__inf__")

/* attribute identifiers */
/* XXX: handle all tokens generically since speed is not critical */
     DEF(TOK_SECTION1, "section")
     DEF(TOK_SECTION2, "__section__")
     DEF(TOK_ALIGNED1, "aligned")
     DEF(TOK_ALIGNED2, "__aligned__")
     DEF(TOK_PACKED1, "packed")
     DEF(TOK_PACKED2, "__packed__")
#ifdef TCC_TARGET_TRANSPUTER
     DEF(TOK_TRANSPARENT_UNION1, "transparent_union")
     DEF(TOK_TRANSPARENT_UNION2, "__transparent_union__")
#endif
     DEF(TOK_WEAK1, "weak")
     DEF(TOK_WEAK2, "__weak__")
     DEF(TOK_ALIAS1, "alias")
     DEF(TOK_ALIAS2, "__alias__")
     DEF(TOK_USED1, "used")
     DEF(TOK_USED2, "__used__")
     DEF(TOK_UNUSED1, "unused")
     DEF(TOK_UNUSED2, "__unused__")
     DEF(TOK_FORMAT1, "format")
     DEF(TOK_FORMAT2, "__format__")
     DEF(TOK_NODEBUG1, "nodebug")
     DEF(TOK_NODEBUG2, "__nodebug__")
     DEF(TOK_CDECL1, "cdecl")
     DEF(TOK_CDECL2, "__cdecl")
     DEF(TOK_CDECL3, "__cdecl__")
     DEF(TOK_STDCALL1, "stdcall")
     DEF(TOK_STDCALL2, "__stdcall")
     DEF(TOK_STDCALL3, "__stdcall__")
     DEF(TOK_FASTCALL1, "fastcall")
     DEF(TOK_FASTCALL2, "__fastcall")
     DEF(TOK_FASTCALL3, "__fastcall__")
     DEF(TOK_THISCALL1, "thiscall")
     DEF(TOK_THISCALL2, "__thiscall")
     DEF(TOK_THISCALL3, "__thiscall__")
     DEF(TOK_REGPARM1, "regparm")
     DEF(TOK_REGPARM2, "__regparm__")
     DEF(TOK_CLEANUP1, "cleanup")
     DEF(TOK_CLEANUP2, "__cleanup__")
     DEF(TOK_CONSTRUCTOR1, "constructor")
     DEF(TOK_CONSTRUCTOR2, "__constructor__")
     DEF(TOK_DESTRUCTOR1, "destructor")
     DEF(TOK_DESTRUCTOR2, "__destructor__")
     DEF(TOK_ALWAYS_INLINE1, "always_inline")
     DEF(TOK_ALWAYS_INLINE2, "__always_inline__")
#ifdef TCC_TARGET_TRANSPUTER
     DEF(TOK_NOINLINE1, "noinline")
     DEF(TOK_NOINLINE2, "__noinline__")
#else
     DEF(TOK_NOINLINE, "__noinline__")
#endif
     DEF(TOK_PURE1, "pure")
     DEF(TOK_PURE2, "__pure__")

     DEF(TOK_MODE, "__mode__")
     DEF(TOK_MODE_QI, "__QI__")
     DEF(TOK_MODE_DI, "__DI__")
     DEF(TOK_MODE_HI, "__HI__")
     DEF(TOK_MODE_SI, "__SI__")
     DEF(TOK_MODE_word, "__word__")

     DEF(TOK_DLLEXPORT, "dllexport")
     DEF(TOK_DLLIMPORT, "dllimport")
     DEF(TOK_NODECORATE, "nodecorate")
     DEF(TOK_NORETURN1, "noreturn")
     DEF(TOK_NORETURN2, "__noreturn__")
     DEF(TOK_NORETURN3, "_Noreturn")
     DEF(TOK_VISIBILITY1, "visibility")
     DEF(TOK_VISIBILITY2, "__visibility__")

     DEF(TOK_builtin_types_compatible_p, "__builtin_types_compatible_p")
     DEF(TOK_builtin_choose_expr, "__builtin_choose_expr")
     DEF(TOK_builtin_constant_p, "__builtin_constant_p")
#ifdef TCC_TARGET_TRANSPUTER
     DEF(TOK_builtin_object_size, "__builtin_object_size")
#endif
     DEF(TOK_builtin_frame_address, "__builtin_frame_address")
     DEF(TOK_builtin_return_address, "__builtin_return_address")
     DEF(TOK_builtin_expect, "__builtin_expect")
#ifdef TCC_TARGET_TRANSPUTER
     DEF(TOK_builtin_clz, "__builtin_clz")
     DEF(TOK_builtin_clzl, "__builtin_clzl")
     DEF(TOK_builtin_clzll, "__builtin_clzll")
     DEF(TOK_builtin_strlen, "__builtin_strlen")
     DEF(TOK_builtin_strcmp, "__builtin_strcmp")
     DEF(TOK_builtin_strncmp, "__builtin_strncmp")
     DEF(TOK_builtin_add_overflow, "__builtin_add_overflow")
     DEF(TOK_builtin_sub_overflow, "__builtin_sub_overflow")
     DEF(TOK_builtin_mul_overflow, "__builtin_mul_overflow")
#endif
     DEF(TOK_builtin_unreachable, "__builtin_unreachable")
#if defined TCC_TARGET_TRANSPUTER
     DEF(TOK_builtin_popcount, "__builtin_popcount")
     DEF(TOK_builtin_popcountl, "__builtin_popcountl")
#endif
     /*DEF(TOK_builtin_va_list, "__builtin_va_list")*/
#if defined TCC_TARGET_PE && defined TCC_TARGET_X86_64
     DEF(TOK_builtin_va_start, "__builtin_va_start")
#elif defined TCC_TARGET_X86_64
     DEF(TOK_builtin_va_arg_types, "__builtin_va_arg_types")
#elif defined TCC_TARGET_ARM64
     DEF(TOK_builtin_va_start, "__builtin_va_start")
     DEF(TOK_builtin_va_arg, "__builtin_va_arg")
#elif defined TCC_TARGET_RISCV64
     DEF(TOK_builtin_va_start, "__builtin_va_start")
#endif

/* atomic operations */
#define DEF_ATOMIC(ID) DEF(TOK_##__##ID, "__"#ID)
     DEF_ATOMIC(atomic_store)
     DEF_ATOMIC(atomic_load)
     DEF_ATOMIC(atomic_exchange)
     DEF_ATOMIC(atomic_compare_exchange)
     DEF_ATOMIC(atomic_fetch_add)
     DEF_ATOMIC(atomic_fetch_sub)
     DEF_ATOMIC(atomic_fetch_or)
     DEF_ATOMIC(atomic_fetch_xor)
     DEF_ATOMIC(atomic_fetch_and)
     DEF_ATOMIC(atomic_fetch_nand)
     DEF_ATOMIC(atomic_add_fetch)
     DEF_ATOMIC(atomic_sub_fetch)
     DEF_ATOMIC(atomic_or_fetch)
     DEF_ATOMIC(atomic_xor_fetch)
     DEF_ATOMIC(atomic_and_fetch)
     DEF_ATOMIC(atomic_nand_fetch)

/* pragma */
     DEF(TOK_pack, "pack")
#if !defined(TCC_TARGET_I386) && !defined(TCC_TARGET_X86_64) && \
    !defined(TCC_TARGET_ARM) && !defined(TCC_TARGET_ARM64) && \
    !defined(TCC_TARGET_RISCV64)
     /* already defined for assembler */
     DEF(TOK_ASM_push, "push")
     DEF(TOK_ASM_pop, "pop")
#endif
     DEF(TOK_comment, "comment")
     DEF(TOK_lib, "lib")
     DEF(TOK_push_macro, "push_macro")
     DEF(TOK_pop_macro, "pop_macro")
     DEF(TOK_once, "once")
     DEF(TOK_option, "option")

/* builtin functions or variables */
#ifndef TCC_ARM_EABI
     DEF(TOK_memcpy, "memcpy")
     DEF(TOK_memmove, "memmove")
     DEF(TOK_memset, "memset")
     DEF(TOK___divdi3, "__divdi3")
     DEF(TOK___moddi3, "__moddi3")
     DEF(TOK___udivdi3, "__udivdi3")
     DEF(TOK___umoddi3, "__umoddi3")
     DEF(TOK___ashrdi3, "__ashrdi3")
     DEF(TOK___lshrdi3, "__lshrdi3")
     DEF(TOK___ashldi3, "__ashldi3")
     DEF(TOK___floatundisf, "__floatundisf")
     DEF(TOK___floatundidf, "__floatundidf")
# ifndef TCC_ARM_VFP
     DEF(TOK___floatundixf, "__floatundixf")
     DEF(TOK___fixunsxfdi, "__fixunsxfdi")
# endif
     DEF(TOK___fixunssfdi, "__fixunssfdi")
     DEF(TOK___fixunsdfdi, "__fixunsdfdi")
#endif

#if defined TCC_TARGET_ARM
# ifdef TCC_ARM_EABI
     DEF(TOK_memcpy, "__aeabi_memcpy")
     DEF(TOK_memmove, "__aeabi_memmove")
     DEF(TOK_memmove4, "__aeabi_memmove4")
     DEF(TOK_memmove8, "__aeabi_memmove8")
     DEF(TOK_memset, "__aeabi_memset")
     DEF(TOK___aeabi_ldivmod, "__aeabi_ldivmod")
     DEF(TOK___aeabi_uldivmod, "__aeabi_uldivmod")
     DEF(TOK___aeabi_idivmod, "__aeabi_idivmod")
     DEF(TOK___aeabi_uidivmod, "__aeabi_uidivmod")
     DEF(TOK___divsi3, "__aeabi_idiv")
     DEF(TOK___udivsi3, "__aeabi_uidiv")
     DEF(TOK___floatdisf, "__aeabi_l2f")
     DEF(TOK___floatdidf, "__aeabi_l2d")
     DEF(TOK___fixsfdi, "__aeabi_f2lz")
     DEF(TOK___fixdfdi, "__aeabi_d2lz")
     DEF(TOK___ashrdi3, "__aeabi_lasr")
     DEF(TOK___lshrdi3, "__aeabi_llsr")
     DEF(TOK___ashldi3, "__aeabi_llsl")
     DEF(TOK___floatundisf, "__aeabi_ul2f")
     DEF(TOK___floatundidf, "__aeabi_ul2d")
     DEF(TOK___fixunssfdi, "__aeabi_f2ulz")
     DEF(TOK___fixunsdfdi, "__aeabi_d2ulz")
# else
     DEF(TOK___modsi3, "__modsi3")
     DEF(TOK___umodsi3, "__umodsi3")
     DEF(TOK___divsi3, "__divsi3")
     DEF(TOK___udivsi3, "__udivsi3")
     DEF(TOK___floatdisf, "__floatdisf")
     DEF(TOK___floatdidf, "__floatdidf")
#  ifndef TCC_ARM_VFP
     DEF(TOK___floatdixf, "__floatdixf")
     DEF(TOK___fixunssfsi, "__fixunssfsi")
     DEF(TOK___fixunsdfsi, "__fixunsdfsi")
     DEF(TOK___fixunsxfsi, "__fixunsxfsi")
     DEF(TOK___fixxfdi, "__fixxfdi")
#  endif
     DEF(TOK___fixsfdi, "__fixsfdi")
     DEF(TOK___fixdfdi, "__fixdfdi")
# endif
#endif

#if defined TCC_TARGET_C67
     DEF(TOK__divi, "_divi")
     DEF(TOK__divu, "_divu")
     DEF(TOK__divf, "_divf")
     DEF(TOK__divd, "_divd")
     DEF(TOK__remi, "_remi")
     DEF(TOK__remu, "_remu")
#endif

#if defined TCC_TARGET_I386
     DEF(TOK___fixsfdi, "__fixsfdi")
     DEF(TOK___fixdfdi, "__fixdfdi")
     DEF(TOK___fixxfdi, "__fixxfdi")
#endif
#if defined TCC_TARGET_X86_64
     DEF(TOK___fixxfdi, "__fixxfdi")
#endif

     DEF(TOK_alloca, "alloca")

#if defined TCC_TARGET_PE
     DEF(TOK___chkstk, "__chkstk")
     DEF(TOK___tls_index, "__tls_index")
#endif
#ifdef TCC_TARGET_ARM64
     DEF(TOK___arm64_clear_cache, "__arm64_clear_cache")
#endif
#ifdef TCC_TARGET_RISCV64
     DEF(TOK___riscv64_clear_cache, "__riscv64_clear_cache")
#endif
#if defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64
     DEF(TOK___addtf3, "__addtf3")
     DEF(TOK___subtf3, "__subtf3")
     DEF(TOK___multf3, "__multf3")
     DEF(TOK___divtf3, "__divtf3")
     DEF(TOK___extendsftf2, "__extendsftf2")
     DEF(TOK___extenddftf2, "__extenddftf2")
     DEF(TOK___trunctfsf2, "__trunctfsf2")
     DEF(TOK___trunctfdf2, "__trunctfdf2")
     DEF(TOK___negtf2, "__negtf2")
     DEF(TOK___fixtfsi, "__fixtfsi")
     DEF(TOK___fixtfdi, "__fixtfdi")
     DEF(TOK___fixunstfsi, "__fixunstfsi")
     DEF(TOK___fixunstfdi, "__fixunstfdi")
     DEF(TOK___floatsitf, "__floatsitf")
     DEF(TOK___floatditf, "__floatditf")
     DEF(TOK___floatunsitf, "__floatunsitf")
     DEF(TOK___floatunditf, "__floatunditf")
     DEF(TOK___eqtf2, "__eqtf2")
     DEF(TOK___netf2, "__netf2")
     DEF(TOK___lttf2, "__lttf2")
     DEF(TOK___letf2, "__letf2")
     DEF(TOK___gttf2, "__gttf2")
     DEF(TOK___getf2, "__getf2")
#endif

#ifdef TCC_TARGET_TRANSPUTER
/* Transputer intrinsics - hardware operations */
     /* Channel I/O (DMA) */
     DEF(TOK___out, "__out")
     DEF(TOK___in, "__in")
     DEF(TOK___outbyte, "__outbyte")
     DEF(TOK___outword, "__outword")
     DEF(TOK___tin, "__tin")
     DEF(TOK___resetch, "__resetch")
     /* Memory operations */
     DEF(TOK___move, "__move")
     DEF(TOK___move2dinit, "__move2dinit")
     DEF(TOK___move2dall, "__move2dall")
     DEF(TOK___move2dnonzero, "__move2dnonzero")
     DEF(TOK___move2dzero, "__move2dzero")
     /* T805 CRC instructions */
     DEF(TOK___crcbyte, "__crcbyte")
     DEF(TOK___crcword, "__crcword")
     /* Timer operations */
     DEF(TOK___ldtimer, "__ldtimer")
     DEF(TOK___sttimer, "__sttimer")
     /* Process/scheduler operations */
     DEF(TOK___mint, "__mint")
     DEF(TOK___ldpri, "__ldpri")
     DEF(TOK___startp, "__startp")
     DEF(TOK___endp, "__endp")
     DEF(TOK___runp, "__runp")
     DEF(TOK___stopp, "__stopp")
     /* ALT operations */
     DEF(TOK___alt, "__alt")
     DEF(TOK___altwt, "__altwt")
     DEF(TOK___altend, "__altend")
     DEF(TOK___talt, "__talt")
     DEF(TOK___taltwt, "__taltwt")
     DEF(TOK___enbc, "__enbc")
     DEF(TOK___enbs, "__enbs")
     DEF(TOK___enbt, "__enbt")
     DEF(TOK___disc, "__disc")
     DEF(TOK___diss, "__diss")
     DEF(TOK___dist, "__dist")
     /* Scheduler queue registers (saveh/savel/sthf/sthb/stlf/stlb) */
     DEF(TOK___saveh, "__saveh")
     DEF(TOK___savel, "__savel")
     DEF(TOK___sthf, "__sthf")
     DEF(TOK___sthb, "__sthb")
     DEF(TOK___stlf, "__stlf")
     DEF(TOK___stlb, "__stlb")
     /* Workspace operations */
     DEF(TOK___gajw, "__gajw")
     DEF(TOK___wcnt, "__wcnt")
     /* Error handling */
     DEF(TOK___seterr, "__seterr")
     DEF(TOK___testerr, "__testerr")
     DEF(TOK___sethalterr, "__sethalterr")
     DEF(TOK___clrhalterr, "__clrhalterr")
     DEF(TOK___testhalterr, "__testhalterr")
     DEF(TOK___stoperr, "__stoperr")
     DEF(TOK___testpranal, "__testpranal")
     /* Bit manipulation */
     DEF(TOK___norm, "__norm")
     DEF(TOK___bitcnt, "__bitcnt")
     DEF(TOK___bitrevword, "__bitrevword")
     DEF(TOK___bitrevnbits, "__bitrevnbits")
     /* Bounds checking */
     DEF(TOK___csub0, "__csub0")
     /* Fractional multiply */
     DEF(TOK___fmul, "__fmul")
     /* Device identification */
     DEF(TOK___lddevid, "__lddevid")
     /* FPU square root / absolute value.  Spelled as explicit builtins
        rather than hooking sqrtf()/fabsf(): sqrt() owes errno and a domain
        error on negative input, which the bare instruction does not do. */
     DEF(TOK___fpsqrt, "__fpsqrt")
     DEF(TOK___fpabs, "__fpabs")
     /* FPU error flag, rounding to an integer, remainder (T800 and later:
        the T425 and T414 have no FPU) */
     DEF(TOK___fpchkerr, "__fpchkerr")
     DEF(TOK___fptesterr, "__fptesterr")
     DEF(TOK___fpseterr, "__fpseterr")
     DEF(TOK___fpclrerr, "__fpclrerr")
     DEF(TOK___fpint, "__fpint")
     DEF(TOK___fpintrp, "__fpintrp")
     DEF(TOK___fpintrm, "__fpintrm")
     DEF(TOK___fpintrz, "__fpintrz")
     DEF(TOK___fprem, "__fprem")
     /* TVX: the __bf16 storage type (x86-64 psABI / ARM ACLE spelling) */
     DEF(TOK___bf16, "__bf16")
     /* IEEE-754 binary16, spelled as the ARM ACLE storage-only type.
        Deliberately NOT spelled _Float16: that name is C23 Annex H and
        requires in-type arithmetic, which VT_BTYPE has no room to express
        separately from __fp16.  See the comment in gen_op(). */
     DEF(TOK___fp16, "__fp16")
     /* C23 Annex H / TS 18661-3 binary16.  Same format as __fp16 but a real
        arithmetic type: an operation on it yields _Float16, not float. */
     DEF(TOK__Float16, "_Float16")
     /* TVX: bfloat16 */
     DEF(TOK___bf16tof32, "__bf16tof32")
     DEF(TOK___f32tobf16, "__f32tobf16")
     DEF(TOK___bf16pack, "__bf16pack")
     DEF(TOK___bf16unpkh, "__bf16unpkh")
     /* TVX: MXFP4 (OCP microscaling E2M1 + E8M0) */
     DEF(TOK___mxfp4tof32, "__mxfp4tof32")
     DEF(TOK___f32tomxfp4, "__f32tomxfp4")
     DEF(TOK___mxe8m0tof32, "__mxe8m0tof32")
     DEF(TOK___mxe8m0of, "__mxe8m0of")
     /* TVX: block dot products */
     DEF(TOK___mxfp4dot, "__mxfp4dot")
     DEF(TOK___bf16dot, "__bf16dot")
     DEF(TOK___mxfp4gemv, "__mxfp4gemv")
     /* TVX: sub-word SIMD */
     DEF(TOK___mxfp4unpk8, "__mxfp4unpk8")
     DEF(TOK___f32tomxfp4x8, "__f32tomxfp4x8")
     DEF(TOK___bf16mul2, "__bf16mul2")
     DEF(TOK___bf16add2, "__bf16add2")
     /* TVX: vector kernels for inference */
     DEF(TOK___vsetlen, "__vsetlen")
     DEF(TOK___mxfp4gemvbf, "__mxfp4gemvbf")
     DEF(TOK___vadd, "__vadd")
     DEF(TOK___vmul, "__vmul")
     DEF(TOK___vsilu, "__vsilu")
     DEF(TOK___vrmsnorm, "__vrmsnorm")
     DEF(TOK___vsoftmax, "__vsoftmax")
     /* TVX: IEEE binary16 and GGUF Q8_0 */
     DEF(TOK___f16tof32, "__f16tof32")
     DEF(TOK___f32tof16, "__f32tof16")
     DEF(TOK___q8dot, "__q8dot")
     DEF(TOK___q8dotbf, "__q8dotbf")
     /* TVX: GGUF i-quants */
     DEF(TOK___iq1mdot, "__iq1mdot")
     DEF(TOK___iq2xxsdot, "__iq2xxsdot")
     DEF(TOK___iq3xxsdot, "__iq3xxsdot")
     DEF(TOK___qsetfmt, "__qsetfmt")
     DEF(TOK___qdot, "__qdot")
     /* TVX: block rotation (BRQ) */
     DEF(TOK___vhad32, "__vhad32")
     DEF(TOK___mxqrot, "__mxqrot")
     DEF(TOK___mxqrotbf, "__mxqrotbf")
     /* FTP: transform & project (T&L geometry) */
     DEF(TOK___ftsetvp, "__ftsetvp")
     DEF(TOK___ftransp, "__ftransp")
     DEF(TOK___ftranspb, "__ftranspb")
     DEF(TOK___fsincos, "__fsincos")
     DEF(TOK___frsqrtb, "__frsqrtb")
     DEF(TOK___frecipb, "__frecipb")
     DEF(TOK___fsincosb, "__fsincosb")
     DEF(TOK___vsetstride, "__vsetstride")
     DEF(TOK___ftcull, "__ftcull")
     DEF(TOK___tskin, "__tskin")
     DEF(TOK___vsetfmt, "__vsetfmt")
     DEF(TOK___ftsetlit, "__ftsetlit")
     DEF(TOK___flitb, "__flitb")
     DEF(TOK___qgemm, "__qgemm")
     DEF(TOK___vblend8, "__vblend8")
     DEF(TOK___vfill, "__vfill")
     DEF(TOK___vfill2d, "__vfill2d")
     DEF(TOK___csumb, "__csumb")
     DEF(TOK___vblend2d, "__vblend2d")
     DEF(TOK___stvxstate, "__stvxstate")
     DEF(TOK___csumcpy, "__csumcpy")
     DEF(TOK___vq8k, "__vq8k")
     DEF(TOK___vq80, "__vq80")
     DEF(TOK___vcvt, "__vcvt")
     DEF(TOK___vxfer, "__vxfer")
     /* T9000 configuration space and virtual channel processor */
     DEF(TOK___ldconf, "__ldconf")
     DEF(TOK___stconf, "__stconf")
     DEF(TOK___initvlcb, "__initvlcb")
     DEF(TOK___sethdr, "__sethdr")
     DEF(TOK___writehdr, "__writehdr")
     DEF(TOK___readhdr, "__readhdr")
     DEF(TOK___setchmode, "__setchmode")
     DEF(TOK___stopch, "__stopch")
     DEF(TOK___swapbfr, "__swapbfr")
     DEF(TOK___vin, "__vin")
     DEF(TOK___vout, "__vout")
     DEF(TOK___ldchstatus, "__ldchstatus")
     DEF(TOK___chantype, "__chantype")
     /* T9000 semaphores (ISM 8.6) and resource channels (ISM 8.8, 12.7);
        not __wait/__signal, which kernel and libc code use as names */
     DEF(TOK___semwait, "__semwait")
     DEF(TOK___semsignal, "__semsignal")
     DEF(TOK___mkrc, "__mkrc")
     DEF(TOK___unmkrc, "__unmkrc")
     DEF(TOK___grant, "__grant")
     DEF(TOK___enbg, "__enbg")
     DEF(TOK___disg, "__disg")
     DEF(TOK___irdsq, "__irdsq")
     DEF(TOK___erdsq, "__erdsq")
     DEF(TOK___ldresptr, "__ldresptr")
     DEF(TOK___stresptr, "__stresptr")
     /* T9 FPU Temps T0-T7 (ip/cpu/ISA.md) */
     DEF(TOK___tld, "__tld")
     DEF(TOK___tst, "__tst")
     DEF(TOK___tclr, "__tclr")
     DEF(TOK___f2ldm, "__f2ldm")
     DEF(TOK___f4ldm, "__f4ldm")
     DEF(TOK___f2ldma, "__f2ldma")
     DEF(TOK___f2stm, "__f2stm")
     DEF(TOK___fmvmul4, "__fmvmul4")
     DEF(TOK___fmvmul4st, "__fmvmul4st")
     DEF(TOK___fpoly3, "__fpoly3")
#endif

/* bound checking symbols */
#ifdef CONFIG_TCC_BCHECK
     DEF(TOK___bound_ptr_add, "__bound_ptr_add")
     DEF(TOK___bound_ptr_indir1, "__bound_ptr_indir1")
     DEF(TOK___bound_ptr_indir2, "__bound_ptr_indir2")
     DEF(TOK___bound_ptr_indir4, "__bound_ptr_indir4")
     DEF(TOK___bound_ptr_indir8, "__bound_ptr_indir8")
     DEF(TOK___bound_ptr_indir12, "__bound_ptr_indir12")
     DEF(TOK___bound_ptr_indir16, "__bound_ptr_indir16")
     DEF(TOK___bound_main_arg, "__bound_main_arg")
     DEF(TOK___bound_local_new, "__bound_local_new")
     DEF(TOK___bound_local_delete, "__bound_local_delete")
     DEF(TOK___bound_setjmp, "__bound_setjmp")
     DEF(TOK___bound_longjmp, "__bound_longjmp")
     DEF(TOK___bound_new_region, "__bound_new_region")
# ifdef TCC_TARGET_PE
#  ifdef TCC_TARGET_X86_64
     DEF(TOK___bound_alloca_nr, "__bound_alloca_nr")
#  endif
# else
     DEF(TOK_sigsetjmp, "sigsetjmp")
     DEF(TOK___sigsetjmp, "__sigsetjmp")
     DEF(TOK_siglongjmp, "siglongjmp")
# endif
     DEF(TOK_setjmp, "setjmp")
     DEF(TOK__setjmp, "_setjmp")
     DEF(TOK_longjmp, "longjmp")
#endif


/*********************************************************************/
/* Tiny Assembler */
#define DEF_ASM(x) DEF(TOK_ASM_ ## x, #x)
#define DEF_ASMDIR(x) DEF(TOK_ASMDIR_ ## x, "." #x)
#define TOK_ASM_int TOK_INT

#define TOK_ASMDIR_FIRST TOK_ASMDIR_byte
#ifdef TCC_TARGET_TRANSPUTER
#define TOK_ASMDIR_LAST TOK_ASMDIR_incbin
#else
#define TOK_ASMDIR_LAST TOK_ASMDIR_section
#endif

 DEF_ASMDIR(byte)       /* must be first directive */
 DEF_ASMDIR(word)
 DEF_ASMDIR(align)
 DEF_ASMDIR(balign)
 DEF_ASMDIR(p2align)
 DEF_ASMDIR(set)
 DEF_ASMDIR(skip)
 DEF_ASMDIR(space)
 DEF_ASMDIR(string)
 DEF_ASMDIR(asciz)
 DEF_ASMDIR(ascii)
 DEF_ASMDIR(file)
 DEF_ASMDIR(globl)
 DEF_ASMDIR(global)
 DEF_ASMDIR(weak)
 DEF_ASMDIR(hidden)
 DEF_ASMDIR(ident)
 DEF_ASMDIR(size)
 DEF_ASMDIR(type)
 DEF_ASMDIR(text)
 DEF_ASMDIR(data)
 DEF_ASMDIR(bss)
 DEF_ASMDIR(previous)
 DEF_ASMDIR(pushsection)
 DEF_ASMDIR(popsection)
 DEF_ASMDIR(fill)
 DEF_ASMDIR(rept)
 DEF_ASMDIR(endr)
 DEF_ASMDIR(org)
 DEF_ASMDIR(quad)
#if PTR_SIZE == 4
 DEF_ASMDIR(code16)
 DEF_ASMDIR(code32)
#else
 DEF_ASMDIR(code64)
#endif
#if defined(TCC_TARGET_RISCV64)
 DEF_ASMDIR(option)
#endif
 DEF_ASMDIR(short)
 DEF_ASMDIR(long)
 DEF_ASMDIR(int)
 DEF_ASMDIR(symver)
 DEF_ASMDIR(reloc)
#ifdef TCC_TARGET_TRANSPUTER
 DEF_ASMDIR(section)
 DEF_ASMDIR(incbin)    /* must be last directive */
#else
 DEF_ASMDIR(section)    /* must be last directive */
#endif

#if defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64
#include "i386-tok.h"
#endif

#if defined TCC_TARGET_ARM
#include "arm-tok.h"
#endif

#if defined TCC_TARGET_ARM64
#include "arm64-tok.h"
#endif

#if defined TCC_TARGET_RISCV64
#include "riscv64-tok.h"
#endif
