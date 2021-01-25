/*
 *  Tansputer code generator for TCC
 */ 

/* number of available registers */

#ifdef TARGET_DEFS_ONLY

#define NB_REGS 9

/* a register can belong to several classes. The classes must be
   sorted from more general to more precise (see gv2() code which does
   assumptions on it). */
#define RC_IST      	0x0000    	/* 	any int stack entry   	*/
#define RC_IST0     	0x0001    	/* 	top of int stack      	*/
#define RC_IST1     	0x0002    	/* 	top int stack - 1     	*/
#define RC_IST2     	0x0004    	/* 	top int stack - 2     	*/

#define RC_FST      	0x0010    	/* 	any float stack entry 	*/
#define RC_FST0     	0x0020    	/* 	top of float stack    	*/
#define RC_FST1     	0x0040    	/* 	top float stack - 1   	*/
#define RC_FST2     	0x0080    	/* 	top float stack - 2   	*/

#define RC_WREG		0x0100    	/* 	workspace register    	*/
#define RC_ARG    		0x0003		/* 	Argument pointer 		*/
#define RC_IPTR		0x0030		/* 	Frame pointer			*/

#define RC_INT      	0x0007      	/* 	generic integer register */
#define RC_FLOAT    	0x0070      	/* 	generic float register   */

#define RC_WORKREGS		0x0700		/* 	Workspace registers (through Regions)	*/

#define RC_BASEREGS		0x0707		/*	Base registers			*/
#define RC_STACKREGS	0x0077		/*	Stack registers		*/
#define RC_ALLREGS		0x0777		/*	All registers in region	*/

#define REG_IRET     	RC_IST0  		/* 	single word int return register */
#define REG_IRE2     	RC_IST1  		/* 	second word int return register for long long */
#define REG_FRET     	RC_FST0  		/* 	64 bit float return register */

#define ALWAYS_ASSERT(x) \
do {\
   if (!(x))\
       tcc_error("internal compiler error file at %s:%d", __FILE__, __LINE__);\
} while (0)

#define BETWEEN(v1,l1,v2,l2) \
  ((v1 + l1 >= v2 && v1 <= v2) || (v2 + l2 >= v1 && v2 <= v2))  

#define DEFAULT_ALIGN 	1
/* maximum alignment (for aligned attribute support) */
#define MAX_ALIGN     	4

#define BITS_BIG_ENDIAN	0

#define PTR_SIZE 		4

#define LDOUBLE_SIZE 8
#define LDOUBLE_ALIGN 8

#define DBL_EPSILON 	2.2204460492503131e-16
#define FLT_EPSILON 	1.19209290e-07F

#undef BOOL
#define BOOL int

#ifndef FALSE
# define FALSE 0
# define TRUE 1
#endif

#define ALIGNEMENT 		DEFAULT_ALIGN

/******************************************************/
#else /* ! TARGET_DEFS_ONLY */
/******************************************************/

#define USING_GLOBALS
#include "tcc.h"

ST_DATA const char *target_machine_defs =
    "__t9000__\0"
    ;

#if 0
#define DEFAULT_ALIGN 	1
/* maximum alignment (for aligned attribute support) */
#define MAX_ALIGN     	4

#define BITS_BIG_ENDIAN	0

#define PTR_SIZE 		4

#define LDOUBLE_SIZE 8
#define LDOUBLE_ALIGN 8

#define DBL_EPSILON 	2.2204460492503131e-16
#define FLT_EPSILON 	1.19209290e-07F

#undef BOOL
#define BOOL int

#ifndef FALSE
# define FALSE 0
# define TRUE 1
#endif

#define ALIGNEMENT 		DEFAULT_ALIGN
#endif
/* pretty names for the registers */
enum {
	REG_IST0 = 0,
	REG_IST1,
	REG_IST2,
	REG_FST0,
	REG_FST1,
	REG_FST2,
	REG_WREG,
	REG_ARG,
	REG_IPTR,
	REG_INT,
	REG_FLOAT,
	REG_WORKREGS,
	REG_BASEREGS,
	REG_STACKREGS,
	REG_ALLREGS
};

ST_DATA const int reg_classes[NB_REGS] = {
	/* IST0 */ RC_IST | RC_IST0,
	/* IST1 */ RC_IST | RC_IST1,
	/* IST2 */ RC_IST | RC_IST2,
	/* FST0 */ RC_FST | RC_FST0,
	/* FST1 */ RC_FST | RC_FST1,
	/* FST2 */ RC_FST | RC_FST2,
	/* WREG */ RC_WREG,
	/* ARG  */ RC_ARG,
	/* FRP  */ RC_IPTR,
	/* GEN  */ RC_INT,
	/* GENF */ RC_FLOAT,
	/* WRKs */ RC_WORKREGS,
	/* BASE */ RC_BASEREGS,
	/* STACK*/ RC_STACKREGS,
	/* ALL  */ RC_ALLREGS
};

static char *reg_names[] =  {
    	"Areg",
    	"Breg",
    	"Creg",
    	"FAreg",
    	"FBreg",
    	"FCreg",
    	"Wreg",
    	"Arg",
	"Iprt",
	"Gen",
	"Flt",
	"Wrgs",
	"Bregs",
	"Sregs",
	"Allregs"
};

#define REG_STK REG_STACKREGS
#define REG_WPTR REG_WORKREGS

/* FIXME: to be defined */
#define Transputer_SS()
#define Transputer_SB()
#define Transputer_LT()
#define Transputer_GT()
#define Transputer_FPREM()
#define Transputer_FPR64TOR32()
#define Transputer_FPR32TOR64()
#define Transputer_FPCHKERR()
#define Transputer_FPLDZEROSN()
#define Transputer_LS()
#define Transputer_LDLNP(A)
#define Transputer_WSUB()
#define Transputer_BSUB()
#define Transputer_WSUBDB()
#define Transputer_SSUB()
#define Transputer_NOT()
#define Transputer_LSHL()
#define Transputer_LSHR()
#define Transputer_LSUM()
#define Transputer_LDIFF()
#define Transputer_XDBLE()

static unsigned long func_sub_sp_offset;
static int func_ret_sub;

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

int get_prefix(int v)
{
	if (v < 16)
		return 0x00;
	else if (v < 0)
		return -v >> 4;

	return v >> 4;
}

static uint32_t BigtoLittle32(uint32_t value)
{
    	uint32_t result = 0;

    	result |= (value & 0x000000FF) << 24;
    	result |= (value & 0x0000FF00) << 8;
    	result |= (value & 0x00FF0000) >> 8;
    	result |= (value & 0xFF000000) >> 24;
    	
	return result;
}

#define NoCallArgsPassedOnStack 10
int NoOfCurFuncArgs;
int TranslateStackToReg[NoCallArgsPassedOnStack];
int ParamLocOnStack[NoCallArgsPassedOnStack];

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

/* 
 *	All transputers address bytes
 * 	from org 0x800000 (roles over after all 32 bit set) 
 * 	to 0x7FFFFF where 0x000000 is the middle
 */

/*
 * 	In protected mode 32 bit with align = 4 (through goprot), 
 *	the memory regions are 
 *	logical mapped through the region descriptors. The region 
 * 	registers are holding beside bit flags for page 
 *	marking (privilege level, cachable, dirty, readable, etc.) 
 * 	logical memory locations and flag registers for the integer and FP 
 *	stack registers in the P-state data structure (PDS). It's similar
 *	to the x86 IA32 legacy and protected mode where context switching 
 *	is handle by the hardware and we have to initilaize all necessary
 *	data structures into memory and load the logical address into 
 *	the descriptor registers before we switch to protected mode. All the 
 *	mapping is already done (in long mode we must map all needed regions
 *	before we enter long mode with a long jump) through the region registers.
 *	The memory management breaks down to all the information held in
 *	region0-region3 almost like cr3, PAT and MTRRs holding paging informations in x86. 

 *	Caching gets divided into memory cache and workspace cache, which is 
 *	mostly write-through. The mechanism of caching is similar to the of x86
 *	with some special informations about the workspace cache.
 * 	We only need to care about the allocated memory space for the compiler
 *	process, since from ring-levels we are at the
 */

/* output a symbol and patch all calls to it */
ST_FUNC void gsym_addr(int t_, int a_)
{
	uint32_t t = t_;
    	uint32_t a = a_;
   	printf("gsym_addr(t=%d, a=%d)\n", t, a);

   	while (t) {
		unsigned char *ptr = cur_text_section->data + t;
		uint32_t next = read32le(ptr);
		uint32_t r = a - t, imm;

		if (!((a - t + 0x800000 < 0x100000000) || (a-t < 0x7FFFFF)))
            		tcc_error("branch out of range");
		write32le(ptr, a - t == 4 ? 0x63F0 : ((0x02 << 12) | \
			 (get_prefix(imm) << 8) | 0xF0 | (imm & 0x0F))); // nop || j imm
		t = next;
    	}
}

static int oad(int c, int s)
{
	int t;
	if (nocode_wanted)
		return s;
	t = ind;
  	g((0x02 << 12) | \
	  (get_prefix(s)) << 8 | \
	  (c | (s & 0x0F)));

	return t;
}

#define gjmp2(instr,lbl) oad(instr,lbl)

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
		if ((ft & VT_BTYPE) == VT_STRUCT) {
	    		int align;
	    		switch (type_size(&sv->type, &align)) {
				case 1: ft = VT_BYTE; break;
				case 2: ft = VT_SHORT; break;
				case 4: ft = VT_INT; break;
				case 8: ft = VT_LLONG; break;
				default:
		    			tcc_error("invalid aggregate type for register load");
		    			break;
	    		}
		} else if (v == VT_LLOCAL) {
            		v1.type.t = VT_INT;
            		v1.r = VT_LOCAL | VT_LVAL;
            		v1.c.i = fc;
            		load(fr, &v1);
			fr = r;
        	} else if ((ft & VT_BTYPE) == VT_FUNC) {
			size = PTR_SIZE;
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
				Transputer_LDLP(v);		// LDPL n = *v into Areg address offset relative to Wptreg
				if (size == 1)				
					Transputer_LB();
				else
					Transputer_LS();
			} else if (size == 4) {
				if ((fr & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST && fc == 0.f) {
					Transputer_FPLDZEROSN();
				} else {
					Transputer_LDLP(v);
					Transputer_FPLDNLSN();
				}
			} else if (size == 8) {
				if ((fr & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST && fc == 0.0) {
					Transputer_FPLDZEROSN();
				} else {
					Transputer_LDLP(v);
					Transputer_FPLDNLDB();
				}
			}
			// We only need Areg and Breg
			if (r == REG_IST1)
				Transputer_REV();
			gen_fill_nops(4);
			return;
    		} else if (fr & VT_SYM) {
	    		greloc(cur_text_section, sv->sym, ind, R_TRANSPUTER_IPREL32);	// rem the inst need to be patched

	    		if (size == 1 || size == 2) {
		   		Transputer_LDLNP(fc);

				if (size == 1)				
					Transputer_LB();
				else
					Transputer_LS();

	    		} else if (size == 4) {
				if ((fr & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST && fc == 0.f) {
					Transputer_FPLDZEROSN();
				} else {
					Transputer_LDLP(v);
					Transputer_FPLDNLSN();
				}
	    		} else if (size == 8) {
				if ((fr & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST && fc == 0.0) {
					Transputer_FPLDZEROSN();
				} else {
					Transputer_LDLP(v);
					Transputer_FPLDNLDB();
				}
	    		}

	    		gen_fill_nops(4);		// NOP 4
	    		return;
		}
	} else {
		if (v == VT_CONST) {
			if (fr & VT_SYM)
				greloc(cur_text_section, sv->sym, ind, R_TRANSPUTER_IPREL32);

			Transputer_LDC(fc);	//fc contant to load into Areg
		} else if (v == VT_LOCAL) {
			Transputer_LDL(v);
		} else if (v == VT_CMP) {
			Transputer_LDL(v);
			Transputer_REV();
		} else if (v == VT_JMP || v == VT_JMPI) {
			t = v & 1;		// didn't found any description of near or far jumps
			Transputer_LDC(t);
			gjmp_addr(ind + 4);
			gsym(fc);
			Transputer_LDC(t ^ 1);
		} else if (v != r) {
		   	printf("mov %s, %s\n", reg_names[r], reg_names[v]);
			Transputer_LDL(v);
		}
	}	
}


/* store register 'r' in lvalue 'v' */
ST_FUNC void store(int r, SValue * v)
{
	printf("store(r=%d, v=%p)\n", r, v);

	int fr, bt, ft, fc, t, size = 0;

	ft = v->type.t;
	fc = v->c.i;
	fr = v->r & VT_VALMASK;
	bt = ft & VT_BTYPE;

	if (bt == VT_FLOAT)
		size = 4;
	else if (bt == VT_DOUBLE)
		size = 8;
	else if (bt == VT_LDOUBLE)
		tcc_error("long double not supported");
	else if (bt == VT_SHORT)
		size = 2;	
	else if (bt == VT_BYTE || bt == VT_BOOL)
		size = 1;
	else
		size = 4;

	if ((v->r & VT_VALMASK) == VT_CONST) {
		if (v->r & VT_SYM)
			greloc(cur_text_section, v->sym, ind, R_TRANSPUTER_IPREL32);
		
		Transputer_LDC(fr);	// Load constant into Areg
	} else if ((v->r & VT_VALMASK) == VT_LOCAL) {
		if (fc > 0)		// argument ??
	    	{
			// walk through sizes and figure which param

			int stack_pos = 8;

			for (t = 0; t < NoCallArgsPassedOnStack; t++) {
		    		if (fc == stack_pos)
					break;

		    	stack_pos += TranslateStackToReg[t];
			}

			// param has been pushed on stack, get it like a local var
			fc = ParamLocOnStack[t] - 8;
	    	}

		Transputer_LDL(fr);
	} else {
		Transputer_LDNL(fr);
	}

	if (size == 1) {
		Transputer_LDL(r);
		Transputer_SB();	// Store content into relative Wptr address in Areg
	} else if (size == 2) {
		Transputer_LDL(r);
		Transputer_SS();	// Store content into relative Wptr address in Areg
	} else if (size == 4 && bt == VT_INT) {
		Transputer_STL(r);		
	} else if (size == 4 && bt == VT_FLOAT) {
		Transputer_LDLP(r);
		Transputer_FPSTNLSN();
	} else {
		Transputer_LDLP(r);
		Transputer_FPSTNLDB();
	}
}

/* Return the number of registers needed to return the struct, or 0 if
   returning via struct pointer. Same as i386-gen. */
ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret,
                       int *ret_align, int *regsize)
{
#ifdef TCC_TARGET_PE
	int size, align;
	*ret_align = DEFAULT_ALIGN;	// Never have to re-align return values for Transputer
	*regsize = 4;
	size = type_size(vt, &align);
	    
	if (size > 8 || (size & (size - 1)))
		return 0;
	if (size == 8)
		ret->t = VT_LLONG;
	else if (size == 4)
		ret->t = VT_INT;
	else if (size == 2)
		ret->t = VT_SHORT;
	else
		ret->t = VT_BYTE;
		ret->ref = NULL;
		return 1;
#else
	*ret_align = 1;
	return 0;
#endif
}

/* 'is_jmp' is '1' if it is a jump */
static void gcall_or_jmp(int is_jmp)
{
    	printf("gcall_or_jmp(is_jmp=%d)\n", is_jmp);
	
	int r;

    	if ((vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST && (vtop->r & VT_SYM)) {
		/* constant and relocation case */
		greloc(cur_text_section, vtop->sym, ind + 1, R_TRANSPUTER_IPREL32);
		oad(0x09 - (is_jmp*9), vtop->c.i - 4); /* call/jmp im */
    	} else {
		/* otherwise, indirect call */
		r = gv(RC_INT);
		o(r);
		Transputer_GCALL();
    	}
}

/* generate function call with address in (vtop->t, vtop->c) and free function
   context. Stack entry is popped */
ST_FUNC void gfunc_call(int nb_args)
{
    printf("gfunc_call(nb_args=%d)\n", nb_args);

	int size, align, r, args_size, i, func_call;
    	Sym *func_sym;

	align = DEFAULT_ALIGN;

    	args_size = 0;

    	for(i = 0;i < nb_args; i++) {
        if ((vtop->type.t & VT_BTYPE) == VT_STRUCT) {
            size = type_size(&vtop->type, &align);
            /* align to stack align size */
            size = (size + 3) & ~3;
            /* generate structure store */
            r = get_reg(RC_INT);
		  args_size += size;

		  if (size == 1) {
			Transputer_LDLP(RC_ARG);
			Transputer_ADC(-args_size);
			Transputer_LDLP(r);
			Transputer_LB();
			Transputer_REV();
			Transputer_SB();
		  } else {
			//Transputer_LDLP(RC_ARG);
			//Transputer_ADC(-size);
			
		  	Transputer_LDL(r);
            	Transputer_STL(RC_ARG-args_size);
            }
		  vset(&vtop->type, r | VT_LVAL, 0);
            vswap();
            vstore();
        } else if (is_float(vtop->type.t & VT_BYTE)) {
            gv(RC_FST); 		/* only one float register */
            if ((vtop->type.t & VT_BTYPE) == VT_FLOAT)
                size = 4;
            else if ((vtop->type.t & VT_BTYPE) == VT_DOUBLE)
                size = 8;

		  args_size += size;

            Transputer_LDLP(RC_ARG);
            Transputer_ADC(-args_size);

            if (size == 8)
                Transputer_FPSTNLDB();
            else
			 Transputer_FPSTNLSN();
        } else {
            /* simple type (currently always same size) */
            r = gv(RC_INT);
		  args_size += size;

		  Transputer_LDL(r);
            Transputer_STL(RC_ARG-args_size);
            
		  if ((vtop->type.t & VT_BTYPE) == VT_LLONG) {
			 Transputer_LDL(vtop->r2);
			 Transputer_STL(RC_ARG-args_size+4);
 		  }
        }
        vtop--;
    }
    save_regs(0); /* save used temporary registers */
    func_sym = vtop->type.ref;
    func_call = func_sym->f.func_call;
}

/* generate function prolog of type 't' */
ST_FUNC void gfunc_prolog(Sym *func_sym)
{
	printf("//---------------------------------//\n");
    	printf("gfunc_prolog(func_type=%p)\n", func_sym);

	CType *func_type = &func_sym->type;
	int addr, align, size, func_call, i, offset;
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

	offset = (addr >> 3);

	// First save Integer Stack Register and Iptr 
	Transputer_LDL(REG_ARG);
	Transputer_LDNL(1);
	Transputer_LDL(REG_STK);
	Transputer_GAJW();
	Transputer_DUP();
	Transputer_LDNL(REG_ARG);
	Transputer_STL(offset - 3);
	Transputer_DUP();
	Transputer_LDNL(REG_WPTR);
	Transputer_STL(offset - 2);
	Transputer_STL(offset - 1);
	Transputer_AJW(-2);
	Transputer_STL(1);
	Transputer_LDC(ind);
	Transputer_LDC(2);
	Transputer_LDPI();
	Transputer_STL(0);
	Transputer_GCALL();

/*
	Transputer_AJW(-NoOfCurFuncArgs-4);	// Adjust Workspace for local variables
	Transputer_STL(1);
	Transputer_STL(2);
	Transputer_STL(3);
	Transputer_LDC(ind+NoOfCurFuncArgs+1);
	Tranputer_LDPI();			// Load relative instruction pointer after function call
	Transputer_STL(4);			// Iptr instruction after call in Wptr+4, Areg = return address
*/
	// Function pointer as parameter? LDPNL 
	// Store absolute address with ldl and stl
	
	// First three stack registers are getting 
	// stored into Wptr 0-3 and rest gets stacked 
	// from Wptr+5 - Wptr+5+param_size (aligned)	
}

/* generate function epilog */
ST_FUNC void gfunc_epilog(void)
{
    	printf("gfun_epilog()\n");
    	printf("ret\n");
    	printf("//---------------------------------//\n");

	int offset = func_sub_sp_offset;

	Transputer_LDNL((-loc + 7) & -4);
	Transputer_LDL(offset-4);
	Transputer_LDL(offset-3);
	Transputer_STNL(REG_WPTR);
	Transputer_LDL(offset-5);
	Transputer_LDL(offset-3);
	Transputer_STNL(REG_ARG);
	
	Transputer_LDL(offset-3);
	Transputer_AJW(offset-2);
	Transputer_GAJW();
	Transputer_STL(REG_STK);
}


ST_FUNC void gen_fill_nops(int bytes)
{
    if ((bytes & 3))
      tcc_error("alignment of code section not multiple of 4");
    while (bytes > 0) {
	Transputer_NOP(4);
	//TP_OP_NOP(4);
	bytes -= 4;
    }
}

/*	Binary and unary operations simplified 	*/

/* Consider the following definitions: 
 * z := memory address of local variable
 * a := long var X relative offset from Wptreg
 * b := long var Y --,,--
 * s := int shift constant
 * c := word constant
 * i := index 
 * x := (SValue) stack entry
 * word aligned, byte to word length addresses, offset are positive
 */

// load b into a[i] word array
void Transputer_LD_BLOCK_OP(int a, int b, int i)
{
	Transputer_LDL(a);
	Transputer_LDNL(4*i);
	Transputer_STL(b);

	Transputer_LDC(i);
	Transputer_LDLP(a);
	Transputer_WSUB();
	
	Transputer_LDLP(a);
	Transputer_LDNLP(i);
}

void Transputer_LDAR(SValue * x, int b, int i)
{
	ALWAYS_ASSERT(x->type.t & VT_ARRAY);

	Sym *s;
	s = x->type.ref;
	int ts = type_size(&s->type, 1);

	if (ts < 0 && s->c < 0)
		ts = -ts;

	if ((x->type.t & VT_BTYPE) == VT_LONG)
		printf("Loading long array content is not supported!");

	if ((x->type.t & VT_BTYPE) == VT_PTR) {
		Transputer_LDC(i);
		Transputer_LDL(x);

		// same as LDL x; LDLNP e; STL b; where LDLNP e gives non-local address

		if (ts == 1)
			Transputer_BSUB();
		else if (ts == 4)
			Transputer_WSUB();
		else if (ts == 8)
			Transputer_WSUBDB();
		else 
			Transputer_SSUB();

		Transputer_STL(b);
	} else if ((x->type.t & VT_BTYPE) == VT_INT) {
		Transputer_LDL(x);		
		Transputer_LDNL(ts*i);
		Transputer_STL(b);
	} else {
		Transputer_LDC(ts);
		Transputer_LDLP(x);

		if ((x->type.t & VT_BTYPE) == VT_DOUBLE) {
			Transputer_FPLDNLDB();
			Transputer_FPSTNLDB(b);
		} else {
			Transputer_FPLDNLSN();
			Transputer_FPSTNLSN(b);
		}
	}
}

void Transputer_GCALL(int n)
{
	gen_le16(0x20 << 12 | \
			(get_prefix(n) << 8) | \
			(0xF0 | (0x0F & n)));
}

void Transputer_LDC_OP(int a, int c)
{
	Transputer_LDC(c);
	Transputer_LDL(a);
	Transputer_STNL(0);
}

void Transputer_LDL_OP(int a, int b)
{
	Transputer_LDL(a);
	Transputer_LDNL(0);
	Transputer_LDL(b);
	Transputer_STNL(0);
}

void Transputer_LADD_OP(SValue *a, SValue *b, SValue *z)
{
	Transputer_LDC(0);

	Transputer_LDL(a->r);
	Transputer_LDL(b->r);
	Transputer_LSUM();
	Transputer_STL(z->r);

	Transputer_LDL(a->r2);
	Transputer_LDL(b->r2);
	Transputer_LADD();
	Transputer_STL(z->r2);
}

void Transputer_LSUB_OP(SValue *a, SValue *b, SValue *z)
{
	Transputer_LDC(0);

	Transputer_LDL(a->r);
	Transputer_LDL(b->r);
	Transputer_LDIFF();
	Transputer_STL(z->r);

	Transputer_LDL(a->r2);
	Transputer_LDL(b->r2);
	Transputer_LSUB();
	Transputer_STL(z->r2);
}

void Transputer_LMUL_OP(SValue *a, SValue *b, SValue *z)
{
	int ra, rb, rz, ra2, rb2, rz2;
	ra = a->r;
	rb = b->r;
	rz = z->r;

	ra2 = a->r2;
	rb2 = b->r2;
	rz2 = z->r2;

	Transputer_LDC(0);

	Transputer_LDL(ra);
	Transputer_LDL(rb);
	Transputer_LMUL();
	Transputer_STL(rz);
	
	Transputer_LDL(ra);
	Transputer_LDL(rb2);
	Transputer_LMUL();
	Transputer_REV();
	Transputer_STL(rz);
	
	Transputer_LDL(ra2);
	Transputer_LDL(b);
	Transputer_LMUL();
	Transputer_STL(rz2);
	
	Transputer_LDL(ra2);
	Transputer_LDL(rb2);
	Transputer_LMUL();
	Transputer_STL(rz2);
	
	Transputer_LDC(0);
	Transputer_REV();
	Transputer_LDL(rz);
	Transputer_LSUM();
	Transputer_STL(rz);

	Transputer_LDL(rz2);
	Transputer_SUM();
	Transputer_STL(rz2);
}

void Transputer_LDIV_OP(SValue *a, SValue *b, SValue *z)
{
	Transputer_LDC(0);

	// Since we only have 3 registers for 1 indirect operation
	// we need to simplify (a_hi + a_lo) / (b_hi + b_lo) = a_hi / (b_hi + b_lo) + ...
	// We calculate and save the denominator once and add 
	// both results into z+4 (hi) and z (lo) 64 bit 2 word (Transputer 1 (d)word = 32 bit)
	// Transputer overwrites content of Breg, without push we cannot 

	Transputer_LDL(b->r);
	Transputer_LDL(b->r2);
	Transputer_LSUM();
	
	Transputer_REV();

	Transputer_LDL(a->r);
	Transputer_LDIV();
	Transputer_STL(z->r);

	Transputer_LDL(b->r);
	Transputer_LDL();
	Transputer_LSUM();
	
	Transputer_REV();

	Transputer_LDL(a->r2);
	Transputer_STL(z->r2);
}

void Transputer_LLSHL(SValue *a, long int s)
{
	Transputer_LDL(a->r);
	Transputer_LDL(a->r2);
	Transputer_LDL(s);
	Transputer_LSHL();
	Transputer_STL(a->r);
	Transputer_STL(a->r2);
}

void Transputer_LLSHR(SValue *a, long int s)
{
	Transputer_LDL(a->r);
	Transputer_LDL(a->r2);
	Transputer_LDL(s);
	Transputer_LSHR();
	Transputer_STL(a->r);
	Transputer_STL(a->r2);
}

void Transputer_SLSHL(SValue *a, long int s)
{
	Transputer_LDL(a->r);
	Transputer_XDBLE();
	Transputer_LDL(s);
	Transputer_LSHL();
	Transputer_STL(a->r);
}

void Transputer_SLSHR(SValue *a, long int s)
{
	Transputer_LDL(a->r);
	Transputer_XDBLE();
	Transputer_LDL(s);
	Transputer_LSHR();
	Transputer_STL(a->r);
}

void Transputer_ROR(int a, int s)
{
	Transputer_LDL(a);
	Transputer_LDC(0);
	Transputer_LDL(s);
	Transputer_LSHR();
	Transputer_OR();
	Transputer_STL(a);
}

void Transputer_ROL(int a, int s)
{
	Transputer_LDL(a);
	Transputer_LDC(0);
	Transputer_LDL(s);
	Transputer_LSHL();
	Transputer_OR();
	Transputer_STL(a);
}

/*	Jump Instruction handling	*/

/* generate a jump to a label */
ST_FUNC int gjmp(int t)
{
	printf("gjmp(t=%d)\n", t);
	return gjmp2(0x00, t);
}

/* generate a jump to a fixed address */
ST_FUNC void gjmp_addr(int a)
{
	printf("gjmp_addr(a=%d)\n", a);

	int r;
	r = a - ind - 2;	// a-L relative to org address (absolute address) - 2 bytes for instruction
	g((0x20 << 12) | (get_prefix(r) << 8) | (0x0F) << 4 | (0x0F & r));
}

// FIXME:
ST_FUNC int gjmp_append(int n, int t)
{
    void *p;
    /* insert jump list n into t */
    if (n) {
        uint32_t n1 = n, n2;
        while ((n2 = read32le(p = cur_text_section->data + n1)))
            n1 = n2;
        write32le(p, t);
        t = n;
    }
    return t;
}

// op = gt, gtu, eqc, diff
// t = absoulte address
ST_FUNC int gjmp_cond(int op, int t)
{
	g(0xF0 | op);

	// Areg holds result of operation Areg 
	
	t = gjmp2(0x0A, t);	// handle return address and jump to address t
    	return t;
}

/* 	Indirect Functions 	*/

// v1 = local offset of data block from source
// v2 = same for destination
// l1 = length of v1 block in bytes
// l2 = same for v2 block
void Transputer_MOVE(int v1, int v2, int l1, int l2)
{
	ALWAYS_ASSERT((l1 > l2) || BETWEEN(v1,l1,v2,l2) \
			|| (v1 % ALIGNEMENT || v2 % ALIGNEMENT));

	Transputer_LDLP(v1);
	Transputer_LDLP(v2);
	Transputer_LDC(l1);
	gen_le16(0x24FA);
	// out_op(TP_OP_MOVE);
}

/*void Transputer_GCALL()
{
	gen_le8(0xF6);
}*/

void Transputer_GAJW() 
{
	gen_le16(0x23FC);
}

void Transputer_AJW(int v) 
{
	g(0x20 | get_prefix(v));
	g(0xB | (0x0F & v));
}

void Transputer()
{
	gen_le16(0x23F2); //ge_l16
}

void Transputer_UMINUS()
{
	Transputer_NOT();
	Transputer_ADC(1);
}

void Transputer_NOP() 
{
	gen_le16(0x63F0);
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
	gen_le16(0x25F2);
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
	gen_le16(0x24FB);
	// out_op(TP_OP_OR);
}

void Transputer_DIV()
{
	gen_le16(0x22FC);
	// out_op(TP_OP_DIV);
}

void Transputer_AND()
{
	gen_le16(0x24F6);
	// out_op(TP_OP_AND);
}

void Transputer_LDPI()
{
	gen_le16(0x21FB);
	// out_op(TP_OP_LDPI);
}

void Transputer_SHR()
{
	gen_le16(0x24F0);
	// out_op(TP_OP_SHR);
}

void Transputer_LADD()
{
	gen_le16(0x21F6);
	// out_op(TP_OP_LADD);
}

void Transputer_LSUB()
{
	gen_le16(0x23F8);
	// out_op(TP_OP_LSUB);
}

void Transputer_LMUL()
{
	gen_le16(0x23F1);
	// out_op(TP_OP_LMUL);
}

#if TARGET_CPU_DEFAULT == 9000
void Transputer_XBWORD()
{
	gen_le16(0x2BF8);
}
#endif

void Transputer_XWORD() 
{
	gen_le16(0x23FA);
	// out_op(TP_OP_XWORD);
}

void Transputer_LDIV()
{
	gen_le16(0x21FA);
	// out_op(TP_OP_LDIV);
}


void Transputer_SHL()
{
	gen_le16(0x24F1);
	// out_op(TP_OP_SHL);
}

void Transputer_FPLDNLSN()
{
	gen_le16(0x28FE);
	// out_op(TP_OP_FPLDNLSN);
}

void Transputer_FPSTNLSN()
{
	gen_le16(0x28F8);
}

void Transputer_FPSTNLDB()
{
	gen_le16(0x28F4);
}

void Transputer_FPLDNLDBI()
{
	gen_le16(0x28F2);
}

void Transputer_FPLDNLSNI()
{
	gen_le16(0x28F6);
}

void Transputer_FPLDNLDB()
{
	gen_le16(0x28FA);
}

/*	Direct Functions 	*/

// v = offset from Wpreg
void Transputer_STL(int v)
{
	g(0x20 | get_prefix(v));
	g(0xD0 | 0x0F & v);
	// out_op(TP_OP_STL);
}

void Transputer_J(int v)
{
	gen_le16((0x02 << 12) | \
		 (get_prefix(v) << 8) | \
		 (0x0F & v));
}

void Transputer_DUP() 
{
	gen_le16(0x25FA);
	// out_op(TP_OP_DUP);
}

void Transputer_STNL(int v)
{
	g(0x20 | get_prefix(v));
	g(0xE0 | 0x0F & v);
	// out_op(TP_OP_STNL);
}

// v = Constant 
void Transputer_LDC(int v)
{
	g(0x20 | get_prefix(v));
	g(0x40 | 0x0F & v);
	// out_op(TP_OP_LDC);
}

// Load non-local address relative from Areg 
void Transputer_LDNL(int v)
{
	g(0x20 | get_prefix(v));
	g(0x30 | v & 0x0F);
	// out_op(TP_OP_LDNL);
}

// v = offset from wpreg as address 
void Transputer_LDL(int v)
{
	g(0x20 | get_prefix(v));
	g(0x70 | 0x0F & v);
	// out_op(TP_OP_LDL);
}

// v = offset from Areg relative address
void Transputer_LDLP(int v) 
{
	g(0x20 | get_prefix(v));
	g(0x10 | 0x0F & v);
	//out_op(TP_OP_LDLP);
}

void Transputer_EQC(int v)
{
	g(0x20 | get_prefix(v));
	g(0xC0 | 0x0F & v);
}

void Transputer_LDNLP(int v)
{
	g(0x20 | get_prefix(v));
	g(0x50 | 0x0F & v);
}

void Transputer_ADC(int v)
{
	g(0x20 | get_prefix(v));
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
	gen_le16(0x28F7);
}

void Transputer_FPLDNLADDSN()
{
#if TARGET_CPU_DEFAULT == 9000 || TARGET_CPU_DEFAULT == 801
	gen_le16(0x2AFA);
#else
	Transputer_FPLDNLSN();
	//Transputer_FPRX();
	Transputer_FPADD();
#endif
}

void Transputer_FPLDNLADDDB()
{
#if TARGET_CPU_DEFAULT == 9000 || TARGET_CPU_DEFAULT == 801
	gen_le16(0x2AF6);
#else
	Transputer_FPLDNLDB();
	//Transputer_FPRX();
	Transputer_FPADD();
#endif
}

void Transputer_FPLDNLMULSN()
{
#if TARGET_CPU_DEFAULT == 9000 || TARGET_CPU_DEFAULT == 801
	gen_le16(0x2AFC);
#else
	Transputer_FPLDNLSN();
	//Transputer_FPRX();
	Transputer_FPMUL();
#endif
}

void Transputer_FPLDNLMULDB()
{
#if TARGET_CPU_DEFAULT == 9000 || TARGET_CPU_DEFAULT == 801
	gen_le16(0x2AF8);
#else
	Transputer_FPLDNLDB();
	//Transputer_FPRX();
	Transputer_FPMUL();
#endif
}

void Transputer_FPGT()
{
	gen_le16(0x29F4);
}

void Transputer_FPEQ()
{
	gen_le16(0x29F5);
}

void Transputer_FPGE()
{
	gen_le16(0x29F7);
}

void Transputer_FPREV()
{
	gen_le16(0x2AF4);
}

void Transputer_FPSUB() 
{
	gen_le16(0x28F9);
}

void Transputer_FPMUL() 
{
	gen_le16(0x28FB);
}

void Transputer_FPDIV() 
{
	gen_le16(0x28FC);
}

void Transputer_FPEXPDEC32() 
{
	gen_le16(0x2DF9);
}

void Transputer_FPEXPINC32() 
{
	gen_le16(0x2DFA);
}

void Transputer_FPRZ()
{
	gen_le16(0x2DF6);
}

void Transputer_FPRN() 
{
	gen_le16(0x2DF0);
}

void Transputer_FPI32TOR64()
{
	gen_le16(0x29F8);
	// out_op(TP_OP_FPI32TOR64);
}

void Transputer_FPB32TOR64()
{
	gen_le16(0x29FA);
	// out_op(TP_OP_FPB32TOR64);
}

void Transputer_FPI32TOR32()
{
	gen_le16(0x29F6);
	// out_op(TP_OP_FPI32TOR32);
}

void Transputer_FPUEXPDEC32()
{
	gen_le16(0x2DF9);
}

void Transputer_FPUCHKI32()
{
	gen_le16(0x2DFE);
}

void Transputer_FPUCLRERR()
{
	gen_le16(0x29FC);
}

void Transputer_FPSTNLI32()
{
	gen_le16(0x29FE);
}

void Transputer_FPINT()
{
	gen_le16(0x2AF1);
}

void Transputer_FPDUP()
{
	gen_le16(0x2AF3);
}

/* generate an integer binary operation */
void gen_opi(int op)
{
	int r, fr, ft, rt, opc;

	gv(REG_IST0);

	r = vtop->r;
	rt = vtop->type.t;

	//ALWAYS_ASSERT((ft & VT_BTYPE) == (rt & VT_BTYPE));

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
			opc = TP_OP_SHR;
			//out_op(TP_OP_SHR); //
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
			opc = TP_OP_REM;
			//out_op(TP_OP_REM); //
			goto std_op;
		case TOK_GT:
			opc = 0x09;
			//out_op(TP_OP_GT);
			goto std_op;
		case TOK_LT:
			opc = 0x09;
			vswap();
			goto std_op;
		case TOK_EQ:
			gv(RC_INT);
			Transputer_DIFF();
			Transputer_EQC(0);
			break;
		case TOK_NE:
			gv(RC_INT);
			Transputer_DIFF();
			break;
		case TOK_LE:
			gv(RC_INT);
			vswap();
			Transputer_GT();
			Transputer_EQC(0);
			break;
		case TOK_GE:
			gv(RC_INT);
			Transputer_LT();
			Transputer_EQC(0);
			break;
		std_shift:
			//ALWAYS_ASSERT(abs(v) < 32767);

			gv(REG_IST1);

			fr = vtop[-1].r;
			ft = vtop[-1].type.t;

			if ((rt & VT_BTYPE) == VT_PTR)
 				Transputer_LDLP(r);
    			else if ((r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) 
 				Transputer_LDC(vtop[-1].c.i);
			else 
				Transputer_LDL(r);

			Transputer_REV();

    			if ((ft & VT_BTYPE) == VT_PTR)
 				Transputer_LDLP(fr);
    			else if ((fr & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) 
 				Transputer_LDC(vtop->c.i);
			else 
				Transputer_LDL(fr);

			if ((r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) {
				gen_le16((0x02 << 12) | (get_prefix(opc) << 8) | (0xF0 | (0x0F & opc)));
			} else {
				if ((rt & VT_BTYPE) == VT_LONG)
					opc += -9;
			    	gen_le16((0x02 << 12) | (get_prefix(opc) << 8) | (0xF0 | (0x0F & opc)));
			}
			vtop--;
			break;
		std_op:
			//ALWAYS_ASSERT(abs(v) < 32767);

			gv(REG_IST1);

			if ((vtop->type.t & VT_BTYPE == VT_LONG) || (rt & VT_BTYPE == VT_LONG)) {

				fr = vtop->r;
				ft = vtop->type.t;

				// Convert to long and loading must be handled by LXXX_OP

				if (op == '+')
					Transputer_LADD_OP(&vtop[-1], &vtop[0], &vtop[-1]);
				else if (op == '-')
					Transputer_LSUB_OP(&vtop[-1], &vtop[0], &vtop[-1]);
				else if (op == '/')
					Transputer_LDIV_OP(&vtop[-1], &vtop[0], &vtop[-1]);
				else if (op == '*')
					Transputer_LMUL_OP(&vtop[-1], &vtop[0], &vtop[-1]);
				else
					gen_le16((0x02 << 12) | (get_prefix(opc) << 8) | (0xF0 | (0x0F & opc)));
			} else {
				if ((op == '+' || op == '-') && ((vtop[-1].r & VT_BTYPE) == VT_CONST)) {
					Transputer_ADC(vtop->c.i);
				} else {
					rt = vtop[-1].type.t;
		
					fr = vtop->r;
					ft = vtop->type.t;

					if ((rt & VT_BTYPE) == VT_PTR)
	 					Transputer_LDLP(r);
	    				else if ((rt & VT_BTYPE) == VT_CONST) 
	 					Transputer_LDC(vtop[-1].c.i);
					else 
						Transputer_LDL(r);
				
					Transputer_REV();

					if ((ft & VT_BTYPE) == VT_PTR)
		 				Transputer_LDLP(fr);
		    			else if ((ft & VT_BTYPE) == VT_CONST) 
		 				Transputer_LDC(vtop->c.i);
					else 
						Transputer_LDL(fr);
		
					if (op == '+' || op == '-' || op == TOK_GT) {
						g(0xF0 | opc);
					} else {
						gen_le16((0x02 << 12) | (get_prefix(opc) << 8) | (0xF0 | (0x0F & opc)));
					}
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
//    printf("gen_opf(op=%d)\n", op);
	
    	int ft, fr, r, rt;

	gv2(RC_FST0, RC_FST1);
	gv2(RC_IST0, RC_IST1);

    	ft = vtop[-1].type.t;
	rt = vtop->type.t;
    	r = vtop->r;
    	fr = vtop[-1].r;

    	if ((fr & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) {
 		if ((ft & VT_BTYPE) == VT_DOUBLE) {
			Transputer_LDC(vtop->c.d);
			Transputer_FPLDNLDB();
		} else {
			Transputer_LDC(vtop->c.f);
			Transputer_FPLDNLSN();
		}
	} else {
		Transputer_LDLP(fr);
		if ((ft & VT_BTYPE) == VT_DOUBLE)
			Transputer_FPLDNLDB();
		else
			Transputer_FPLDNLSN();
	}

	if ((rt & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST)
 		if ((rt & VT_BTYPE) == VT_DOUBLE)
			Transputer_LDC(vtop[0].c.d);
		else
			Transputer_LDC(vtop[0].c.f);
	else
		Transputer_LDLP(r);

	ALWAYS_ASSERT((ft & (VT_BTYPE | VT_DOUBLE)) == (rt & (VT_BTYPE | VT_DOUBLE)) || \
				(ft & (VT_BTYPE | VT_FLOAT)) == (rt & (VT_BTYPE | VT_FLOAT)));

	if (op == '+') {
		if ((rt & VT_BTYPE) == VT_DOUBLE)
			Transputer_FPLDNLADDDB();
		else
			Transputer_FPLDNLADDSN();
		//Transputer_FPADD();
		vtop--;
	} else if (op == '-') {
		Transputer_FPSUB();
		vtop--;
	} else if (op == '*') {
		if ((rt & VT_BTYPE) == VT_DOUBLE)
			Transputer_FPLDNLMULDB();
		else 
			Transputer_FPLDNLMULSN();
		//Transputer_FPMUL();
	    	vtop--;
	} else {

		if ((rt & VT_BTYPE) == VT_DOUBLE)
			Transputer_FPLDNLDB();
		else 
			Transputer_FPLDNLSN();

		if (op == '/') {
			Transputer_FPREV();		
			Transputer_FPDIV();
			vtop--;
		} else if (op == '%') {
			// TODO: FPRANGE 2^24 - 1 for float
			// and 2^51 - 1 for double
			Transputer_FPREM();
			vtop--;
		} else if (op == TOK_GT){
			Transputer_FPGT();
			vtop--;
		} else if (op == TOK_LE){
			Transputer_FPGT();
			Transputer_EQC(0);
			vtop--;
		} else if (op == TOK_LE){
			Transputer_FPREV();
			Transputer_FPGT();
			vtop--;
		} else if (op == TOK_GE){
			Transputer_FPGE();
			vtop--;
  		} else if (op == TOK_EQ){
			Transputer_FPEQ();
			vtop--;
  		} else if (op == TOK_NE){
			Transputer_FPGE();
			Transputer_EQC(0);
			vtop--;
  		} else {
	    		ALWAYS_ASSERT(FALSE);
     	}
	}
}

/* convert fp to int 't' type */
/* XXX: handle long long case */
ST_FUNC void gen_cvt_ftoi(int t)
{
	int r, r2;
    	//printf("gen_cvt_itof(t=%d)\n", t);

	// FIXME fpr, intr
	r = /*fpr*/(gv(RC_INT));
	r2 = /*intr*/(vtop->r=get_reg(RC_INT));

	if ((vtop->type.t & VT_BTYPE) == VT_INT) {
		Transputer_FPINT();
		Transputer_FPUCLRERR();
		Transputer_FPUCHKI32();
		Transputer_FPCHKERR();
		Transputer_LDLP(vtop->r);
		Transputer_FPSTNLI32();
	} else if ((vtop->type.t & VT_BTYPE) == VT_LONG) {
		Transputer_FPRZ();
		Transputer_FPINT();
		Transputer_LDLP(vtop->r);
		Transputer_FPDUP();
		Transputer_DUP();
		Transputer_FPSTNLI32();
		Transputer_LDNLP(1);
		Transputer_FPUEXPDEC32();
		Transputer_FPSTNLI32();
	}

	vtop->r = REG_IRET;
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
		if ((vtop->type.t & VT_BTYPE) == VT_FLOAT) {
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
#if TARGET_CPU_DEFAULT == 9000
		Transputer_XSWORD();
#else
		Transputer_REV();
		Transputer_LDC(4);
		Transputer_XWORD();	// Areg = length of type in bytes, Breg = content 
#endif	
	} else if ((vtop->type.t & VT_BTYPE) == VT_BYTE) {
		//Transputer_CB();
#if TARGET_CPU_DEFAULT == 9000
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
	int r, r2;
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
	vtop->r2 = 0;		// set this as unused
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

ST_FUNC void gen_le16(int v)
{
    g(v);
    g(v >> 8);
}

ST_FUNC void gen_le32(int c)
{
    g(c);
    g(c >> 8);
    g(c >> 16);
    g(c >> 24);
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

#endif
/* end of transputer code generator */
