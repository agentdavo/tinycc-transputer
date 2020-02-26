/*
 * Transputer 9000 support for TCC
 */
 
#define NB_REGS_9000 19

static char *add_reg_names[] = {
	"Statusreg",
	"Wdescreg",
	"Iptrreg",
	"Threg",
	"FPstatusreg",
	"Breg0",
	"Breg1",
	"Breg2",
	"WIreg",
	"Wureg",
	"Ereg",
	"Xreg",
	"Eptrreg",
	"Regionreg0",
	"Regionreg1",
	"Regionreg2",
	"Regionreg3",
	"Pstatereg",
	"WdescStubreg"
}

/* generate a floating point operation 'v = t1 op t2' instruction. The
 *    two operands are guaranteed to have the same floating point type */
static int transputer9000_opf(int op) 
{
  uint64_t x;
  int fneg = 0, r;
  
  c = 0;
  switch(op) {
    case '+':
      opc = 0x05;
      break;
    case '-':
      opc = 0x0c; 
      break;
    case ''
  }
}

