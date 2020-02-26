/*************************************************************/
/*
 *  TRANSPUTER dummy assembler for TCC
 *
 */

#ifdef TARGET_DEFS_ONLY

#define CONFIG_TCC_ASM
#define NB_ASM_REGS 16

ST_FUNC void g(int c);
ST_FUNC void gen_le16(int c);
ST_FUNC void gen_le32(int c);

#include "transputer-asm.h"

/*************************************************************/
#else
/*************************************************************/

#include "tcc.h"

#ifndef TARGET_CPU_DEFAULT
#define TARGET_CPU_DEFAULT 800
#endif

#if TARGET_CPU_DEFAULT == 800
#define MASK_CPU_DEFAULT  MASK_CPU_CAPABILITIES_T800
#elif TARGET_CPU_DEFAULT == 805
#define MASK_CPU_DEFAULT  MASK_CPU_CAPABILITIES_T805
#elif TARGET_CPU_DEFAULT == 425
#define MASK_CPU_DEFAULT  MASK_CPU_CAPABILITIES_T425
#elif TARGET_CPU_DEFAULT == 450
#define MASK_CPU_DEFAULT  MASK_CPU_CAPABILITIES_T450
#elif TARGET_CPU_DEFAULT == 9000
#define MASK_CPU_DEFAULT  MASK_CPU_CAPABILITIES_T9000
#else
#error "Improper value for TARGET_CPU_DEFAULT.  Someone improved configure?"
#endif

/* Processor type to capabilities mask mapping */

#define MASK_CPU_CAPABILITIES_T800 \
  (MASK_HAVE_FPU | MASK_HAVE_FPENTRY)

#define MASK_CPU_CAPABILITIES_T805 \
  (MASK_HAVE_FPU | MASK_HAVE_FPENTRY | MASK_HAVE_POP)

#define MASK_CPU_CAPABILITIES_T425 \
  (MASK_HAVE_POP)

#define MASK_CPU_CAPABILITIES_T450 \
  (MASK_HAVE_POP | MASK_HAVE_GTU | MASK_HAVE_SIXTEEN \
   | MASK_HAVE_XTEND | MASK_HAVE_SLMUL)

#define MASK_CPU_CAPABILITIES_T9000 \
  (MASK_HAVE_FPU | MASK_HAVE_POP | MASK_HAVE_GTU \
   | MASK_HAVE_XTEND | MASK_HAVE_SIXTEEN)

#define MASK_CPU_CAPABILITIES_ALL \
  (0			\
   | MASK_HAVE_FPU	\
   | MASK_HAVE_FPENTRY	\
   | MASK_HAVE_FPGE	\
   | MASK_HAVE_POP	\
   | MASK_HAVE_GTU	\
   | MASK_HAVE_SIXTEEN	\
   | MASK_HAVE_XTEND	\
   | MASK_HAVE_SLMUL	\
  )

/* Masks for the -m switches */

#define MASK_USE_cmpqi				000000000001
#define MASK_DATASEG_PC_RELATIVE	000000000002
#define MASK_SHORT16				000000000004
#define MASK_SHORT32				000000000010
/* processor capabilities */
#define MASK_HAVE_FPU				000000001000
#define MASK_HAVE_FPENTRY			000000002000
#define MASK_HAVE_FPGE				000000004000
#define MASK_HAVE_POP				000000010000
#define MASK_HAVE_GTU				000000020000
#define MASK_HAVE_SIXTEEN			000000040000
#define MASK_HAVE_XTEND				000000100000
#define MASK_HAVE_SLMUL				000000200000

#define TARGET_SWITCHES_DEFAULT \
  (0 \
   | MASK_USE_cmpqi \
   | MASK_DATASEG_PC_RELATIVE \
   | MASK_CPU_DEFAULT \
  )

#define TARGET_SWITCHES \
  {{"no-cmpqi",				-MASK_USE_cmpqi},		\
   {"dataseg-pc-relative",	+MASK_DATASEG_PC_RELATIVE},	\
   {"dataseg-by-pointer",	-MASK_DATASEG_PC_RELATIVE},	\
   {"short16",				-MASK_SHORT32},			\
   {"short16",				+MASK_SHORT16},			\
   {"short32",				-MASK_SHORT16},			\
   {"short32",				+MASK_SHORT32},			\
   {"t800", 				-MASK_CPU_CAPABILITIES_ALL},	\
   {"t800", 				+MASK_CPU_CAPABILITIES_T800},	\
   {"t805", 				-MASK_CPU_CAPABILITIES_ALL},	\
   {"t805", 				+MASK_CPU_CAPABILITIES_T805},	\
   {"t425", 				-MASK_CPU_CAPABILITIES_ALL},	\
   {"t425", 				+MASK_CPU_CAPABILITIES_T425},	\
   {"t450", 				-MASK_CPU_CAPABILITIES_ALL},	\
   {"t450", 				+MASK_CPU_CAPABILITIES_T450},	\
   {"t9000", 				-MASK_CPU_CAPABILITIES_ALL},	\
   {"t9000", 				+MASK_CPU_CAPABILITIES_T9000},	\
   {"fpu",					+MASK_HAVE_FPU},		\
   {"no-fpu",				-MASK_HAVE_FPU},		\
   {"fpentry",				+MASK_HAVE_FPENTRY},		\
   {"no-fpentry",			-MASK_HAVE_FPENTRY},		\
   {"fpge",					+MASK_HAVE_FPGE},		\
   {"no-fpge",				-MASK_HAVE_FPGE},		\
   {"pop",					+MASK_HAVE_POP},		\
   {"no-pop",				-MASK_HAVE_POP},		\
   {"gtu",					+MASK_HAVE_GTU},		\
   {"no-gtu",				-MASK_HAVE_GTU},		\
   {"sixteen",				+MASK_HAVE_SIXTEEN},		\
   {"no-sixteen",			-MASK_HAVE_SIXTEEN},		\
   {"xtend",				+MASK_HAVE_XTEND},		\
   {"no-xtend",				-MASK_HAVE_XTEND},		\
   {"slmul",				+MASK_HAVE_SLMUL},		\
   {"no-slmul",				-MASK_HAVE_SLMUL},		\
   SUBTARGET_SWITCHES						\
   {"",  TARGET_SWITCHES_DEFAULT | SUBTARGET_SWITCHES_DEFAULT}}

static void asm_error(void)
{
    tcc_error("TRANSPUTER asm not implemented.");
}

/* XXX: make it faster ? */
ST_FUNC void g(int c)
{
    int ind1;
    if (nocode_wanted)
        return;
    ind1 = ind + 1;
    if (ind1 > cur_text_section->data_allocated)
        section_realloc(cur_text_section, ind1);
    cur_text_section->data[ind] = c;
    ind = ind1;
}

ST_FUNC void gen_le16 (int i)
{
    g(i);
    g(i>>8);
}

ST_FUNC void gen_le32 (int i)
{
    gen_le16(i);
    gen_le16(i>>16);
}

ST_FUNC void gen_expr32(ExprValue *pe)
{
    gen_le32(pe->v);
}

ST_FUNC void asm_opcode(TCCState *s1, int opcode)
{
    asm_error();
}

ST_FUNC void subst_asm_operand(CString *add_str, SValue *sv, int modifier)
{
    asm_error();
}

/* generate prolog and epilog code for asm statement */
ST_FUNC void asm_gen_code(ASMOperand *operands, int nb_operands,
                         int nb_outputs, int is_output,
                         uint8_t *clobber_regs,
                         int out_reg)
{
}

ST_FUNC void asm_compute_constraints(ASMOperand *operands,
                                    int nb_operands, int nb_outputs,
                                    const uint8_t *clobber_regs,
                                    int *pout_reg)
{
}

ST_FUNC void asm_clobber(uint8_t *clobber_regs, const char *str)
{
    asm_error();
}

ST_FUNC int asm_parse_regvar (int t)
{
    asm_error();
    return -1;
}

/*************************************************************/
#endif /* ndef TARGET_DEFS_ONLY */