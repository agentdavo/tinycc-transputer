/*
 *  Tansputer code generator for TCC
 * 
 
/* number of available registers */
#define NB_REGS 7

#define DEFAULT_ALIGN 1
/* maximum alignment (for aligned attribute support) */
#define MAX_ALIGN     4

/* a register can belong to several classes. The classes must be
   sorted from more general to more precise (see gv2() code which does
   assumptions on it). */
#define RC_IST      0x0001    /* any int stack entry   */
#define RC_IST0     0x0002    /* top of int stack      */
#define RC_IST1     0x0004    /* top int stack - 1     */
#define RC_IST2     0x0008    /* top int stack - 2     */
#define RC_WREG	    0x0010    /* workspace register    */

#define RC_FST      0x0020    /* any float stack entry */
#define RC_FST0     0x0040    /* top of float stack    */
#define RC_FST1     0x0080    /* top float stack - 1   */
#define RC_FST2     0x0100    /* top float stack - 2   */

#define RC_INT      0x0200      /* generic integer register */
#define RC_FLOAT    0x0400      /* generic float register   */

#define REG_IRET     RC_IST0  /* single word int return register */
#define REG_LRET     RC_IST1  /* second word int return register for long long */
#define REG_FRET     RC_FST0  /* 64 bit float return register */

#define ALIGNEMENT 	DEFAULT_ALIGN

#define ALWAYS_ASSERT(x) \
do {\
   if (!(x))\
       tcc_error("internal compiler error file at %s:%d", __FILE__, __LINE__);\
} while (0)

#define BETWEEN(v1,l1,v2,l2) \
if !((v1 + l1 >= v2 && v1 <= v2) || (v2 + l2 >= v1 && v2 <= v2))  

/* pretty names for the registers */
enum {
    REG_IST0 = 0,
    REG_IST1,
    REG_IST2,
	REG_WREG,
	REG_FST0,
    REG_FST1,
    REG_FST2,
};


ST_DATA const int reg_classes[NB_REGS] = {
	/* IST0 */ RC_IST | RC_IST0,
    /* IST1 */ RC_IST | RC_IST1,
    /* IST2 */ RC_IST | RC_IST2,
	/* WREG */ RC_IST | RC_WREG,
	/* FST0 */ RC_FST | RC_FST0,
    /* FST1 */ RC_FST | RC_FST1,
    /* FST2 */ RC_FST | RC_FST2,
};

static char *reg_names[] =  {
    "Areg",
    "Breg",
    "Creg",
    "Wreg",
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

// The memory code consists of pfix, opr and the opcode
// if it's a secondary function we need opr as operation code

// E.g. 
// direct function: ldc 40 = pfix 4; ldc 0 = 0x2440
// Indirect function: ret = pfix 2; opr 0 = 0x22F0
// shl = 0x41 = pfix 4 opr 1 = 0x24F1

// Question: How does TCC interact through paging and mapping 
// with memory access (org, alignment, ...), mapped registers, local var, etc. ?

// Important functions: gv, gv2, tccgen_compile
// Important files: tcc_gen

// Are those actually stored in TCC structures SValue, CValue, vtop, ...?
// Direct assembly code would be much easier to handle.

// c67, tccgen are good references how to implement the code generator
// tccgen is the meta-level of the tcc code generator

ST_FUNC void transputer_init(struct TCCState *s)
{

}

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
			out_op(TP_OP_LDL)
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
        	}

		// Need Wptr (local) or Areg (non-local) stack offset for address loading 
		// Areg, Breg, Creg for integer and address handling
		if (v == VT_LOCAL && fc > 0) {
			int stack_pos = 8;

			for (t = 0; t < NoCallArgsPassedOnStack; t++) {
				if (fc == stack_pos)
					break;
			}

			stack_pos += TranslateStackToReg[t];

			// param has been pushed on stack, get it like a local var

			fc = ParamLocOnStack[t] - 8;
		}

		if ((fr & VT_VALMASK) < VT_CONST)	// check for pure indirect
		{
			if (size == 1 || size == 2) {
				Transputer_LDPL(v);		// LDPL n = *v into Areg address offset relative to Wptreg
			} else if (size == 4) {
				Transputer_LDPL(v);
				Transputer_FPLDNLSN(v);	// load 64 bit non-local offset from Areg load previously into FPareg
			} else if (size == 8) {
				Transputer_LDPL(v);
				Transputer_FPLDNLDB(v);
			}
			// We only need Areg and Breg
			if r == REG_IST1
				Transputer_REV();
			gen_fill_nops(4);
			return;
    		} else if (fr & VT_SYM) {
	    		greloc(cur_text_section, sv->sym, ind, R_C60HI16);	// rem the inst need to be patched
	    		greloc(cur_text_section, sv->sym, ind + 4, R_C60HI16);

	    		Transputer_LDC(fc);

	    		if (size == 1 || size == 2) {
		   		Transputer_LDLNP(0);	
	    		} else if (size == 4) {
				Transputer_LDLNP(0);
				Transputer_FPLDNLSN(v);
	    		} else if (size == 8) {
				Transputer_LDLNP(0);
				Transputer_FPLDNLDB(v);
	    		}

	    		gen_fill_nops(4);		// NOP 4
	    		return;
		}
	} else {
        if (v == VT_CONST) {
			if (fr & VT_SYM) {
				greloc(cur_text_section, sv->sym, ind, R_C60LO16);	// rem the inst need to be patched
				greloc(cur_text_section, sv->sym, ind + 4, R_C60HI16);
			}
			Transputer_LDC(fc);	//fc contant to load into Areg
            //printf("ldi %s, %d\n", reg_names[r], fc);
        } else if (v == VT_LOCAL) {
		Transputer_LDL(fc + 8);
		Transputer_ADD();
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

/* Need remapping of registers?
int Transputer_map_regn(int r)
{
	if (r == 0)
		return 0x04		// Wreg
	else if (r == 1)
		return 0x00		// Areg
	else if (r == 2)
		return 0x01		// Breg
	else if (r == 3)
		return 0x02		// Creg
	else if (r == RC_FST0)
		return 0x05		// FPregA
	else if (r == RC_FST1)
		return 0x06		// FPregB
	else if (r == RC_FST2)
		return 0x07		// FPregC
	else 
		ALWAYS_ASSERT(FALSE);

	return 0
}
*/

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

	CType *func_type = &func_sym->type;
	int addr, align, size, func_call, i;
	Sym *sym;
	CType *type;
	
	sym = func_type->ref;
	func_call = sym->f.func_call;
	addr = 8;

	/* if the function returns a structure, then add an
	   implicit pointer parameter */
	func_vt = sym->type;
	func_var = (sym->f.func_type == FUNC_ELLIPSIS);

	if ((func_vt.t & VT_BTYPE) == VT_STRUCT) {
		func_vc = addr;
		addr += 4;
	}

	NoOfCurFuncArgs = 0;

	/* define parameters */
	while ((sym = sym->next) != NULL) {
		type = &sym->type;
		sym_push(sym->v & ~SYM_FIELD, type, VT_LOCAL | VT_LVAL, addr);
		size = type_size(type, &align);
		size = (size + 3) & ~3;

		// keep track of size of arguments so
		// we can translate where tcc thinks they
		// are on the stack into the appropriate reg

		TranslateStackToReg[NoOfCurFuncArgs] = size;
		NoOfCurFuncArgs++;

#ifdef FUNC_STRUCT_PARAM_AS_PTR
		/* structs are passed as pointer */
		if ((type->t & VT_BTYPE) == VT_STRUCT) {
			size = 4;
		}
#endif
		addr += size;
	}
	func_ret_sub = 0;
	/* pascal type call ? */
	if (func_call == FUNC_STDCALL)
		func_ret_sub = addr - 8;

	// place all the args passed in regs onto the stack

	loc = 0;

	/* Wptr + n	(old Wptr)(local n-3)
	 *      .	
	 *	.
	 *	.
	 * Wptr + 4	(local1)
	 * Wptr	+ 3 	(Creg)
	 * Wptr + 2 	(Breg)
	 * Wptr + 1	(Areg)
	 * Wptr + 0	(Iptreg)
	*/

	// First save Integer Stack Register and Iptr 
	Transputer_AJW(-NoOfCurFuncArgs-4);	// Adjust Workspace for local variables
	Transputer_STL(1);
	Transputer_STL(2);
	Transputer_STL(3);
	Transputer_LDC(ind+NoOfCurFuncArgs+1);
	Tranputer_LDPI();			// Load relative instruction pointer after function call
	Transputer_STL(4);			// Iptr instruction after call in Wptr+4, Areg = return address

	// Function pointer as parameter? LDPNL 
	// Store absolute address with ldl and stl
	
	// First three stack registers are getting 
	// stored into Wptr 0-3 and rest gets stacked 
	// from Wptr+5 - Wptr+5+param_size (aligned)	
	for (i = 0; i < NoOfCurFuncArgs; i++) {

		ParamLocOnStack[i] = loc;	// remember where the param is
		loc -= 4;
		Transputer_LDL(loc);
		Transputer_STL(i+5);
	}

	TotalBytesPushedOnStack = -loc;

	func_sub_sp_offset = ind;	// remember where we put the stack instruction 
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

int prefix(int v)
{
	if v < 0x10
		return 0x00;

	return v >> 4;
}

/* 	Indirect Functions 	*/

// v1 = local offset of data block from source
// v2 = same for destination
// l1 = length of v1 block in bytes
// l2 = same for v2 block
void Transputer_MOVE(int v1, int v2, int l1, int l2)
{
	ALWAYS_ASSERT((l1 > l2) || BETWEEN(v1,l1,v2,l2) \
			|| (v1 % ALIGNEMENT || v2 % ALIGNEMENT);

	Transputer_LDPL(v1);
	Transputer_LDPL(v2);
	Transputer_LDC(l1);
	gen_l16(0x24FA);
	// out_op(TP_OP_MOVE);
}

void Transputer_GAJW()
{
	gen_l16(0x23FC);
}

void Transputer_AJW(int v)
{
	g(0x20 | prefix(v));
	g(0xB | (0x0F & v));
}

void Transputer_LDPI()
{
	gen_l16(0x21FB);
}

void Transputer_REV()
{
	g(0xF0);
}

void Transputer_LB()
{
	g(0xF1);
	// out_op(TP_OP_LB);
}

void Transputer_SUM()
{
	gen_l16(0x25F2);
	// out_op(TP_OP_SUM);
}

void Transputer_DIFF()
{
	g(0xF4);
	// out_op(TP_OP_DIFF);
}

void Transputer_PROD()
{
	g(0xF8);
	// out_op(TP_OP_PROD);
}

void Transputer_OR()
{
	gen_l16(0x24FB);
	// out_op(TP_OP_OR);
}

void Transputer_DIV()
{
	gen_l16(0x22FC);
	// out_op(TP_OP_DIV);
}

void Transputer_AND()
{
	gen_l16(0x24F6);
	// out_op(TP_OP_AND);
}

void Transputer_LDPI()
{
	gen_l16(0x21FB);
	// out_op(TP_OP_LDPI);
}

void Transputer_SHR()
{
	gen_l16(0x24F0);
	// out_op(TP_OP_SHR);
}

void Transputer_LADD()
{
	gen_l16(0x21F6);
	// out_op(TP_OP_LADD);
}

void Transputer_LSUB()
{
	gen_l16(0x23F8);
	// out_op(TP_OP_LSUB);
}

void Transputer_LMUL()
{
	gen_l16(0x23F1);
	// out_op(TP_OP_LMUL);
}

#if defined Transputer9000
void Transputer_XBWORD()
{
	gen_l16(0x2BF8);
}

#endif

void Transputer_XWORD() 
{
	gen_l16(0x23FA);
	// out_op(TP_OP_XWORD);
}

void Transputer_LDIV()
{
	gen_l16(0x21FA);
	// out_op(TP_OP_LDIV);
}


void Transputer_SHL()
{
	gen_l16(0x24F1);
	// out_op(TP_OP_SHL);
}

void Transputer_FPLDNLSN(int v)
{
	gen_l16(0x28FE);
	// out_op(TP_OP_FPLDNLSN);
}

/*	Direct Functions 	*/

// v = offset from Wpreg
void Transputer_STL(int v)
{
	g(0x20 | prefix(v));
	g(0xD0 | 0x0F & v);
	// out_op(TP_OP_STL);
}

void Transputer_DUP() 
{
	g(0x25FA);
	// out_op(TP_OP_DUP);
}

void Transputer_STNL(int v)
{
	g(0x20 | prefix(v));
	g(0xE0 | 0x0F & v);
	// out_op(TP_OP_STNL);
}

// v = Constant 
void Transputer_LDC(int v)
{
	g(0x20 | prefix(v));
	g(0x40 | 0x0F & v);
	// out_op(TP_OP_LDC);
}

// Load non-local address relative from Areg 
void Transputer_LDNL(int v)
{
	g(0x20 | prefix(v));
	g(0x30 | v & 0x0F);
	// out_op(TP_OP_LDNL);
}

// v = offset from wpreg as address 
void Transputer_LDL(int v)
{
	g(0x20 | prefix(v));
	g(0x70 | 0x0F & v);
	// out_op(TP_OP_LDL);
}

// v = offset from Areg relative address
void Transputer_LDLP(int v) 
{
	g(0x20 | prefix(v));
	g(0x10 | 0x0F & v);
	//out_op(TP_OP_LDLP);
}

void Transputer_LDNLP(int v)
{
	g(0x20 | prefix(v));
	g(0x50 | 0x0F & v);
}

void Transputer_ADC(int v)
{
	g(0x20 | prefix(v));
	g(0x80 | 0x0F & v);
	//out_op(TP_OP_ADC);
}

void Transputer_ADD()
{
	g(0xF5);
	//out_op(TP_OP_ADD);
}

void Transputer_SUB()
{
	g(0xFc);
	//out_op(TP_OP_SUB);
}

void Transputer_FPADD() 
{
	g(0x28F7);
}

void Transputer_FPSUB() 
{
	g(0x28F9);
}

void Transputer_FPMUL() 
{
	g(0x28FB);
}

void Transputer_FPDIV() 
{
	g(0x28FC);
}

void Transputer_FPEXPDEC32() 
{
	g(0x2DF9);
}

void Transputer_FPEXPINC32() 
{
	g(0x2DFA);
}

void Transputer_FPRZ()
{
	g(0x2DF6);
}

void Transputer_FPRN() 
{
	g(0x2DF0);
}

void Transputer_FPI32TOR64()
{
	g(0x29F8);
	// out_op(TP_OP_FPI32TOR64);
}

void Transputer_FPB32TOR64()
{
	g(0x29FA);
	// out_op(TP_OP_FPB32TOR64);
}

void Transputer_FPI32TOR32()
{
	g(0x29F6);
	// out_op(TP_OP_FPI32TOR32);
}

/* generate an integer binary operation */
void gen_opi(int op)
{
	int r, fr, opc;

	gv(RC_INT);

	r = vtop[-1].r;

	// Since the Transputer arch is stack based
	// the order of instruction for non-commuting
	// operators is important.

	// E.g. if a and b are two non local variables
	// and we want to divide them, we first need 
	// to load b into areg with LDL and LDNL and than
	// rev into breg and a into areg with LDL and LDNL.
	// If fr is not non-local we only need to load 
	// a into areg and LDL into breg and divide them

	// LDNL operates on Areg and doesn't affect Breg and Creg
	// and after LDL it overwrites the previous content

	switch(op) {
		case '+':
			opc = 0x05;
			// out_op(TP_OP_ADD);
			goto std_op;
		case '-':
			opc = 0x0C;
			//out_op(TP_OP_SUB);
			goto std_op;
		case '&':
			opc = 0x46;
			//out_op(TP_OP_AND);
			goto std_op;
		case '^':
			opc = 0x33;
			//out_op(TP_OP_XOR);
			goto std_op;
		case '|':
			opc = 0x4B;
			//out_op(TP_OP_OR);
			goto std_op;
		case '*':
			opc = 0x53;
			//out_op(TP_OP_MUL);
			goto std_op;
		case TOK_SHL:
			opc = 0x41;
			// out_op(TP_OP_SHL);
			goto std_shift;
		case TOK_SHR:
			opc = 0x40;
			//out_op(TP_OP_SHR);
			goto std_shift;
		case TOK_SAR:
			out_op(TP_OP_SHR);
			goto std_shift;
		case '/':
			opc = 0x2C;
			//out_op(TP_OP_DIV);
			goto std_op;
		case '%':
			opc = 0x1F;
			//out_op(TP_OP_REM);
			goto std_op;
		case TOK_UMOD:
			out_op(TP_OP_REM);
			goto std_op;
		case TOK_GT:
			out_op(TP_OP_GT);
			goto std_op;
		std_shift:
			ALWAYS_ASSERT(abs(v) < 32767);

			if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) {
			    	/* constant case */
			    	//c = vtop->c.i & 0x1f;
			    	//Transputer_LDC(c);
				gen_l16((0x02 << 12) | \
					(prefix(opc) << 8) \
					(0xF0 | (0x0F & opc)));
			} else {
				if ((vtop->r & VT_BTYPE) == VT_LONG) {
					opc += -9;
				}
				gv(REG_IST1);
			    	gen_l16((0x02 << 12) | \
					(prefix(opc) << 8) \
					(0xF0 | (0x0F & opc)));
			}
			vtop--;
			break;
		std_op:
			ALWAYS_ASSERT(abs(v) < 32767);

			if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) {
				/* constant case */
				c = vtop->c.i;
				if (op == '+' || op == '-') {
					Transputer_ADC(c);
				} else {
					gv(REG_IST1);
					gen_l16((0x02 << 12) | \
						(prefix(opc) << 8) \
						(0xF0 | (0x0F & opc)));
				}
			} else if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_LONG){
				gv(REG_IST1);
				if (op == '+')
					Transputer_LADD();
				else if (op == '-')
					Transputer_LSUB();
				else if (op == '/')
					Transputer_LDIV();
				else if (op == '*')
					Transputer_LMUL();
			} else {
				if (op == '+' || op == '-') {
					g(0xF0 | opc);
				} else {
					gen_l16((0x02 << 12) | \
						(prefix(opc) << 8) \
						(0xF0 | (0x0F & opc)));
				}
			}
			vtop--;
			if (op >= TOK_ULT && op <= TOK_GT)
			    vset_VT_CMP(op);
			break;
	    }
}

static int is_zero(int i)
{
    if((vtop[i].r & (VT_VALMASK | VT_LVAL | VT_SYM)) != VT_CONST)
      return 0;
    if (vtop[i].type.t == VT_FLOAT)
      return (vtop[i].c.f == 0.f);
    else if (vtop[i].type.t == VT_DOUBLE)
      return (vtop[i].c.d == 0.0);
    return (vtop[i].c.ld == 0.l);
}

/* generate a floating point operation 'v = t1 op t2' instruction. The
   two operands are guaranted to have the same floating point type */
ST_FUNC void gen_opf(int op)
{
    printf("gen_opf(op=%d)\n", op);

    switch(op) {
      case '+':
        if(is_zero(0)) {
          vtop--;
      	  return;
        }
      out_op(TP_OP_FPADD);
      break;
      case '-':
        if(is_zero(0)) {
          vtop--;
	  return;
        }
        out_op(TP_OP_FPSUB);
        break;
      case '*':
        out_op(TP_OP_FPMUL);
        break;
      case '/':
        out_op(TP_OP_PFDIV);
        break;
      case '%':
        out_op(TP_OP_FPREM);
        break;
      default:
        if(is_zero(-1)) {
      	  switch(op) {
	    case TOK_LT: 
	      out_op(TP_OP_FPGT); 
	      break;
	    case TOK_GE: 
              vswap(); 
	      out_op(TP_OP_FPGE); 
	      break;
            case TOK_LE:
	      out_op(TP_OP_FPGE); 
	      break;
            case TOK_GT:
	      vswap();
              out_op(TP_OP_FPGT); 
	      break;
            case TOK_EQ:
              out_op(TP_OP_FPEQ);
	      break;
          }
	  if(is_zero(0)) {
	    vtop--;
	  }
        }
       std_op:
	    vtop--;
        break;
     }
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

	save_reg(REG_FRET);
	gv(RC_INT);

	if ((vtop->type.t & VT_BTYPE) == VT_INT) {
		// TODO: Transputer_CWORD(); need to check if convertion is possible
        	/* signed N-bit object into signed Word(T) = 32 bit = 4 bytes */
		Transputer_LDLP(vtop->r);
		if ((vtop[-1]->type.t & VT_BTYPE) == VT_FLOAT) {
			Transputer_FPI32TOR32();
			vtop->r2 = VT_FLOAT;
		} else {
			Transputer_FPI32TOR64();
			vtop->r2 = VT_DOUBLE;
		}
	}

	if ((vtop->type.t & VT_BTYPE) == VT_LLONG) {
		Transputer_LDLP(vtop->r);
		Transputer_DUP();
		Transputer_FPB32TOR64();
		Transputer_LDNLP(1);
		Transputer_FPI32TOR64();
		Transputer_FPEXPINC32();
		Transputer_FPADD();
	}
}

ST_FUNC void gen_cvt_csti(int t)
{
	int r;

	printf("gen_cvt_csti(t=%d)\n", t);

	r = gv(RC_INT);

	// Breg and Creg are unaffected
	if ((t & VT_BTYPE) == VT_SHORT) {
		//Transputer_CS();
#if defined Transputer9000
		Transputer_XSWORD();
#else
		Transputer_REV();
		Transputer_LDC(4);
		Transputer_XWORD();	// Areg = length of type in bytes, Breg = content 
#endif	
	} else if ((vtop->type.t & VT_BTYPE) == VT_BYTE) {
		//Transputer_CB();
#if defined Transputer9000
		Transputer_XBWORD();
#else
		Transputer_REV();
		Transputer_LDC(4);
		Transputer_XWORD();	// Areg = length of type in bytes, Breg = content 
#endif
	} else if ((t & VT_BTYPE) == VT_INT) {
		//Transputer_CWORD();		// check if 8 bit can be repr. as uint 32
        	/* signed N-bit object into signed Word(T) = 32 bit = 4 bytes */
		Transputer_REV();
		Transputer_LDC(4);
		Transputer_XWORD();	// Areg = length of type in bytes, Breg = content 
        	vtop->r2 = VT_INT;
	}

	vtop->type.t = VT_INT;
	vtop->r2 = VT_CONST;
}


/* convert from one floating point type to another */
ST_FUNC void gen_cvt_ftof(int t)
{
    	printf("gen_cvt_ftof(t=%d)\n", t);

	if ((vtop->type.t & VT_BTYPE) == VT_DOUBLE &&
	(t & VT_BTYPE) == VT_FLOAT) {
	// convert double to float

	gv(RC_FLOAT);		// get it in a register pair

	r = vtop->r;

	Transputer_FPR64TOR32();

	vtop->type.t = VT_FLOAT;
	vtop->r2 = VT_CONST;	// set this as unused
    } else if ((vtop->type.t & VT_BTYPE) == VT_FLOAT &&
	       (t & VT_BTYPE) == VT_DOUBLE) {
	// convert float to double

	gv(RC_FLOAT);		// get it in a register

	r = vtop->r;
	Transputer_FPR32TOR64();

	vtop->type.t = VT_DOUBLE;
	vtop->r2 = r2;		// set this as unused
    } else {
	ALWAYS_ASSERT(FALSE);
    }
}


/* computed goto support */
ST_FUNC void ggoto(void)
{
    	printf("ggoto()\n");

	gcall_or_jmp(1);
    	vtop--;
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
