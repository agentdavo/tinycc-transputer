#ifdef TARGET_DEFS_ONLY

#include "tcc.h"
#include <assert.h>

/* number of available registers */
#define NB_REGS 10

#define RC_INT           /* generic integer register */
#define RC_FLOAT         /* generic float register   */

/* 32-bit integer stack registers           */
#define RC_AREG     (0)  /* Areg            */
#define RC_BREG     (1)  /* Breg            */
#define RC_CREG     (2)  /* Creg            */

/* 64-bit floating point stack              */
#define RC_FAREG    (3)  /* FAreg           */
#define RC_FBREG    (4)  /* FBreg           */
#define RC_FCREG    (5)  /* FCreg           */

/* workspace and intruction pointers        */
#define RC_WPTR     (6)  /* WPtr            */
#define RC_IPTR     ()   /* IPtr            */

/* queue pointers                           */
#define RC_BPTR     ()   /* BptrReg         */
#define RC_FPTR     ()   /* FptrReg         */

/* return registers for function */
#define REG_IRET TREG_R0    /* single word int return register */
#define REG_LRET TREG_R1    /* second word return register (for long long) */
#define REG_FRET TREG_F0    /* float return register */


/* pretty names for the registers */
enum {
	

};

/******************************************************/
#else /* ! TARGET_DEFS_ONLY */
/******************************************************/

#define USING_GLOBALS
#include "tcc.h"


ST_DATA const int reg_classes[NB_REGS] = {
	
};


/******************************************************/
/* opcode definitions */

enum TPOPCodes {
#define OP(name, str, n) TP_OP_ ## name = n,
#include "tranpsuter-opcodes.h"
#undef OP
};

char *tp_opcodes_str[] = {
#define OP(name, str, n) [n] = str,
#include "tranpsuter-opcodes.h"
#undef OP
};

/******************************************************/


ST_FUNC void gsym_addr(int t_, int a_)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


ST_FUNC void gsym(int t_)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


ST_FUNC void load(int r, SValue *sv)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


ST_FUNC void store(int r, SValue *sv)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret,
                       int *ret_align, int *regsize)
{
    /* generic code can only deal with structs of pow(2) sizes
       (it always deals with whole registers), so go through our own
       code.  */
    tcc_error("implement me: %s", __FUNCTION__);
}


ST_FUNC void gfunc_call(int nb_args)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


ST_FUNC void gfunc_prolog(CType *func_type)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


ST_FUNC void gfunc_epilog(void)
{
    out_op(TP_OP_RET);
    fprintf(tp_outfile, "}\n\n");
}


ST_FUNC void gen_fill_nops(int bytes)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


ST_FUNC int gjmp(int t)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


ST_FUNC void gjmp_addr(int a)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


ST_FUNC int gjmp_cond(int op, int t)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


ST_FUNC int gjmp_append(int n, int t)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


void gen_opi(int op)
{
    switch(op) {
    case '+':
        out_op(TP_OP_ADD);
        goto std_op;
    case '-':
        out_op(TP_OP_SUB);
        goto std_op;
    case '&':
        out_op(TP_OP_AND);
        goto std_op;
    case '^':
        out_op(TP_OP_XOR);
        goto std_op;
    case '|':
        out_op(TP_OP_OR);
        goto std_op;
    case '*':
        out_op(TP_OP_MUL);
        goto std_op;
    case TOK_SHL:
        out_op(TP_OP_SHL);
        goto std_op;
    case TOK_SHR:
        out_op(TP_OP_SHR);
        goto std_op;
    case TOK_SAR:
        out_op(TP_OP_SHR);
        goto std_op;
    case '/':
    case TOK_PDIV:
        out_op(TP_OP_DIV);
        goto std_op;
    case TOK_UDIV:
        out_op(TP_OP_DIV);
        goto std_op;
    case '%':
        out_op(TP_OP_REM);
        goto std_op;
    case TOK_UMOD:
        out_op(TP_OP_REM);
    std_op:
        break;
    }
}


ST_FUNC void gen_opf(int op)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


ST_FUNC void gen_cvt_ftoi(int t)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


ST_FUNC void gen_cvt_itof(int t)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


ST_FUNC void gen_cvt_ftof(int t)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


ST_FUNC void ggoto(void)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


ST_FUNC void o(unsigned int c)
{
    tcc_error("implement me: %s", __FUNCTION__);
}


/* Save the stack pointer onto the stack and return the location of its address */
ST_FUNC void gen_vla_sp_save(int addr) {
    tcc_error("variable length arrays unsupported for this target");
}

/* Restore the SP from a location on the stack */
ST_FUNC void gen_vla_sp_restore(int addr) {
    tcc_error("variable length arrays unsupported for this target");
}

/* Subtract from the stack pointer, and push the resulting value onto the stack */
ST_FUNC void gen_vla_alloc(CType *type, int align) {
    tcc_error("variable length arrays unsupported for this target");
}


/* end of transputer code generator */
/*************************************************************/
#endif
/*************************************************************/