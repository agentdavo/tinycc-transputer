/*
 * Minimal Transputer assembler used by TinyCC.
 * Derived from a standalone "tasm" example.
 */

#ifdef TARGET_DEFS_ONLY

/* When included from tcc.h - only forward declarations */
#define CONFIG_TCC_ASM
#define NB_ASM_REGS 14

#ifdef TP_FPU_RUNTIME
ST_FUNC void tp_fp_round_plus(void);
ST_FUNC void tp_fp_round_minus(void);
ST_FUNC void tp_fp_round_zero(void);
ST_FUNC void tp_fp_round_nearest(void);
ST_FUNC void tp_fp_seterr(void);
ST_FUNC void tp_fp_clrerr(void);
ST_FUNC int tp_fp_geterr(void);
ST_FUNC int tp_fp_getround(void);
ST_FUNC float tp_addf(float a, float b);
#endif
ST_FUNC void asm_opcode(TCCState *s1, int t);

#else /* !TARGET_DEFS_ONLY */

/* When included from libtcc.c - full implementation */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef TCC_TARGET_TRANSPUTER
# define USING_GLOBALS
# include "tcc.h"
# include "transputer-enc.h"  /* Unified prefix encoding */
# include "transputer-opcodes.h"
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
    {"j", 0x0},  {"ldlp", 0x1}, {"pfix", 0x2}, {"ldnl", 0x3},
    {"ldc", 0x4}, {"ldnlp", 0x5}, {"nfix", 0x6}, {"ldl", 0x7},
    {"adc", 0x8}, {"call", 0x9}, {"cj", 0xA},   {"ajw", 0xB},
    {"eqc", 0xC}, {"stl", 0xD},  {"stnl", 0xE}, {"opr", 0xF},
    {"rev", TP_ASM_REV}, {"lb", -0x01}, {"bsub", -0x02}, {"endp", -0x03},
    {"diff", -0x04}, {"add", -0x05}, {"gcall", -0x06}, {"in", -0x07},
    {"prod", -0x08}, {"gt", -0x09}, {"wsub", -0x0A}, {"out", -0x0B},
    {"sub", -0x0C}, {"startp", -0x0D}, {"outbyte", -0x0E}, {"outword", -0x0F},
    {"seterr", -0x10}, {"csub0", -0x13}, {"stopp", -0x15}, {"ladd", -0x16},
    {"stlb", -0x17}, {"sthf", -0x18}, {"norm", -0x19}, {"ldiv", -0x1A},
    {"ldpi", -0x1B}, {"stlf", -0x1C}, {"xdble", -0x1D}, {"ldpri", -0x1E},
    {"rem", -0x1F}, {"ret", -0x20}, {"lend", -0x21}, {"ldtimer", -0x22},
    {"tin", -0x2B}, {"div", -0x2C}, {"lmul", -0x31}, {"not", -0x32},
    {"xor", -0x33}, {"bcnt", -0x34}, {"lshr", -0x35}, {"lshl", -0x36},
    {"lsum", -0x37}, {"lsub", -0x38}, {"runp", -0x39}, {"xword", -0x3A},
    {"sb", -0x3B}, {"gajw", -0x3C}, {"savel", -0x3D}, {"saveh", -0x3E},
    {"wcnt", -0x3F}, {"shr", -0x40}, {"shl", -0x41}, {"mint", -0x42},
    {"alt", -0x43}, {"altwt", -0x44}, {"altend", -0x45}, {"and", -0x46},
    {"enbt", -0x47}, {"enbc", -0x48}, {"enbs", -0x49}, {"move", -0x4A},
    {"or", -0x4B}, {"csngl", -0x4C}, {"ccnt1", -0x4D}, {"talt", -0x4E},
    {"ldiff", -0x4F}, {"sthb", -0x50}, {"taltwt", -0x51}, {"sum", -0x52},
    {"mul", -0x53}, {"sttimer", -0x54}, {"stoperr", -0x55}, {"cword", -0x56},
    {"clrhalterr", -0x57}, {"sethalterr", -0x58}, {"testhalterr", -0x59},
    {"dup", -0x5A}, {"move2dinit", -0x5B}, {"move2dall", -0x5C},
    {"move2dnonzero", -0x5D}, {"move2dzero", -0x5E}, {"gtu", -0x5F},
    {"fmul", -0x72},
    {"crcword", -0x74}, {"crcbyte", -0x75}, {"bitcnt", -0x76},
    {"bitrevword", -0x77}, {"bitrevnbits", -0x78}, {"wsubdb", -0x81},
    {"terminate", -0x03},
    {"fpldnlsn", -0x8E}, {"fpldnldb", -0x8A}, {"fpstnlsn", -0x88},
    {"fpstnldb", -0x84}, {"fpadd", -0x87}, {"fpsub", -0x89},
    {"fpmul", -0x8B}, {"fpdiv", -0x8C}, {"fpremfirst", -0x8F},
    {"fpremstep", -0x90}, {"fpint", -0xA1}, {"fprev", -0xA4},
    {"fpentry", -0xAB}, {"fptesterr", -0x9C},
    {"diss", -0x30},
    {"disc", -0x2F},
    {"dist", -0x2E},
    {"testerr", -0x29},
    {"resetch", -0x12},
    {"testpranal", -0x2A},
    {"unpacksn", -0x63},
    {"roundsn", -0x6D},
    {"postnormsn", -0x6C},
    {"ldinf", -0x71},
    {"cflerr", -0x73},
    {"fpdup", -0xA3},
    {"fpldnlsni", -0x86},
    {"fpldnldbi", -0x82},
    {"fpldzerosn", -0x9F},
    {"fpldzerodb", -0xA0},
    {"fpldnladdsn", -0xAA},
    {"fpldnladddb", -0xA6},
    {"fpldnlmulsn", -0xAC},
    {"fpldnlmuldb", -0xA8},
    {"fpsttest", -0x80},
    {"fpchkerr", -0x83},
    {"fpldtest", -0x85},
    {"fpgt", -0x94},
    {"fprange", -0x8D},
    {"fpge", -0x97},
    {"fpeq", -0x95},
    {"fpordered", -0x92},
    {"fpnan", -0x91},
    {"fpnotfinite", -0x93},
    {"fpstnli32", -0x9E},
    {"fprtoi32", -0x9D},
    {"fpi32tor32", -0x96},
    {"fpi32tor64", -0x98},
    {"fpb32tor64", -0x9A},
    {"fplg", -0x9B},
    /* TVX - Transputer Vector eXtension (MXFP4 / bfloat16) */
    {"bf16tor32", -0x1C0},
    {"r32tobf16", -0x1C1},
    {"bf16pack", -0x1C2},
    {"bf16unpkh", -0x1C3},
    {"mxfp4tor32", -0x1C4},
    {"r32tomxfp4", -0x1C5},
    {"mxe8m0tor32", -0x1C6},
    {"mxe8m0of", -0x1C7},
    {"mxfp4dot", -0x1C8},
    {"bf16dot", -0x1C9},
    {"mxfp4gemv", -0x1CA},
    {"mxfp4unpk8", -0x1CB},
    {"r32tomxfp4x8", -0x1CC},
    {"bf16mul2", -0x1CD},
    {"bf16add2", -0x1CE},
    {"vsetlen", -0x1CF},
    {"mxfp4gemvbf", -0x1D0},
    {"vaddsn", -0x1D1},
    {"vmulsn", -0x1D2},
    {"vsilusn", -0x1D3},
    {"vrmsnorm", -0x1D4},
    {"vsoftmaxsn", -0x1D5},
    {"f16tor32", -0x1D6},
    {"r32tof16", -0x1D7},
    {"q8dot", -0x1D8},
    {"q8dotbf", -0x1D9},
    {"iq1mdot", -0x1DA},
    {"iq2xxsdot", -0x1DB},
    {"iq3xxsdot", -0x1DC},
    {"qsetfmt", -0x1DD},
    {"qdot", -0x1DE},
    {"vhad32", -0x1DF},
    {"mxqrot", -0x1E0},
    {"mxqrotbf", -0x1E1},
    {"ftsetvp", -0x1E2},
    {"ftransp", -0x1E3},
    {"ftranspb", -0x1E4},
    {"fsincos", -0x1E5},
    {"frsqrt", -0x1E6},
    {"frecip", -0x1E7},
    {"fsincosb", -0x1E8},
    {"vsetstride", -0x1E9},
    {"ftcull", -0x1EA},
    {"tskin", -0x1EB},
    {"vsetfmt", -0x1EC},
    {"ftsetlit", -0x1ED},
    {"flitb", -0x1EE},
    {"qgemm", -0x1EF},
    {"vblend8", -0x1F0},
    {"vfill", -0x1F1},
    {"vfill2d", -0x1F2},
    {"csum", -0x1F3},
    {"vblend2d", -0x1F4},
    {"stvxstate", -0x1F5},
    {"csumcpy", -0x1F6},
    {"vq8k", -0x1F7},
    {"vq80", -0x1F8},
    {"vcvt", -0x1F9},
    {"vxfer", -0x1FA},
    {"fpusqrtfirst", 0x20001},
    {"fpusqrtstep", 0x20002},
    {"fpusqrtlast", 0x20003},
    {"fpurp", 0x20004},
    {"fpurm", 0x20005},
    {"fpurz", 0x20006},
    {"fpur32tor64", 0x20007},
    {"fpur64tor32", 0x20008},
    {"fpuexpdec32", 0x20009},
    {"fpuexpinc32", 0x2000A},
    {"fpuabs", 0x2000B},
    {"fpunoround", 0x2000D},
    {"fpuchki32", 0x2000E},
    {"fpuchki64", 0x2000F},
    {"fpudivby2", 0x20011},
    {"fpumulby2", 0x20012},
    {"fpurn", 0x20022},
    {"fpuseterr", 0x20023},
    {"fpuclrerr", 0x2009C},
    {"start", -TP_OPR_START},
    {"testhardchan", -0x2D},
    {"testldd", -0x25},
    {"teststd", -0x28},
    {"testlde", -0x24},
    {"testste", -0x27},
    {"testlds", -0x23},
    {"teststs", -0x26},
    {"settimeslice", -0xB0},
    {"break", -0xB1},
    {"clrj0break", -0xB2},
    {"setj0break", -0xB3},
    {"testj0break", -0xB4},
    {"ldflags", -0xB6},
    {"stflags", -0xB7},
    {"xbword", -0xB8},
    {"lbx", -0xB9},
    {"cb", -0xBA},
    {"cbu", -0xBB},
    {"insphdr", -0xBC},
    {"readbfr", -0xBD},
    {"ldconf", -0xBE},
    {"stconf", -0xBF},
    {"ldcnt", -0xC0},
    {"ssub", -0xC1},
    {"ldth", -0xC2},
    {"ldchstatus", -0xC3},
    {"intdis", -0xC4},
    {"intenb", -0xC5},
    {"cir", -0xC7},
    {"ss", -0xC8},
    {"chantype", -0xC9},
    {"ls", -0xCA},
    {"fpseterr", -0xCB},
    {"ciru", -0xCC},
    {"fprem", -0xCF},
    {"fprn", -0xD0},
    {"fpdivby2", -0xD1},
    {"fpmulby2", -0xD2},
    {"fpsqrt", -0xD3},
    {"fprp", -0xD4},
    {"fprm", -0xD5},
    {"fprz", -0xD6},
    {"fpr32tor64", -0xD7},
    {"fpr64tor32", -0xD8},
    {"fpexpdec32", -0xD9},
    {"fpexpinc32", -0xDA},
    {"fpabs", -0xDB},
    {"fpclrerr", -0xDC},
    {"fpadddbsn", -0xDD},
    {"fpchki32", -0xDE},
    {"fpchki64", -0xDF},
    {"devlb", -0xF0},
    {"devsb", -0xF1},
    {"devls", -0xF2},
    {"devss", -0xF3},
    {"devlw", -0xF4},
    {"devsw", -0xF5},
    {"xsword", -0xF8},
    {"lsx", -0xF9},
    {"cs", -0xFA},
    {"csu", -0xFB},
    {"timerdisableh", -0x7A},
    {"timerdisablel", -0x7B},
    {"timerenableh", -0x7C},
    {"timerenablel", -0x7D},
    {"ldmemstartval", -0x7E},
    {"pop", -0x79},
    {"lddevid", -TP_OPR_LDDEVID},
    /* T9000 protection / trap (ISM App.B nfix encodings → final OReg):
     *   selth −7, tret −5, goprot −6, syscall −8  →  60 F9 / FB / FA / F8
     * Raw nfix0;opr path via add_t9000_protect_opr (not tp_encode_instr). */
    {"selth",   0x30007},
    {"tret",    0x30005},
    {"goprot",  0x30006},
    {"syscall", 0x30008},
    /* T9000 semaphores (ISM 8.6): wait 60 F5, signal 60 F4 */
    {"wait",    0x3000B},
    {"signal",  0x3000C},
    /* T9000 VCP and resource channels (ISM App.B), same nfix encoding:
     * OReg = -(code & 0xFF), nfix 1 for -0x11..-0x20, nfix 2 below. */
    {"stopch",    0x30012},
    {"vout",      0x30013},
    {"vin",       0x30014},
    {"swapbfr",   0x30017},
    {"sethdr",    0x30018},
    {"setchmode", 0x30019},
    {"initvlcb",  0x3001A},
    {"writehdr",  0x3001B},
    {"readhdr",   0x3001C},
    {"disg",      0x3001D},
    {"enbg",      0x3001E},
    {"grant",     0x3001F},
    {"unmkrc",    0x30023},
    {"mkrc",      0x30024},
    {"irdsq",     0x30025},
    {"erdsq",     0x30026},
    {"stresptr",  0x30027},
    {"ldresptr",  0x30028},
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

static int stricmp(const char *a, const char *b) {
    while (*a && tolower(*a) == tolower(*b)) { a++; b++; }
    return tolower(*a) - tolower(*b);
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
    if (stricmp(name, "$") == 0) {
        as->current_address_label->value = as->current_address;
        return as->current_address_label;
    }
    idx = hash_string(name);
    for (l = as->label_table[idx]; l; l = l->next)
        if (stricmp(l->name, name) == 0) return l;
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
    if (stricmp(mnemonic, "equ") == 0) {
        char label_name[MAX_LINE_LENGTH];
        sscanf(as->line_buffer, " %s", label_name);
        label_name[strlen(label_name)-1] = 0;
        define_equ(as, label_name, p);
    } else if (stricmp(mnemonic, "db") == 0 || stricmp(mnemonic, ".db") == 0 ||
               stricmp(mnemonic, ".byte") == 0) {
        parse_db(as, p);
    } else if (stricmp(mnemonic, "dw") == 0) {
        add_data_item(as, 4, p);
    } else if (stricmp(mnemonic, "ds") == 0) {
        bool error = false;
        int count;
        AsmItem *it;
        count = evaluate_expression(as, p, &error);
        if (error) { report_error(as, "DS requires value", NULL); return; }
        it = add_item(as, count); it->type = ITEM_PAD;
    } else if (stricmp(mnemonic, "align") == 0) {
        bool err = false;
        int n;
        int pad;
        AsmItem *it;
        n = evaluate_expression(as, p, &err);
        if (err || n <= 0) { report_error(as, "ALIGN requires value", NULL); return; }
        pad = (-as->current_address) & (n - 1);
        if (pad) { it = add_item(as, pad); it->type = ITEM_PAD; }
    } else if (stricmp(mnemonic, "include") == 0) {
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
    } else if (stricmp(mnemonic, "org") == 0) {
        bool error = false;
        int new_addr = evaluate_expression(as, p, &error);
        if (error) { report_error(as, "ORG requires value", NULL); return; }
        if (new_addr < as->current_address) {
            report_error(as, "ORG cannot go backwards", NULL);
        } else {
            AsmItem *it = add_item(as, new_addr - as->current_address);
            it->type = ITEM_PAD;
        }
    } else if (stricmp(mnemonic, "end") == 0 || mnemonic[0] == '.') {
        /* ignore */
    } else {
        int i;
        for (i = 0; instructions[i].mnemonic; i++) {
            if (stricmp(mnemonic, instructions[i].mnemonic) == 0) {
                if (asm_trace && asm_sym_filter && p && strstr(p, asm_sym_filter)) {
                    fprintf(stderr, "ASM_SYM: file=%s line=%d mnemonic=%s expr=%s\n",
                            as->current_filename ? as->current_filename : "<unknown>",
                            as->current_line, mnemonic, p);
                }
                if (instructions[i].code >= 0x30000) {
                    /* selth/tret/goprot/syscall — ISM nfix0;opr */
                    add_t9000_protect_opr(as, -(instructions[i].code & 0xFF));
                } else if (instructions[i].code >= 0x20000) {
                    add_fpentry_sequence(as, instructions[i].code);
                } else {
                    add_instruction_item(as, instructions[i].code, p);
                }
                return;
            }
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

static const InstructionInfo *find_instr(const char *name)
{
    const InstructionInfo *p;
    for (p = instructions; p->mnemonic; p++)
        if (strcasecmp(p->mnemonic, name) == 0)
            return p;
    return NULL;
}

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
    s1->tp_isa_used |= tp_target_flags(s1);

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
        int reloc_type;
        int fixed_size = 0;
        /* The linker replaces the entire instruction operand with the
         * relocated value. It cannot recover a symbolic addend here. */
        if (expr.v)
            tcc_error("symbolic addend is unsupported for '%s'", name);
        if (opcode == 0x0 || opcode == 0x9 || opcode == 0xA) {
            reloc_type = R_TRANSPUTER_IPREL32;
            fixed_size = 9;
        } else if (opcode == 0x4) {
            reloc_type = R_TRANSPUTER_LDC_ABS;
            fixed_size = 9;
        } else if (opcode == 0x1 || opcode == 0x3 || opcode == 0x5 ||
                   opcode == 0x7 || opcode == 0x8 || opcode == 0xB ||
                   opcode == 0xD || opcode == 0xE) {
            /* ldlp, ldnl, ldnlp, ldl, adc, ajw, stl, stnl —
             * workspace/memory opcodes need full prefix reservation */
            reloc_type = R_TRANSPUTER_LDC_ABS;
            fixed_size = 9;
        } else {
            reloc_type = R_TRANSPUTER_32;
        }
        greloc(cur_text_section, expr.sym, ind, reloc_type);
        if (fixed_size) {
            int j;
            for (j = 0; j < fixed_size - 1; j++)
                g(0x20);
            g((uint8_t)((opcode << 4) | (op & 0xF)));
            return;
        }
        val = 0;
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


/* Include FPU helper functions */
#ifdef TARGET_DEFS_ONLY

#ifdef TP_FPU_RUNTIME
ST_FUNC void tp_fp_round_plus(void);
ST_FUNC void tp_fp_round_minus(void);
ST_FUNC void tp_fp_round_zero(void);
ST_FUNC void tp_fp_round_nearest(void);
ST_FUNC void tp_fp_seterr(void);
ST_FUNC void tp_fp_clrerr(void);
ST_FUNC int tp_fp_geterr(void);
ST_FUNC int tp_fp_getround(void);
ST_FUNC float tp_addf(float a, float b);
#endif

#else /* !TARGET_DEFS_ONLY */

#ifndef TP_EMBED_FPU
# define USING_GLOBALS
# include "tcc.h"

/******************************************************/
/* Basic byte emit helpers                            */
/******************************************************/
ST_FUNC void g(int c)
{
    int ind1;
    static int last_byte = -1;
    static int nfix_opr_log_count = 0;
    static int nfix_opr_warn;
    static int prefix_count = 0;
    static int call_log_count = 0;
    static int call_log_limit;
    static int call_log_short;
    static const char *call_log_filter;
    static int env_read;
    if (nocode_wanted)
        return;
    if (!cur_text_section)
        return;
    /* Read the trace knobs once: this runs for every byte emitted. */
    if (!env_read) {
        const char *env;
        env = getenv("T4_NFIX_OPR_WARN");
        nfix_opr_warn = env && *env ? atoi(env) : 0;
        env = getenv("T4_CALL_EMIT_TRACE");
        call_log_limit = env && *env ? atoi(env) : 0;
        env = getenv("T4_CALL_EMIT_SHORT");
        call_log_short = env && *env ? atoi(env) : 0;
        call_log_filter = getenv("T4_CALL_EMIT_FILTER");
        env_read = 1;
    }
    ind1 = ind + 1;
    if (ind1 > cur_text_section->data_allocated)
        section_realloc(cur_text_section, ind1);
    cur_text_section->data[ind] = c;
    if (nfix_opr_warn && nfix_opr_log_count < 64
        && last_byte >= 0
        && ((last_byte & 0xF0) == 0x60)
        && ((c & 0xF0) == 0xF0)) {
        const char *fname = file ? file->filename : "<unknown>";
        const char *func = funcname ? funcname : "<no func>";
        fprintf(stderr, "WARN: g(): NFIX->OPR sequence 0x%02X 0x%02X at offset=0x%x file=%s func=%s\n",
                last_byte, c, ind, fname, func);
        nfix_opr_log_count++;
    }
    /* Note: 0xF3 is a valid Transputer instruction byte (OPR 3).
     * When preceded by PFIX 3 (0x23), it encodes OPR 0x33 = XOR.
     * This is used legitimately in unsigned comparison code.
     * Do NOT intercept or replace this byte! */
    ind = ind1;
    /* Keep data_offset in sync with ind */
    if (ind > cur_text_section->data_offset)
        cur_text_section->data_offset = ind;
    last_byte = c;

    if ((c & 0xF0) == 0x20 || (c & 0xF0) == 0x60) {
        prefix_count++;
    } else {
        if (call_log_limit > 0 && call_log_count < call_log_limit) {
            int opc = c & 0xF0;
            if (opc == 0x90 || opc == 0x00 || opc == 0xA0) {
                const char *fname = file ? file->filename : "<unknown>";
                const char *func = funcname ? funcname : "<no func>";
                fprintf(stderr,
                        "CALL_EMIT: byte=0x%02X prefixes=%d offset=0x%x file=%s func=%s\n",
                        c, prefix_count, ind - 1, fname, func);
                call_log_count++;
            }
        } else if (call_log_short > 0 && call_log_count < call_log_short) {
#define TP_IPREL_NIBBLES 9

            int opc = c & 0xF0;
            if ((opc == 0x90 || opc == 0x00 || opc == 0xA0) &&
                prefix_count < (TP_IPREL_NIBBLES - 1)) {
                const char *fname = file ? file->filename : "<unknown>";
                const char *func = funcname ? funcname : "<no func>";
                if (call_log_filter && *call_log_filter) {
                    if (!strstr(fname, call_log_filter) &&
                        (!func || !strstr(func, call_log_filter))) {
                        prefix_count = 0;
                        return;
                    }
                }
                fprintf(stderr,
                        "CALL_EMIT_SHORT: byte=0x%02X prefixes=%d offset=0x%x file=%s func=%s\n",
                        c, prefix_count, ind - 1, fname, func);
                call_log_count++;
            }
        }
        prefix_count = 0;
    }
}

ST_FUNC void gen_le16(int v)
{
    g(v);
    g(v >> 8);
}

ST_FUNC void gen_le32(int v)
{
    g(v);
    g(v >> 8);
    g(v >> 16);
    g(v >> 24);
}
#endif /* TP_EMBED_FPU */

/* Additional helpers for IEEE 754 compliance */
#ifdef USE_FP_NAN_NOTFINITE
ST_FUNC void tp_fpnan(void)           { g(0x91); }
ST_FUNC void tp_fpnotfinite(void)     { g(0x93); }
#endif
#ifdef TP_FPU_RUNTIME
/* T414 software FPU emulation using host C library
 * NOTE: Currently unused but kept for future software FPU support
 * These functions provide IEEE 754 compliance for T414 (no hardware FPU)
 * using the host system's floating-point library
 */
#include <math.h>
#include <fenv.h>
static int tp_rounding_mode = FE_TONEAREST;
static int tp_fp_exception_flag;

/* Software emulation of FPU rounding modes using C99 fenv.h */
void tp_fp_round_plus(void)   { tp_rounding_mode = FE_UPWARD;   fesetround(FE_UPWARD); }
void tp_fp_round_minus(void)  { tp_rounding_mode = FE_DOWNWARD; fesetround(FE_DOWNWARD); }
void tp_fp_round_zero(void)   { tp_rounding_mode = FE_TOWARDZERO; fesetround(FE_TOWARDZERO); }
void tp_fp_round_nearest(void){ tp_rounding_mode = FE_TONEAREST; fesetround(FE_TONEAREST); }

/* Software emulation of FPU error flag */
void tp_fp_seterr(void)       { tp_fp_exception_flag = 1; }
void tp_fp_clrerr(void)       { tp_fp_exception_flag = 0; }
int tp_fp_geterr(void)        { return tp_fp_exception_flag; }
int tp_fp_getround(void)      { return tp_rounding_mode; }
float tp_addf(float a, float b)
{
    if (isnan(a) || isnan(b)) {
        tp_fp_seterr();
        return NAN;
    }
    if (isinf(a) || isinf(b)) {
        tp_fp_seterr();
        if (isinf(a) && isinf(b) && signbit(a) != signbit(b))
            return NAN;
    }
    fesetround(tp_rounding_mode);
    return a + b;
}
#endif

#endif /* !TARGET_DEFS_ONLY */
