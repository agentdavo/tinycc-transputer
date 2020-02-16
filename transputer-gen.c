/*
 *  Tansputer code generator for TCC
 * 
 
/* number of available registers */
#define NB_REGS 6


/* a register can belong to several classes. The classes must be
   sorted from more general to more precise (see gv2() code which does
   assumptions on it). */
#define RC_IST      0x0000    /* any int stack entry   */
#define RC_IST0     0x0001    /* top of int stack      */
#define RC_IST1     0x0002    /* top int stack - 1     */
#define RC_IST2     0x0003    /* top int stack - 2     */

#define RC_FST      0x0004    /* any float stack entry */
#define RC_FST0     0x0005    /* top of float stack    */
#define RC_FST1     0x0006    /* top float stack - 1   */
#define RC_FST2     0x0007    /* top float stack - 2   */

#define RC_INT                /* generic integer register */
#define RC_FLOAT              /* generic float register   */

#define REG_IRET     RC_IST0  /* single word int return register */
#define REG_LRET     RC_IST1  /* second word int return register for long long */
#define REG_FRET     RC_FST0  /* 64 bit float return register */

/* pretty names for the registers */
enum {
    REG_IST0 = 0,
    REG_IST1,
    REG_IST2,
	REG_FST0,
    REG_FST1,
    REG_FST2,
};


ST_DATA const int reg_classes[NB_REGS] = {
	/* IST0 */ RC_IST | RC_IST0,
    /* IST1 */ RC_IST | RC_IST1,
    /* IST2 */ RC_IST | RC_IST2,
	/* FST0 */ RC_FST | RC_FST0,
    /* FST1 */ RC_FST | RC_FST1,
    /* FST2 */ RC_FST | RC_FST2,
};

static char *reg_names[] =  {
    "Areg",
    "Breg",
    "Creg",
    "FAreg",
    "FBreg",
    "FCreg"
}

/******************************************************/
/* opcode definitions */

enum TPOPCodes {
#define OP(name, str, n) TP_OP_ ## name = n,
#include "transputer-opcodes.h"
#undef OP
};

char *tp_opcodes_str[] = {
#define OP(name, str, n) [n] = str,
#include "transputer-opcodes.h"
#undef OP
};

/******************************************************/


/* output a symbol and patch all calls to it */
ST_FUNC void gsym_addr(int t, int a)
{
    printf("gsym_addr(t=%d, a=%d)\n", t, a);
}


ST_FUNC void gsym(int t)
{
    printf("gsym(t=%d)\n", t);
}


/* load 'r' from value 'sv' */
ST_FUNC void load(int r, SValue *sv)
{
    printf("load(r=%d, sv=%p)\n", r, sv);

    int v, t, ft, fc, fr, size = 0;
    BOOL Unsigned = FALSE;
    SValue v1;

    fr = sv->r;
    ft = sv->type.t;
    fc = sv->c.i;

	printf("fr=%x, ft=%d, fc=%d\n", fr, ft, fc);
	
    v = fr & VT_VALMASK;
	
    printf("v=%x\n", v);
	
    if (fr & VT_LVAL) {
        if (v == VT_LLOCAL) {
            v1.type.t = VT_INT;
            v1.r = VT_LOCAL | VT_LVAL;
            v1.c.i = fc;
            load(fr, &v1);
			fr = r;
        } else if ((ft & VT_BTYPE) == VT_FLOAT) {
			size = 4;
        } else if ((ft & VT_BTYPE) == VT_DOUBLE) {
			size = 8;
        } else if ((ft & VT_BTYPE) == VT_LDOUBLE) {
			tcc_error("long double not supported");
        } else if ((ft & VT_TYPE) == VT_BYTE) {
			size = 1;
        } else if ((ft & VT_TYPE) == (VT_BYTE | VT_UNSIGNED)) {
			size = 1;
			Unsigned = TRUE;
        } else if ((ft & VT_TYPE) == VT_SHORT) {
			size = 2;
        } else if ((ft & VT_TYPE) == (VT_SHORT | VT_UNSIGNED)) {
			size = 2;
			Unsigned = TRUE;
        } else {
			size = 4;
            printf("ldd %s, Y%+d\n", reg_names[r], fc);
        }
    } else {
        if (v == VT_CONST) {
            printf("ldi %s, %d\n", reg_names[r], fc);
        } else if (v == VT_LOCAL) {
        } else if (v == VT_CMP) {
        } else if (v == VT_JMP || v == VT_JMPI) {
        } else if (v != r) {
            printf("mov %s, %s\n", reg_names[r], reg_names[v]);
        }
    }
}


/* store register 'r' in lvalue 'v' */
ST_FUNC void store(int r, SValue * v)
{
    printf("store(r=%d, v=%p)\n", r, v);

    int fr, bt, ft, fc;

    ft = v->type.t;
    fc = v->c.ul;
    fr = v->r & VT_VALMASK;
    bt = ft & VT_BTYPE;

    if (bt == VT_FLOAT) {
    } else if (bt == VT_DOUBLE) {
    } else if (bt == VT_LDOUBLE) {
    } else {
        if (bt == VT_SHORT) {
        } if (bt == VT_BYTE || bt == VT_BOOL) {
        } else {
        }
    }
    if (fr == VT_CONST || fr == VT_LOCAL || (v->r & VT_LVAL)) {
        printf("std Y%+d, %s\n", fc, reg_names[r]);
    } else if (fr != r) {
        printf("mov %s, %s\n", reg_names[fr], reg_names[r]);
    }
}


ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret,
                       int *ret_align, int *regsize)
{
    /* generic code can only deal with structs of pow(2) sizes
       (it always deals with whole registers), so go through our own
       code.  */
    tcc_error("implement me: %s", __FUNCTION__);
}


/* 'is_jmp' is '1' if it is a jump */
static void gcall_or_jmp(int is_jmp)
{
    printf("gcall_or_jmp(is_jmp=%d)\n", is_jmp);
}


/* generate function call with address in (vtop->t, vtop->c) and free function
   context. Stack entry is popped */
ST_FUNC void gfunc_call(int nb_args)
{
    printf("gfunc_call(nb_args=%d)\n", nb_args);
}

/* generate function prolog of type 't' */
ST_FUNC void gfunc_prolog(CType *func_type)
{
    printf("//---------------------------------//\n");
    printf("gfunc_prolog(func_type=%p)\n", func_type);
}


/* generate function epilog */
ST_FUNC void gfunc_epilog(void)
{
    printf("gfun_epilog()\n");
    printf("ret\n");
    printf("//---------------------------------//\n");
}


ST_FUNC void gen_fill_nops(int bytes)
{
    if ((bytes & 3))
      tcc_error("alignment of code section not multiple of 4");
    while (bytes > 0) {
	TP_OP_NOP(4);
	bytes -= 4;
    }
}


/* generate a jump to a label */
ST_FUNC int gjmp(int t)
{
    printf("gjmp(t=%d)\n", t);
}


/* generate a jump to a fixed address */
ST_FUNC void gjmp_addr(int a)
{
    printf("gjmp_addr(a=%d)\n", a);
}


/* generate an integer binary operation */
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
	    vtop--;
        // vtop[0].r = REG_ST0; TODO
        break;
    }
}


/* generate a floating point operation 'v = t1 op t2' instruction. The
   two operands are guaranted to have the same floating point type */
ST_FUNC void gen_opf(int op)
{
    printf("gen_opf(op=%d)\n", op);
}


/* convert fp to int 't' type */
/* XXX: handle long long case */
ST_FUNC void gen_cvt_ftoi(int t)
{
    printf("gen_cvt_itof(t=%d)\n", t);
}


/* convert integers to fp 't' type. Must handle 'int', 'unsigned int'
   and 'long long' cases. */
ST_FUNC void gen_cvt_itof(int t)
{
    printf("gen_cvt_ftoi(t=%d)\n", t);
}


/* convert from one floating point type to another */
ST_FUNC void gen_cvt_ftof(int t)
{
    printf("gen_cvt_ftof(t=%d)\n", t);
}


/* computed goto support */
ST_FUNC void ggoto(void)
{
    printf("ggoto()\n");
}


ST_FUNC void g(int c)
{
    int ind1;
    ind1 = ind + 1;
    if (ind1 > cur_text_section->data_allocated)
        section_realloc(cur_text_section, ind1);
    cur_text_section->data[ind] = c;
    ind = ind1;
}


ST_FUNC void o(unsigned int c)
{
    while (c) {
        g(c);
        c = c >> 8;
    }
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