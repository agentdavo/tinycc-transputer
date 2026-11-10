/*
 * Minimal Transputer assembler used by TinyCC.
 * Derived from a standalone "tasm" example.
 */

#ifdef TARGET_DEFS_ONLY

/* When included from tcc.h - only forward declarations */
#define CONFIG_TCC_ASM
#define NB_ASM_REGS 14

ST_FUNC void asm_opcode(TCCState *s1, int t);

#else /* !TARGET_DEFS_ONLY */

/* When included from libtcc.c - full implementation */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include "transputer-opcodes.h"

#ifdef TCC_TARGET_TRANSPUTER
# define USING_GLOBALS
# include "tcc.h"
# include "transputer-enc.h"  /* Unified prefix encoding */
/* use tcc memory helpers when compiled as part of the compiler */
# undef malloc
# undef calloc
# undef realloc
# undef strdup
# undef free
# define malloc  tcc_malloc
# define calloc(n,s) tcc_mallocz((n)*(s))
# define realloc tcc_realloc
# define strdup  tcc_strdup
# define free    tcc_free
#endif

/* --- Core Definitions & Constants --- */

#define MAX_LINE_LENGTH 256
#define HASH_TABLE_SIZE 1024
#define MAX_PASSES 10

#define NOT_PROCESS_P ((int)0x80000000)

/* Represents an instruction or data item during assembly */
typedef struct AsmItem {
    int address;
    int size;
    int line_number;
    const char *filename;
    enum {
        ITEM_INSTR,
        ITEM_DATA_BYTE,
        ITEM_DATA_WORD,
        ITEM_PAD
    } type;
    int opcode;
    int operation;
    char *operand_expr;
    uint8_t *data_bytes;
    int data_len;
    struct AsmItem *next;
} AsmItem;

/* Label entry in the symbol table */
typedef struct Label {
    char *name;
    int value;
    bool is_resolved;
    char *equ_expr;
    int defined_line;
    const char *defined_filename;
    AsmItem *owner_item;
    struct Label *next;
} Label;

/* Global assembler state */
typedef struct AssemblerState {
    AsmItem *first_item;
    AsmItem *last_item;
    Label *label_table[HASH_TABLE_SIZE];
    int current_address;
    int current_line;
    const char *current_filename;
    int error_count;
    bool fatal_error;
    char line_buffer[MAX_LINE_LENGTH];
    char *line_ptr;
    Label *current_address_label;
} AssemblerState;

/* Instruction table.  code is a direct function 0..15, -(operation) for a
   secondary, or >= 0x20000 for the extended forms below.  rev is
   operation 0, which -0 cannot tell apart from j, so it gets its own
   marker; decode with tp_asm_operation(). */
typedef struct {
    const char *mnemonic;
    int code;
} InstructionInfo;

#define TP_ASM_REV (-0x10000)

static int tp_asm_operation(int code)
{
    return code == TP_ASM_REV ? 0 : -code;
}

static const InstructionInfo instructions[] = {
    {"j", TP_OP_J},  {"ldlp", TP_OP_LDLP}, {"pfix", TP_OP_PFIX}, {"ldnl", TP_OP_LDNL},
    {"ldc", TP_OP_LDC}, {"ldnlp", TP_OP_LDNLP}, {"nfix", TP_OP_NFIX}, {"ldl", TP_OP_LDL},
    {"adc", TP_OP_ADC}, {"call", TP_OP_CALL}, {"cj", TP_OP_CJ},   {"ajw", TP_OP_AJW},
    {"eqc", TP_OP_EQC}, {"stl", TP_OP_STL},  {"stnl", TP_OP_STNL}, {"opr", TP_OP_OPR},
    {"rev", TP_ASM_REV}, {"lb", -TP_OPR_LB}, {"bsub", -TP_OPR_BSUB}, {"endp", -TP_OPR_ENDP},
    {"diff", -TP_OPR_DIFF}, {"add", -TP_OPR_ADD}, {"gcall", -TP_OPR_GCALL}, {"in", -TP_OPR_IN},
    {"prod", -TP_OPR_PROD}, {"gt", -TP_OPR_GT}, {"wsub", -TP_OPR_WSUB}, {"out", -TP_OPR_OUT},
    {"sub", -TP_OPR_SUB}, {"startp", -TP_OPR_STARTP}, {"outbyte", -TP_OPR_OUTBYTE}, {"outword", -TP_OPR_OUTWORD},
    {"seterr", -TP_OPR_SETERR}, {"csub0", -TP_OPR_CSUB0}, {"stopp", -TP_OPR_STOPP}, {"ladd", -TP_OPR_LADD},
    {"stlb", -TP_OPR_STLB}, {"sthf", -TP_OPR_STHF}, {"norm", -TP_OPR_NORM}, {"ldiv", -TP_OPR_LDIV},
    {"ldpi", -TP_OPR_LDPI}, {"stlf", -TP_OPR_STLF}, {"xdble", -TP_OPR_XDBLE}, {"ldpri", -TP_OPR_LDPRI},
    {"rem", -TP_OPR_REM}, {"ret", -TP_OPR_RET}, {"lend", -TP_OPR_LEND}, {"ldtimer", -TP_OPR_LDTIMER},
    {"tin", -TP_OPR_TIN}, {"div", -TP_OPR_DIV}, {"lmul", -TP_OPR_LMUL}, {"not", -TP_OPR_NOT},
    {"xor", -TP_OPR_XOR}, {"bcnt", -TP_OPR_BCNT}, {"lshr", -TP_OPR_LSHR}, {"lshl", -TP_OPR_LSHL},
    {"lsum", -TP_OPR_LSUM}, {"lsub", -TP_OPR_LSUB}, {"runp", -TP_OPR_RUNP}, {"xword", -TP_OPR_XWORD},
    {"sb", -TP_OPR_SB}, {"gajw", -TP_OPR_GAJW}, {"savel", -TP_OPR_SAVEL}, {"saveh", -TP_OPR_SAVEH},
    {"wcnt", -TP_OPR_WCNT}, {"shr", -TP_OPR_SHR}, {"shl", -TP_OPR_SHL}, {"mint", -TP_OPR_MINT},
    {"alt", -TP_OPR_ALT}, {"altwt", -TP_OPR_ALTWT}, {"altend", -TP_OPR_ALTEND}, {"and", -TP_OPR_AND},
    {"enbt", -TP_OPR_ENBT}, {"enbc", -TP_OPR_ENBC}, {"enbs", -TP_OPR_ENBS}, {"move", -TP_OPR_MOVE},
    {"or", -TP_OPR_OR}, {"csngl", -TP_OPR_CSNGL}, {"ccnt1", -TP_OPR_CCNT1}, {"talt", -TP_OPR_TALT},
    {"ldiff", -TP_OPR_LDIFF}, {"sthb", -TP_OPR_STHB}, {"taltwt", -TP_OPR_TALTWT}, {"sum", -TP_OPR_SUM},
    {"mul", -TP_OPR_MUL}, {"sttimer", -TP_OPR_STTIMER}, {"stoperr", -TP_OPR_STOPERR}, {"cword", -TP_OPR_CWORD},
    {"clrhalterr", -TP_OPR_CLRHALTERR}, {"sethalterr", -TP_OPR_SETHALTERR}, {"testhalterr", -TP_OPR_TESTHALTERR},
    {"dup", -TP_OPR_DUP}, {"move2dinit", -TP_OPR_MOVE2DINIT}, {"move2dall", -TP_OPR_MOVE2DALL},
    {"move2dnonzero", -TP_OPR_MOVE2DNONZERO}, {"move2dzero", -TP_OPR_MOVE2DZERO}, {"gtu", -TP_OPR_GTU},
    {"fmul", -TP_OPR_FMUL},
    {"crcword", -TP_OPR_CRCWORD}, {"crcbyte", -TP_OPR_CRCBYTE}, {"bitcnt", -TP_OPR_BITCNT},
    {"bitrevword", -TP_OPR_BITREVWORD}, {"bitrevnbits", -TP_OPR_BITREVNBITS}, {"wsubdb", -TP_OPR_WSUBDB},
    {"terminate", -TP_OPR_ENDP},
    {"fpldnlsn", -TP_FPU_LDNLSN}, {"fpldnldb", -TP_FPU_LDNLDB}, {"fpstnlsn", -TP_FPU_STNLSN},
    {"fpstnldb", -TP_FPU_STNLDB}, {"fpadd", -TP_OPR_FPADD}, {"fpsub", -TP_OPR_FPSUB},
    {"fpmul", -TP_OPR_FPMUL}, {"fpdiv", -TP_OPR_FPDIV}, {"fpremfirst", -TP_OPR_FPREMFIRST},
    {"fpremstep", -TP_OPR_FPREMSTEP}, {"fpint", -TP_OPR_FPINT}, {"fprev", -TP_OPR_FPREV},
    {"fpentry", -TP_OPR_FPENTRY}, {"fptesterr", -TP_OPR_FPTESTERR},
    {"diss", -TP_OPR_DISS},
    {"disc", -TP_OPR_DISC},
    {"dist", -TP_OPR_DIST},
    {"testerr", -TP_OPR_TESTERR},
    {"resetch", -TP_OPR_RESETCH},
    {"testpranal", -TP_OPR_TESTPRANAL},
    {"unpacksn", -TP_OPR_UNPACKSN},
    {"roundsn", -TP_OPR_ROUNDSN},
    {"postnormsn", -TP_OPR_POSTNORMSN},
    {"ldinf", -TP_OPR_LDINF},
    {"cflerr", -TP_OPR_CFLERR},
    {"fpdup", -TP_OPR_FPDUP},
    {"fpldnlsni", -TP_OPR_FPLDNLSNI},
    {"fpldnldbi", -TP_OPR_FPLDNLDBI},
    {"fpldzerosn", -TP_OPR_FPLDZEROSN},
    {"fpldzerodb", -TP_OPR_FPLDZERODB},
    {"fpldnladdsn", -TP_OPR_FPLDNLADDSN},
    {"fpldnladddb", -TP_OPR_FPLDNLADDDB},
    {"fpldnlmulsn", -TP_OPR_FPLDNLMULSN},
    {"fpldnlmuldb", -TP_OPR_FPLDNLMULDB},
    {"fpsttest", -TP_OPR_FPSTTEST},
    {"fpchkerr", -TP_FPU_CHKERR},
    {"fpldtest", -TP_OPR_FPLDTEST},
    {"fpgt", -TP_OPR_FPGT},
    {"fprange", -TP_OPR_FPRANGE},
    {"fpge", -TP_OPR_FPGE},
    {"fpeq", -TP_OPR_FPEQ},
    {"fpordered", -TP_OPR_FPORDERED},
    {"fpnan", -TP_OPR_FPNAN},
    {"fpnotfinite", -TP_OPR_FPNOTFINITE},
    {"fpstnli32", -TP_FPU_STNLI32},
    {"fprtoi32", -TP_FPU_RTOI32},
    {"fpi32tor32", -TP_FPU_I32TOR32},
    {"fpi32tor64", -TP_FPU_I32TOR64},
    {"fpb32tor64", -TP_OPR_FPB32TOR64},
    {"fplg", -TP_OPR_FPLG},
    /* TVX - Transputer Vector eXtension (MXFP4 / bfloat16) */
    {"bf16tor32", -TP_OPR_BF16TOR32},
    {"r32tobf16", -TP_OPR_R32TOBF16},
    {"bf16pack", -TP_OPR_BF16PACK},
    {"bf16unpkh", -TP_OPR_BF16UNPKH},
    {"mxfp4tor32", -TP_OPR_MXFP4TOR32},
    {"r32tomxfp4", -TP_OPR_R32TOMXFP4},
    {"mxe8m0tor32", -TP_OPR_MXE8M0TOR32},
    {"mxe8m0of", -TP_OPR_MXE8M0OF},
    {"mxfp4dot", -TP_OPR_MXFP4DOT},
    {"bf16dot", -TP_OPR_BF16DOT},
    {"mxfp4gemv", -TP_OPR_MXFP4GEMV},
    {"mxfp4unpk8", -TP_OPR_MXFP4UNPK8},
    {"r32tomxfp4x8", -TP_OPR_R32TOMXFP4X8},
    {"bf16mul2", -TP_OPR_BF16MUL2},
    {"bf16add2", -TP_OPR_BF16ADD2},
    {"vsetlen", -TP_OPR_VSETLEN},
    {"mxfp4gemvbf", -TP_OPR_MXFP4GEMVBF},
    {"vaddsn", -TP_OPR_VADDSN},
    {"vmulsn", -TP_OPR_VMULSN},
    {"vsilusn", -TP_OPR_VSILUSN},
    {"vrmsnorm", -TP_OPR_VRMSNORM},
    {"vsoftmaxsn", -TP_OPR_VSOFTMAXSN},
    {"f16tor32", -TP_OPR_F16TOR32},
    {"r32tof16", -TP_OPR_R32TOF16},
    {"q8dot", -TP_OPR_Q8DOT},
    {"q8dotbf", -TP_OPR_Q8DOTBF},
    {"iq1mdot", -TP_OPR_IQ1MDOT},
    {"iq2xxsdot", -TP_OPR_IQ2XXSDOT},
    {"iq3xxsdot", -TP_OPR_IQ3XXSDOT},
    {"qsetfmt", -TP_OPR_QSETFMT},
    {"qdot", -TP_OPR_QDOT},
    {"vhad32", -TP_OPR_VHAD32},
    {"mxqrot", -TP_OPR_MXQROT},
    {"mxqrotbf", -TP_OPR_MXQROTBF},
    {"ftsetvp", -TP_OPR_FTSETVP},
    {"ftransp", -TP_OPR_FTRANSP},
    {"ftranspb", -TP_OPR_FTRANSPB},
    {"fsincos", -TP_OPR_FSINCOS},
    {"frsqrt", -TP_OPR_FRSQRT},
    {"frecip", -TP_OPR_FRECIP},
    {"fsincosb", -TP_OPR_FSINCOSB},
    {"vsetstride", -TP_OPR_VSETSTRIDE},
    {"ftcull", -TP_OPR_FTCULL},
    {"tskin", -TP_OPR_TSKIN},
    {"vsetfmt", -TP_OPR_VSETFMT},
    {"ftsetlit", -TP_OPR_FTSETLIT},
    {"flitb", -TP_OPR_FLITB},
    {"qgemm", -TP_OPR_QGEMM},
    {"vblend8", -TP_OPR_VBLEND8},
    {"vfill", -TP_OPR_VFILL},
    {"vfill2d", -TP_OPR_VFILL2D},
    {"csum", -TP_OPR_CSUM},
    {"vblend2d", -TP_OPR_VBLEND2D},
    {"stvxstate", -TP_OPR_STVXSTATE},
    {"csumcpy", -TP_OPR_CSUMCPY},
    {"vq8k", -TP_OPR_VQ8K},
    {"vq80", -TP_OPR_VQ80},
    {"vcvt", -TP_OPR_VCVT},
    {"vxfer", -TP_OPR_VXFER},
    {"fpusqrtfirst", 0x20000 | TP_FPENTRY_SQRTFIRST},
    {"fpusqrtstep", 0x20000 | TP_FPENTRY_SQRTSTEP},
    {"fpusqrtlast", 0x20000 | TP_FPENTRY_SQRTLAST},
    {"fpurp", 0x20000 | TP_FPENTRY_RP},
    {"fpurm", 0x20000 | TP_FPENTRY_RM},
    {"fpurz", 0x20000 | TP_FPENTRY_RZ},
    {"fpur32tor64", 0x20000 | TP_FPENTRY_R32TOR64},
    {"fpur64tor32", 0x20000 | TP_FPENTRY_R64TOR32},
    {"fpuexpdec32", 0x20000 | TP_FPENTRY_EXPDEC32},
    {"fpuexpinc32", 0x20000 | TP_FPENTRY_EXPINC32},
    {"fpuabs", 0x20000 | TP_FPENTRY_ABS},
    {"fpunoround", TP_FPU_OP_NOROUND},
    {"fpuchki32", TP_FPU_OP_CHKI32},
    {"fpuchki64", TP_FPU_OP_CHKI64},
    {"fpudivby2", TP_FPU_OP_DIVBY2},
    {"fpumulby2", TP_FPU_OP_MULBY2},
    {"fpurn", 0x20000 | TP_FPENTRY_URN},
    {"fpuseterr", 0x20000 | TP_FPENTRY_USETERR},
    {"fpuclrerr", 0x20000 | TP_FPENTRY_UCLRERR},
    {"start", -TP_OPR_START},
    {"testhardchan", -TP_OPR_TESTHARDCHAN},
    {"testldd", -TP_OPR_TESTLDD},
    {"teststd", -TP_OPR_TESTSTD},
    {"testlde", -TP_OPR_TESTLDE},
    {"testste", -TP_OPR_TESTSTE},
    {"testlds", -TP_OPR_TESTLDS},
    {"teststs", -TP_OPR_TESTSTS},
    {"settimeslice", -TP_OPR_SETTIMESLICE},
    {"break", -TP_OPR_BREAK},
    {"clrj0break", -TP_OPR_CLRJ0BREAK},
    {"setj0break", -TP_OPR_SETJ0BREAK},
    {"testj0break", -TP_OPR_TESTJ0BREAK},
    {"ldflags", -TP_OPR_LDFLAGS},
    {"stflags", -TP_OPR_STFLAGS},
    {"xbword", -TP_OPR_XBWORD},
    {"lbx", -TP_OPR_LBX},
    {"cb", -TP_OPR_CB},
    {"cbu", -TP_OPR_CBU},
    {"insphdr", -TP_OPR_INSPHDR},
    {"readbfr", -TP_OPR_READBFR},
    {"ldconf", -TP_OPR_LDCONF},
    {"stconf", -TP_OPR_STCONF},
    {"ldcnt", -TP_OPR_LDCNT},
    {"ssub", -TP_OPR_SSUB},
    {"ldth", -TP_OPR_LDTH},
    {"ldchstatus", -TP_OPR_LDCHSTATUS},
    {"intdis", -TP_OPR_INTDIS},
    {"intenb", -TP_OPR_INTENB},
    {"cir", -TP_OPR_CIR},
    {"ss", -TP_OPR_SS},
    {"chantype", -TP_OPR_CHANTYPE},
    {"ls", -TP_OPR_LS},
    {"fpseterr", -TP_OPR_FPSETERR},
    {"ciru", -TP_OPR_CIRU},
    {"fprem", -TP_OPR_FPREM},
    {"fprn", -TP_OPR_FPRN},
    {"fpdivby2", -TP_OPR_FPDIVBY2},
    {"fpmulby2", -TP_OPR_FPMULBY2},
    {"fpsqrt", -TP_OPR_FPSQRT},
    {"fprp", -TP_OPR_FPRP},
    {"fprm", -TP_OPR_FPRM},
    {"fprz", -TP_OPR_FPRZ},
    {"fpr32tor64", -TP_OPR_FPR32TOR64},
    {"fpr64tor32", -TP_OPR_FPR64TOR32},
    {"fpexpdec32", -TP_OPR_FPEXPDEC32},
    {"fpexpinc32", -TP_OPR_FPEXPINC32},
    {"fpabs", -TP_OPR_FPABS},
    {"fpclrerr", -TP_OPR_FPCLRERR},
    {"fpadddbsn", -TP_OPR_FPADDDBSN},
    {"fpchki32", -TP_OPR_FPCHKI32},
    {"fpchki64", -TP_OPR_FPCHKI64},
    {"devlb", -TP_OPR_DEVLB},
    {"devsb", -TP_OPR_DEVSB},
    {"devls", -TP_OPR_DEVLS},
    {"devss", -TP_OPR_DEVSS},
    {"devlw", -TP_OPR_DEVLW},
    {"devsw", -TP_OPR_DEVSW},
    {"xsword", -TP_OPR_XSWORD},
    {"lsx", -TP_OPR_LSX},
    {"cs", -TP_OPR_CS},
    {"csu", -TP_OPR_CSU},
    {"timerdisableh", -TP_OPR_TIMERDISABLEH},
    {"timerdisablel", -TP_OPR_TIMERDISABLEL},
    {"timerenableh", -TP_OPR_TIMERENABLEH},
    {"timerenablel", -TP_OPR_TIMERENABLEL},
    {"ldmemstartval", -TP_OPR_LDMEMSTARTVAL},
    {"pop", -TP_OPR_POP},
    {"lddevid", -TP_OPR_LDDEVID},
    /* T9000 protection / trap (ISM App.B nfix encodings → final OReg):
     *   selth −7, tret −5, goprot −6, syscall −8  →  60 F9 / FB / FA / F8
     * Raw nfix0;opr path via add_t9000_protect_opr (not tp_encode_instr). */
    {"selth",   0x30007},
    {"tret",    0x30005},
    {"goprot",  0x30006},
    {"syscall", 0x30008},
    /* T9000 semaphores (ISM 8.6): wait 60 F5, signal 60 F4 */
    {"wait",    0x30000 - TP_OPR_WAIT},
    {"signal",  0x30000 - TP_OPR_SIGNAL},
    /* T9000 VCP and resource channels (ISM App.B), same nfix encoding:
     * OReg = -(code & 0xFF), nfix 1 for -0x11..-0x20, nfix 2 below. */
    {"stopch",    0x30000 - TP_OPR_STOPCH},
    {"vout",      0x30000 - TP_OPR_VOUT},
    {"vin",       0x30000 - TP_OPR_VIN},
    {"swapbfr",   0x30000 - TP_OPR_SWAPBFR},
    {"sethdr",    0x30000 - TP_OPR_SETHDR},
    {"setchmode", 0x30000 - TP_OPR_SETCHMODE},
    {"initvlcb",  0x30000 - TP_OPR_INITVLCB},
    {"writehdr",  0x30000 - TP_OPR_WRITEHDR},
    {"readhdr",   0x30000 - TP_OPR_READHDR},
    {"disg",      0x30000 - TP_OPR_DISG},
    {"enbg",      0x30000 - TP_OPR_ENBG},
    {"grant",     0x30000 - TP_OPR_GRANT},
    {"unmkrc",    0x30000 - TP_OPR_UNMKRC},
    {"mkrc",      0x30000 - TP_OPR_MKRC},
    {"irdsq",     0x30000 - TP_OPR_IRDSQ},
    {"erdsq",     0x30000 - TP_OPR_ERDSQ},
    {"stresptr",  0x30000 - TP_OPR_STRESPTR},
    {"ldresptr",  0x30000 - TP_OPR_LDRESPTR},
    {NULL, 0}
};

/* Forward declarations */
static void report_error(AssemblerState *as, const char *msg, const char *data);
static int evaluate_expression(AssemblerState *as, const char *expr, bool *error);
static void parse_line(AssemblerState *as);
static void parse_file(AssemblerState *as, FILE *f, const char *name);

/* Utility functions */
static unsigned int hash_string(const char *s) {
    unsigned int hash = 5381;
    int c;
    while ((c = *s++))
        hash = ((hash << 5) + hash) + tolower(c);
    return hash % HASH_TABLE_SIZE;
}

static int tp_asm_stricmp(const char *a, const char *b) {
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/* Both assembler entry points use the same case-insensitive lookup.  The
 * table's mnemonics are lowercase; reject different initials before comparing
 * the rest, especially for the many extended secondary instructions. */
static const InstructionInfo *find_instr(const char *name)
{
    const InstructionInfo *p;
    int first = tolower((unsigned char)*name);

    for (p = instructions; p->mnemonic; p++)
        if (p->mnemonic[0] == first
            && tp_asm_stricmp(p->mnemonic + 1, name + 1) == 0)
            return p;
    return NULL;
}

static char *strdup_lower(const char *s) {
    char *r;
    char *p;
    if (!s) return NULL;
    r = strdup(s);
    if (r) for (p = r; *p; p++) *p = tolower(*p);
    return r;
}

/* Assembler state management */
static AssemblerState *create_assembler(void) {
    AssemblerState *as = calloc(1, sizeof(*as));
    if (!as) return NULL;
    as->current_address_label = calloc(1, sizeof(Label));
    as->current_address_label->name = strdup("$");
    as->current_address_label->is_resolved = true;
    return as;
}

static void free_assembler(AssemblerState *as) {
    AsmItem *item;
    int i;
    if (!as) return;
    item = as->first_item;
    while (item) {
        AsmItem *n = item->next;
        free(item->operand_expr);
        free(item->data_bytes);
        free(item);
        item = n;
    }
    for (i = 0; i < HASH_TABLE_SIZE; i++) {
        Label *l = as->label_table[i];
        while (l) {
            Label *n = l->next;
            free(l->name);
            free(l->equ_expr);
            free(l);
            l = n;
        }
    }
    free(as->current_address_label->name);
    free(as->current_address_label);
    free(as);
}

/* Label management */
static Label *find_label(AssemblerState *as, const char *name) {
    unsigned int idx;
    Label *l;
    if (tp_asm_stricmp(name, "$") == 0) {
        as->current_address_label->value = as->current_address;
        return as->current_address_label;
    }
    idx = hash_string(name);
    for (l = as->label_table[idx]; l; l = l->next)
        if (tp_asm_stricmp(l->name, name) == 0) return l;
    return NULL;
}

static Label *define_label(AssemblerState *as, const char *name) {
    unsigned int idx;
    Label *l;
    if (find_label(as, name)) {
        report_error(as, "Label redefined", name);
        return NULL;
    }
    idx = hash_string(name);
    l = calloc(1, sizeof(*l));
    l->name = strdup_lower(name);
    l->value = as->current_address;
    l->is_resolved = true;
    l->defined_line = as->current_line;
    l->defined_filename = as->current_filename;
    l->next = as->label_table[idx];
    as->label_table[idx] = l;
    return l;
}

static void define_equ(AssemblerState *as, const char *name, const char *expr) {
    Label *l;
    bool error = false;
    int val;
    if (!name || !*name) {
        report_error(as, "EQU directive requires a label", NULL);
        return;
    }
    l = find_label(as, name);
    if (!l) {
        report_error(as, "EQU applied to an undefined label", name);
        return;
    }
    if (l->defined_line != as->current_line || l->defined_filename != as->current_filename) {
        report_error(as, "EQU must be on the same line as the label definition", name);
        return;
    }
    val = evaluate_expression(as, expr, &error);
    if (error) {
        l->is_resolved = false;
        l->equ_expr = strdup(expr);
    } else {
        l->value = val;
    }
}

/* Item management */
static AsmItem *add_item(AssemblerState *as, int size) {
    AsmItem *it = calloc(1, sizeof(*it));
    it->address = as->current_address;
    it->line_number = as->current_line;
    it->filename = as->current_filename;
    it->size = size;
    if (as->last_item) as->last_item->next = it;
    else as->first_item = it;
    as->last_item = it;
    as->current_address += size;
    return it;
}

static void add_instruction_item(AssemblerState *as, int code, const char *expr) {
    AsmItem *it = add_item(as, 1);
    it->type = ITEM_INSTR;
    it->opcode = (code >= 0) ? code : 0xF;
    it->operation = (code < 0) ? tp_asm_operation(code) : -1;
    it->operand_expr = expr ? strdup(expr) : NULL;
    {
        const char *env = getenv("T4_ASM_RELOC_TRACE");
        if (env && *env) {
            fprintf(stderr, "ASM_INSTR: file=%s line=%d opcode=0x%x op=0x%x expr=%s\n",
                    as->current_filename ? as->current_filename : "<unknown>",
                    as->current_line, it->opcode, it->operation,
                    it->operand_expr ? it->operand_expr : "<none>");
        }
    }
}

static void add_immediate_instruction(AssemblerState *as, int opcode, int imm) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", imm);
    add_instruction_item(as, opcode, buf);
}

static void add_fpentry_sequence(AssemblerState *as, int op) {
    int imm = op & 0xFFFF;
    add_immediate_instruction(as, 0x4, imm); /* ldc imm */
    add_instruction_item(as, -0xAB, NULL);  /* fpentry */
}

/* T9000 App.B negative secondaries (ISM gold OReg, not App.B hex as pfix value).
 * nfix k; opr X → OReg = (~k << 4) | X, so k = 0 covers −16..−1:
 *   selth −7 → X=9 (60 F9); tret −5 → X=B (60 FB);
 *   goprot −6 → X=A (60 FA); syscall −8 → X=8 (60 F8);
 * and the VCP instructions need k = 1 or 2: vout −0x13 → 61 FD. */
static void add_t9000_protect_opr(AssemblerState *as, int neg_oreg)
{
    uint8_t bytes[2];
    AsmItem *it;
    bytes[0] = (uint8_t)(0x60 | (~((unsigned)neg_oreg >> 4) & 0xF));
    bytes[1] = (uint8_t)(0xF0 | (neg_oreg & 0xF));
    it = add_item(as, 2);
    it->type = ITEM_DATA_BYTE;
    it->data_bytes = malloc(2);
    it->data_len = 2;
    memcpy(it->data_bytes, bytes, 2);
}

static void add_data_item(AssemblerState *as, int size, const char *expr) {
    AsmItem *it = add_item(as, size);
    it->type = (size == 1) ? ITEM_DATA_BYTE : ITEM_DATA_WORD;
    it->operand_expr = strdup(expr);
}

/* Expression evaluation */
static int parse_term(AssemblerState *as, const char **expr, bool *error);
static int parse_sum(AssemblerState *as, const char **expr, bool *error);

static int parse_factor(AssemblerState *as, const char **expr, bool *error) {
    int res = parse_term(as, expr, error);
    char op;
    int rhs;
    if (*error) return 0;
    while (1) {
        while (isspace(**expr)) (*expr)++;
        op = **expr;
        if (op != '*' && op != '/') break;
        (*expr)++;
        rhs = parse_term(as, expr, error);
        if (*error) return 0;
        if (op == '*') res *= rhs; else {
            if (rhs == 0) { report_error(as, "Division by zero", NULL); *error = true; return 0; }
            res /= rhs;
        }
    }
    return res;
}

static int parse_sum(AssemblerState *as, const char **expr, bool *error) {
    int res = parse_factor(as, expr, error);
    char op;
    int rhs;
    if (*error) return 0;
    while (1) {
        while (isspace(**expr)) (*expr)++;
        op = **expr;
        if (op != '+' && op != '-') break;
        (*expr)++;
        rhs = parse_factor(as, expr, error);
        if (*error) return 0;
        if (op == '+') res += rhs; else res -= rhs;
    }
    return res;
}

static int parse_term(AssemblerState *as, const char **expr, bool *error) {
    int v;
    char label_name[MAX_LINE_LENGTH];
    int i = 0;
    Label *l;
    char *end;
    long val;
    
    while (isspace(**expr)) (*expr)++;
    if (**expr == '(') {
        (*expr)++;
        v = parse_sum(as, expr, error);
        if (*error) return v;
        while (isspace(**expr)) (*expr)++;
        if (**expr != ')') { report_error(as, "Mismatched parentheses", NULL); *error = true; return 0; }
        (*expr)++;
        return v;
    }
    if (isalpha(**expr) || **expr == '_' || **expr == '$' || **expr == '.') {
        while (i < MAX_LINE_LENGTH-1 && (isalnum(**expr) || **expr=='_' || **expr=='$' || **expr=='.'))
            label_name[i++] = *(*expr)++;
        label_name[i] = '\0';
        l = find_label(as, label_name);
        if (!l || !l->is_resolved) { *error = true; return 0; }
        return l->value;
    }
    val = strtol(*expr, &end, 0);
    if (end == *expr) { report_error(as, "Invalid term in expression", *expr); *error = true; return 0; }
    *expr = end;
    return (int)val;
}

static int evaluate_expression(AssemblerState *as, const char *expr, bool *error) {
    const char *p;
    int v;
    if (!expr || !*expr) { *error = false; return 0; }
    p = expr;
    v = parse_sum(as, &p, error);
    while (isspace(*p)) p++;
    if (*p != '\0') { report_error(as, "Trailing characters in expression", p); *error = true; }
    return v;
}

/* Prefix encoding - now uses unified tp_encode_instr from transputer-enc.h */
static int encode_instruction(AsmItem *item, int value, uint8_t *buffer) {
    int final_operand;
    final_operand = (item->opcode == 0xF) ? item->operation : value;
    if (item->opcode == 0x0 || item->opcode == 0x9 || item->opcode == 0xA)
        final_operand = value - (item->address + item->size);
    return tp_encode_instr(buffer, item->opcode, final_operand);
}

/* Passes */
static bool resolve_equs_pass(AssemblerState *as) {
    bool changed = false;
    int i;
    Label *l;
    for (i = 0; i < HASH_TABLE_SIZE; i++) {
        for (l = as->label_table[i]; l; l = l->next) {
            if (!l->is_resolved) {
                bool error = false;
                int v;
                as->current_line = l->defined_line;
                as->current_filename = l->defined_filename;
                v = evaluate_expression(as, l->equ_expr, &error);
                if (!error) {
                    l->value = v;
                    l->is_resolved = true;
                    free(l->equ_expr); l->equ_expr = NULL;
                    changed = true;
                }
            }
        }
    }
    return changed;
}

static bool stabilize_addresses_pass(AssemblerState *as) {
    bool changed = false;
    int addr = 0;
    uint8_t tmp[16];
    AsmItem *it;
    for (it = as->first_item; it; it = it->next) {
        int old_size;
        int new_size;
        if (it->address != addr) { it->address = addr; changed = true; }
        old_size = it->size;
        new_size = old_size;
        if (it->type == ITEM_INSTR) {
            bool err = false;
            int val;
            as->current_line = it->line_number;
            as->current_filename = it->filename;
            val = evaluate_expression(as, it->operand_expr, &err);
            new_size = err ? 8 : encode_instruction(it, val, tmp);
        }
        /* Never shrink — prevents infinite oscillation from interlocking jumps */
        if (new_size < old_size)
            new_size = old_size;
        if (it->size != new_size) { it->size = new_size; changed = true; }
        addr += it->size;
    }
    if (changed) {
        int i;
        Label *l;
        for (i = 0; i < HASH_TABLE_SIZE; i++)
            for (l = as->label_table[i]; l; l = l->next)
                if (l->owner_item) l->value = l->owner_item->address;
    }
    return changed;
}

/* Parsing helpers */
static char *parse_token(char **line) {
    char *tok;
    while (isspace(**line)) (*line)++;
    if (**line == '\0') return NULL;
    tok = *line;
    while (**line && !isspace(**line)) (*line)++;
    if (**line) *(*line)++ = '\0';
    return tok;
}

static void parse_db(AssemblerState *as, char *expr) {
    while (expr && *expr) {
        char *start;
        AsmItem *it;
        char saved;
        while (isspace(*expr)) expr++;
        if (*expr == '\0') break;
        if (*expr == '"') {
            expr++;
            start = expr;
            while (*expr && *expr != '"') expr++;
            *expr = '\0';
            it = add_item(as, strlen(start));
            it->type = ITEM_DATA_BYTE;
            it->data_bytes = (uint8_t *)strdup(start);
            it->data_len = it->size;
        } else {
            start = expr;
            while (*expr && *expr != ',') expr++;
            saved = *expr;
            if (saved == ',') *expr = '\0';
            add_data_item(as, 1, start);
            if (saved == ',') expr++; else break;
        }
        if (*expr == '"') expr++;
    }
}

static void parse_line(AssemblerState *as) {
    char *p = as->line_ptr;
    char *colon = strchr(p, ':');
    char *mnemonic;
    static int asm_trace = -1;
    static const char *asm_sym_filter;
    if (colon) {
        Label *l;
        *colon = '\0';
        while (isspace(*p)) p++;
        if (*p) {
            l = define_label(as, p);
            if (l) l->owner_item = as->last_item ? as->last_item->next : as->first_item;
        }
        p = colon + 1;
    }
    while (isspace(*p)) p++;
    if (*p == '\0') return;
    mnemonic = parse_token(&p);
    if (!mnemonic) return;
    if (asm_trace < 0) {
        const char *env = getenv("T4_ASM_SYM");
        asm_trace = env && *env;
        asm_sym_filter = env;
    }
    if (tp_asm_stricmp(mnemonic, "equ") == 0) {
        char label_name[MAX_LINE_LENGTH];
        sscanf(as->line_buffer, " %s", label_name);
        label_name[strlen(label_name)-1] = 0;
        define_equ(as, label_name, p);
    } else if (tp_asm_stricmp(mnemonic, "db") == 0 || tp_asm_stricmp(mnemonic, ".db") == 0 ||
               tp_asm_stricmp(mnemonic, ".byte") == 0) {
        parse_db(as, p);
    } else if (tp_asm_stricmp(mnemonic, "dw") == 0) {
        add_data_item(as, 4, p);
    } else if (tp_asm_stricmp(mnemonic, "ds") == 0) {
        bool error = false;
        int count;
        AsmItem *it;
        count = evaluate_expression(as, p, &error);
        if (error) { report_error(as, "DS requires value", NULL); return; }
        it = add_item(as, count); it->type = ITEM_PAD;
    } else if (tp_asm_stricmp(mnemonic, "align") == 0) {
        bool err = false;
        int n;
        int pad;
        AsmItem *it;
        n = evaluate_expression(as, p, &err);
        if (err || n <= 0) { report_error(as, "ALIGN requires value", NULL); return; }
        pad = (-as->current_address) & (n - 1);
        if (pad) { it = add_item(as, pad); it->type = ITEM_PAD; }
    } else if (tp_asm_stricmp(mnemonic, "include") == 0) {
        char fname[MAX_LINE_LENGTH];
        char path[1024];
        char *slash;
        char *q;
        FILE *inc;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '"') {
            p++;
            q = fname;
            while (*p && *p != '"') *q++ = *p++;
            *q = '\0';
        } else {
            sscanf(p, "%s", fname);
        }
        pstrcpy(path, sizeof(path), as->current_filename);
        slash = strrchr(path, '/');
        if (slash) slash[1] = '\0';
        else path[0] = '\0';
        strncat(path, fname, sizeof(path) - strlen(path) - 1);
        inc = fopen(path, "r");
        if (!inc) { report_error(as, "Cannot open include", fname); return; }
        parse_file(as, inc, path);
        fclose(inc);
    } else if (tp_asm_stricmp(mnemonic, "org") == 0) {
        bool error = false;
        int new_addr = evaluate_expression(as, p, &error);
        if (error) { report_error(as, "ORG requires value", NULL); return; }
        if (new_addr < as->current_address) {
            report_error(as, "ORG cannot go backwards", NULL);
        } else {
            AsmItem *it = add_item(as, new_addr - as->current_address);
            it->type = ITEM_PAD;
        }
    } else if (tp_asm_stricmp(mnemonic, "end") == 0 || mnemonic[0] == '.') {
        /* ignore */
    } else {
        const InstructionInfo *info = find_instr(mnemonic);
        if (info) {
            if (asm_trace && asm_sym_filter && p && strstr(p, asm_sym_filter)) {
                fprintf(stderr, "ASM_SYM: file=%s line=%d mnemonic=%s expr=%s\n",
                        as->current_filename ? as->current_filename : "<unknown>",
                        as->current_line, mnemonic, p);
            }
            if (info->code >= 0x30000) {
                /* selth/tret/goprot/syscall — ISM nfix0;opr */
                add_t9000_protect_opr(as, -(info->code & 0xFF));
            } else if (info->code >= 0x20000) {
                add_fpentry_sequence(as, info->code);
            } else {
                add_instruction_item(as, info->code, p);
            }
            return;
        }
        report_error(as, "Unknown mnemonic", mnemonic);
    }
}

static void parse_file(AssemblerState *as, FILE *f, const char *name) {
    int saved;
    char *comment;
    char *end;
    as->current_filename = name;
    saved = as->current_line;
    as->current_line = 0;
    while (fgets(as->line_buffer, sizeof(as->line_buffer), f)) {
        as->current_line++;
        comment = strchr(as->line_buffer, ';');
        if (!comment) comment = strchr(as->line_buffer, '#');
        if (comment) *comment = '\0';
        end = as->line_buffer + strlen(as->line_buffer) - 1;
        while (end >= as->line_buffer && isspace((unsigned char)*end)) *end-- = '\0';
        as->line_ptr = as->line_buffer;
        parse_line(as);
    }
    as->current_line = saved + as->current_line;
}

static void write_output_pass(AssemblerState *as, const char *filename) {
    FILE *f;
    uint8_t buf[16];
    AsmItem *it;
    f = fopen(filename, "wb");
    if (!f) { report_error(as, "Failed to open output file", filename); return; }
    for (it = as->first_item; it; it = it->next) {
        as->current_address_label->value = it->address;
        as->current_line = it->line_number;
        as->current_filename = it->filename;
        switch (it->type) {
            case ITEM_PAD:
                /* 0x20 = pfix 0 (true no-op); avoid 0x00 = j 0 which can trigger timeslice */
                { int i; for (i=0;i<it->size;i++) fputc(0x20,f); }
                break;
            case ITEM_DATA_BYTE:
                if (it->data_bytes)
                    fwrite(it->data_bytes,1,it->data_len,f);
                else {
                    bool err=false; int v; v=evaluate_expression(as,it->operand_expr,&err);
                    if (err) { report_error(as,"Unresolved DB",it->operand_expr); v=0; }
                    fputc(v & 0xFF,f);
                }
                break;
            case ITEM_DATA_WORD: {
                bool err=false; uint32_t v; v=evaluate_expression(as,it->operand_expr,&err);
                if (err) { report_error(as,"Unresolved DW",it->operand_expr); v=0; }
                fwrite(&v,4,1,f);
                break; }
            case ITEM_INSTR: {
                bool err=false; int value; int n;
                value=evaluate_expression(as,it->operand_expr,&err);
                if(err) report_error(as,"Unresolved operand",it->operand_expr);
                n=encode_instruction(it,value,buf);
                fwrite(buf,n,1,f);
                break; }
        }
    }
    fclose(f);
}

/* Error reporting */
static void report_error(AssemblerState *as, const char *msg, const char *data) {
    fprintf(stderr, "%s:%d: error: %s", as->current_filename, as->current_line, msg);
    if (data) fprintf(stderr, ": '%s'", data);
    fprintf(stderr, "\n");
    as->error_count++;
}

/* Entry point used by tools */
int transputer_asm_main(int argc, char **argv) {
    AssemblerState *as;
    FILE *f;
    int i;
    if (argc < 3) {
        fprintf(stderr, "Usage: tasm input.asm output.bin [library.asm]\n");
        return 1;
    }
    as = create_assembler();
    if (!as) { fprintf(stderr, "Allocation failure\n"); return 1; }
    f = fopen(argv[1], "r");
    if (!f) { as->current_filename = argv[1]; report_error(as, "Cannot open input", argv[1]); free_assembler(as); return 1; }
    parse_file(as, f, argv[1]);
    fclose(f);
    if (argc == 4) {
        f = fopen(argv[3], "r");
        if (!f) { as->current_filename = argv[3]; report_error(as,"Cannot open library",argv[3]); }
        else { parse_file(as,f,argv[3]); fclose(f); }
    }
    if (as->error_count) { free_assembler(as); return 1; }
    for (i=0;i<MAX_PASSES;i++) {
        if (!resolve_equs_pass(as)) break;
        if (i==MAX_PASSES-1) report_error(as,"Could not resolve EQU",NULL);
    }
    for (i=0;i<MAX_PASSES;i++) {
        if (!stabilize_addresses_pass(as)) break;
    }
    write_output_pass(as, argv[2]);
    if (as->error_count) {
        fprintf(stderr, "Assembly failed with %d error(s).\n", as->error_count);
        remove(argv[2]);
    }
    free_assembler(as);
    return as->error_count ? 1 : 0;
}

#ifdef TCC_TARGET_TRANSPUTER

/* -------------------------------------------------------------- */
/* Inline assembler support for TinyCC                            */
/* -------------------------------------------------------------- */

/* Encode an inline asm instruction, handling IP-relative adjustment for
 * J/CALL/CJ. Uses the unified tp_encode_instr from transputer-enc.h. */
static int encode_tp_instr_asm(int opcode, int operation, int val, int size,
                               uint8_t *buf)
{
    int operand;
    operand = (opcode == 0xF) ? operation : val;
    if (opcode == 0x0 || opcode == 0x9 || opcode == 0xA)
        operand = val - (ind + size);
    return tp_encode_instr(buf, opcode, operand);
}

ST_FUNC void asm_opcode(TCCState *s1, int t)
{
    const char *name;
    const InstructionInfo *info;
    int opcode;
    int op;
    int val = 0;
    ExprValue expr;
    int has_expr = 0;
    uint8_t buf[16];
    int size = 1, n;
    int i;

    /* This encoder writes workspace slot numbers itself, so the dynamic
     * frame-reserve computation in transputer-gen.c cannot see them.  Flag the
     * function so it falls back to the static reserved region. */
    func_uses_inline_asm = 1;
    /* Hand-written code is taken to be for the target it is assembled for
     * (the output's e_flags). */
    s1->tp_isa_used |= s1->tp_isa;

    while (t == TOK_LINEFEED) {
        next();
        t = tok;
    }
    if (t == TOK_EOF)
        return;
    name = get_tok_str(t, NULL);
    info = find_instr(name);
    if (!info)
        tcc_error("unknown opcode '%s'", name);

    /* T9000 negative secondaries (selth, tret, the VCP instructions): a
     * fixed nfix k; opr x pair with OReg = -(code & 0xFF). */
    if (info->code >= 0x30000) {
        int neg = -(info->code & 0xFF);
        if (tok != TOK_LINEFEED && tok != ';')
            tcc_error("'%s' takes no operand", name);
        g(0x60 | (~((unsigned)neg >> 4) & 0xF));
        g(0xF0 | (neg & 0xF));
        return;
    }

    /* FPENTRY mnemonics need an ldc; fpentry pair. */
    if (info->code >= 0x20000)
        tcc_error("FPENTRY mnemonic '%s' is unsupported; use a compiler builtin", name);

    opcode = info->code >= 0 ? info->code : 0xF;
    op = info->code >= 0 ? 0 : tp_asm_operation(info->code);

    if (tok != TOK_LINEFEED && tok != ';') {
        asm_expr(s1, &expr);
        has_expr = 1;
        val = expr.v;
    }
    if (info->code < 0 && has_expr)
        tcc_error("'%s' takes no operand", name);
    if (info->code == 0xF) {
        /* opr N: the operand is the operation itself */
        if (!has_expr || expr.sym)
            tcc_error("'opr' needs a constant operand");
        op = val;
    }

    if (has_expr && expr.sym) {
        int relative = opcode == TP_ENC_J || opcode == TP_ENC_CALL || opcode == TP_ENC_CJ;
        int reloc_type = relative ? R_TRANSPUTER_IPREL32 : R_TRANSPUTER_LDC_ABS;

        if (opcode == TP_ENC_PFIX || opcode == TP_ENC_NFIX || expr.pcrel)
            tcc_error("unsupported symbolic expression for '%s'", name);
        if (expr.v != (uint32_t)expr.v && expr.v != (uint64_t)(int64_t)(int32_t)expr.v)
            tcc_error("instruction addend does not fit in 32 bits");
        if (expr.v)
            reloc_type = R_TRANSPUTER_INSN;
        greloc(cur_text_section, expr.sym, ind, reloc_type);
        tp_encode_asm_slot(buf, opcode, (int32_t)expr.v);
        for (i = 0; i < TP_ASM_SLOT; i++)
            g(buf[i]);
        return;
    }

    /* A branch displacement depends on the instruction's own length.  Only
       ever grow it: shrinking can oscillate (`j 17` at offset 0: one byte
       needs two, two bytes fit in one).  Left-pad a shorter encoding with
       pfix 0, which adds nothing to Oreg at an instruction boundary. */
    for (;;) {
        n = encode_tp_instr_asm(opcode, op, val, size, buf);
        if (n <= size)
            break;
        size = n;
    }

    for (i = n; i < size; i++)
        g(0x20);
    for (i = 0; i < n; i++)
        g(buf[i]);
}

#endif /* TCC_TARGET_TRANSPUTER */
#endif /* !TARGET_DEFS_ONLY */
