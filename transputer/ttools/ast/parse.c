/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */
#line 53 "parse.y"

#include "ast.h"
#include "y.tab.h"

/* The segment we are currently emitting to.  */
struct segment *curseg;

/* Nth element holds the expression for the last digital label N seen. */
static expr backlab[10];

/* Every time a forward reference is seen, we create an undefined name
   and put the pointer to that here.  When we encounter a diglab,
   we assign the current point to the corresponding name and zero
   the entry in this array.  */
static name forwlab[10];

/* Used in handling of .byte/.half/.word */
static int data_width;

/* Vector of stored .need directives to be handled at end of parsing */
struct deferred_needs {
    name name;
    name dep;
};
static struct deferred_needs *deferred_needs = NULL;
static int ndeferred_needs = 0;


static void do_pending_alignment (struct segment *seg);
static void drop_pending_alignment (struct segment *seg);
static void *check_defined (name np);
static void yyerror (char *errstr);


#line 106 "y.tab.c"

# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

/* Use api.header.include to #include this header
   instead of duplicating it here.  */
#ifndef YY_YY_Y_TAB_H_INCLUDED
# define YY_YY_Y_TAB_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    TOK_NAME = 258,                /* TOK_NAME  */
    TOK_STRING = 259,              /* TOK_STRING  */
    TOK_NUMBER = 260,              /* TOK_NUMBER  */
    TOK_DIGLAB = 261,              /* TOK_DIGLAB  */
    TOK_DIGREF_B = 262,            /* TOK_DIGREF_B  */
    TOK_DIGREF_F = 263,            /* TOK_DIGREF_F  */
    TOK_CMD_DIRECT = 264,          /* TOK_CMD_DIRECT  */
    TOK_CMD_DIRECT_PCREL = 265,    /* TOK_CMD_DIRECT_PCREL  */
    TOK_CMD_INDIR = 266,           /* TOK_CMD_INDIR  */
    TOK_CMD_FPU = 267,             /* TOK_CMD_FPU  */
    TOK_BYTE = 268,                /* TOK_BYTE  */
    TOK_HALF = 269,                /* TOK_HALF  */
    TOK_WORD = 270,                /* TOK_WORD  */
    TOK_ALIGN = 271,               /* TOK_ALIGN  */
    TOK_GLOBL = 272,               /* TOK_GLOBL  */
    TOK_COMM = 273,                /* TOK_COMM  */
    TOK_NEED = 274,                /* TOK_NEED  */
    TOK_SLOT = 275,                /* TOK_SLOT  */
    TOK_STRING_DIR = 276,          /* TOK_STRING_DIR  */
    TOK_DCOLON = 277,              /* TOK_DCOLON  */
    TOK_SEGMENT = 278,             /* TOK_SEGMENT  */
    TOK_OROR = 279,                /* TOK_OROR  */
    TOK_ANDAND = 280,              /* TOK_ANDAND  */
    TOK_EQ = 281,                  /* TOK_EQ  */
    TOK_NE = 282,                  /* TOK_NE  */
    TOK_LE = 283,                  /* TOK_LE  */
    TOK_GE = 284,                  /* TOK_GE  */
    TOK_LSH = 285,                 /* TOK_LSH  */
    TOK_RSH = 286,                 /* TOK_RSH  */
    TOP = 287                      /* TOP  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif
/* Token kinds.  */
#define YYEMPTY -2
#define YYEOF 0
#define YYerror 256
#define YYUNDEF 257
#define TOK_NAME 258
#define TOK_STRING 259
#define TOK_NUMBER 260
#define TOK_DIGLAB 261
#define TOK_DIGREF_B 262
#define TOK_DIGREF_F 263
#define TOK_CMD_DIRECT 264
#define TOK_CMD_DIRECT_PCREL 265
#define TOK_CMD_INDIR 266
#define TOK_CMD_FPU 267
#define TOK_BYTE 268
#define TOK_HALF 269
#define TOK_WORD 270
#define TOK_ALIGN 271
#define TOK_GLOBL 272
#define TOK_COMM 273
#define TOK_NEED 274
#define TOK_SLOT 275
#define TOK_STRING_DIR 276
#define TOK_DCOLON 277
#define TOK_SEGMENT 278
#define TOK_OROR 279
#define TOK_ANDAND 280
#define TOK_EQ 281
#define TOK_NE 282
#define TOK_LE 283
#define TOK_GE 284
#define TOK_LSH 285
#define TOK_RSH 286
#define TOP 287

/* Value type.  */


extern YYSTYPE yylval;


int yyparse (void);


#endif /* !YY_YY_Y_TAB_H_INCLUDED  */
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_TOK_NAME = 3,                   /* TOK_NAME  */
  YYSYMBOL_TOK_STRING = 4,                 /* TOK_STRING  */
  YYSYMBOL_TOK_NUMBER = 5,                 /* TOK_NUMBER  */
  YYSYMBOL_TOK_DIGLAB = 6,                 /* TOK_DIGLAB  */
  YYSYMBOL_TOK_DIGREF_B = 7,               /* TOK_DIGREF_B  */
  YYSYMBOL_TOK_DIGREF_F = 8,               /* TOK_DIGREF_F  */
  YYSYMBOL_TOK_CMD_DIRECT = 9,             /* TOK_CMD_DIRECT  */
  YYSYMBOL_TOK_CMD_DIRECT_PCREL = 10,      /* TOK_CMD_DIRECT_PCREL  */
  YYSYMBOL_TOK_CMD_INDIR = 11,             /* TOK_CMD_INDIR  */
  YYSYMBOL_TOK_CMD_FPU = 12,               /* TOK_CMD_FPU  */
  YYSYMBOL_TOK_BYTE = 13,                  /* TOK_BYTE  */
  YYSYMBOL_TOK_HALF = 14,                  /* TOK_HALF  */
  YYSYMBOL_TOK_WORD = 15,                  /* TOK_WORD  */
  YYSYMBOL_TOK_ALIGN = 16,                 /* TOK_ALIGN  */
  YYSYMBOL_TOK_GLOBL = 17,                 /* TOK_GLOBL  */
  YYSYMBOL_TOK_COMM = 18,                  /* TOK_COMM  */
  YYSYMBOL_TOK_NEED = 19,                  /* TOK_NEED  */
  YYSYMBOL_TOK_SLOT = 20,                  /* TOK_SLOT  */
  YYSYMBOL_TOK_STRING_DIR = 21,            /* TOK_STRING_DIR  */
  YYSYMBOL_TOK_DCOLON = 22,                /* TOK_DCOLON  */
  YYSYMBOL_TOK_SEGMENT = 23,               /* TOK_SEGMENT  */
  YYSYMBOL_24_ = 24,                       /* '?'  */
  YYSYMBOL_25_ = 25,                       /* ':'  */
  YYSYMBOL_TOK_OROR = 26,                  /* TOK_OROR  */
  YYSYMBOL_TOK_ANDAND = 27,                /* TOK_ANDAND  */
  YYSYMBOL_28_ = 28,                       /* '|'  */
  YYSYMBOL_29_ = 29,                       /* '^'  */
  YYSYMBOL_30_ = 30,                       /* '&'  */
  YYSYMBOL_TOK_EQ = 31,                    /* TOK_EQ  */
  YYSYMBOL_TOK_NE = 32,                    /* TOK_NE  */
  YYSYMBOL_33_ = 33,                       /* '<'  */
  YYSYMBOL_34_ = 34,                       /* '>'  */
  YYSYMBOL_TOK_LE = 35,                    /* TOK_LE  */
  YYSYMBOL_TOK_GE = 36,                    /* TOK_GE  */
  YYSYMBOL_TOK_LSH = 37,                   /* TOK_LSH  */
  YYSYMBOL_TOK_RSH = 38,                   /* TOK_RSH  */
  YYSYMBOL_39_ = 39,                       /* '+'  */
  YYSYMBOL_40_ = 40,                       /* '-'  */
  YYSYMBOL_41_ = 41,                       /* '*'  */
  YYSYMBOL_42_ = 42,                       /* '/'  */
  YYSYMBOL_43_ = 43,                       /* '%'  */
  YYSYMBOL_TOP = 44,                       /* TOP  */
  YYSYMBOL_45_ = 45,                       /* ';'  */
  YYSYMBOL_46_ = 46,                       /* ','  */
  YYSYMBOL_47_ = 47,                       /* '['  */
  YYSYMBOL_48_ = 48,                       /* ']'  */
  YYSYMBOL_49_ = 49,                       /* '='  */
  YYSYMBOL_50_ = 50,                       /* '.'  */
  YYSYMBOL_51_ = 51,                       /* '('  */
  YYSYMBOL_52_ = 52,                       /* ')'  */
  YYSYMBOL_53_ = 53,                       /* '!'  */
  YYSYMBOL_54_ = 54,                       /* '~'  */
  YYSYMBOL_YYACCEPT = 55,                  /* $accept  */
  YYSYMBOL_source = 56,                    /* source  */
  YYSYMBOL_initialize = 57,                /* initialize  */
  YYSYMBOL_statements = 58,                /* statements  */
  YYSYMBOL_statement = 59,                 /* statement  */
  YYSYMBOL_label = 60,                     /* label  */
  YYSYMBOL_command = 61,                   /* command  */
  YYSYMBOL_directive = 62,                 /* directive  */
  YYSYMBOL_63_1 = 63,                      /* $@1  */
  YYSYMBOL_64_2 = 64,                      /* $@2  */
  YYSYMBOL_65_3 = 65,                      /* $@3  */
  YYSYMBOL_maybe_filler = 66,              /* maybe_filler  */
  YYSYMBOL_datalist = 67,                  /* datalist  */
  YYSYMBOL_datum = 68,                     /* datum  */
  YYSYMBOL_assignment = 69,                /* assignment  */
  YYSYMBOL_expr = 70                       /* expr  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_int8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  3
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   367

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  55
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  16
/* YYNRULES -- Number of rules.  */
#define YYNRULES  70
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  120

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   287


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    53,     2,     2,     2,    43,    30,     2,
      51,    52,    41,    39,    46,    40,    50,    42,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    25,    45,
      33,    49,    34,    24,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    47,     2,    48,    29,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,    28,     2,    54,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    26,
      27,    31,    32,    35,    36,    37,    38,    44
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    90,    90,   125,   153,   154,   166,   167,   168,   169,
     174,   175,   179,   200,   210,   225,   232,   247,   255,   267,
     291,   309,   350,   369,   381,   381,   383,   391,   391,   399,
     399,   407,   433,   438,   444,   445,   448,   457,   467,   475,
     484,   497,   511,   515,   519,   525,   529,   536,   537,   538,
     539,   540,   541,   542,   543,   544,   545,   546,   547,   548,
     549,   550,   551,   552,   553,   554,   555,   556,   557,   558,
     559
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "TOK_NAME",
  "TOK_STRING", "TOK_NUMBER", "TOK_DIGLAB", "TOK_DIGREF_B", "TOK_DIGREF_F",
  "TOK_CMD_DIRECT", "TOK_CMD_DIRECT_PCREL", "TOK_CMD_INDIR", "TOK_CMD_FPU",
  "TOK_BYTE", "TOK_HALF", "TOK_WORD", "TOK_ALIGN", "TOK_GLOBL", "TOK_COMM",
  "TOK_NEED", "TOK_SLOT", "TOK_STRING_DIR", "TOK_DCOLON", "TOK_SEGMENT",
  "'?'", "':'", "TOK_OROR", "TOK_ANDAND", "'|'", "'^'", "'&'", "TOK_EQ",
  "TOK_NE", "'<'", "'>'", "TOK_LE", "TOK_GE", "TOK_LSH", "TOK_RSH", "'+'",
  "'-'", "'*'", "'/'", "'%'", "TOP", "';'", "','", "'['", "']'", "'='",
  "'.'", "'('", "')'", "'!'", "'~'", "$accept", "source", "initialize",
  "statements", "statement", "label", "command", "directive", "$@1", "$@2",
  "$@3", "maybe_filler", "datalist", "datum", "assignment", "expr", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-42)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-3)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     -42,     2,   -42,   -42,   113,   -42,   -19,    -6,    42,    42,
     -42,   -42,   -42,   -42,   -42,    42,    17,    18,    43,    42,
      44,   -42,   -42,   -42,   -42,   -42,   -42,   -42,   -42,   -42,
      42,   -42,   -42,   -42,   -42,   -42,    42,    42,   -42,    42,
      42,    42,   249,   249,    67,    67,    67,   206,   -42,    27,
     -42,   249,   -42,   249,   -42,   -42,   111,   -42,   -42,    42,
      42,    42,    42,    42,    42,    42,    42,    42,    42,    42,
      42,    42,    42,    42,    42,    42,    42,    42,   -42,    29,
      32,   -42,   184,    32,    32,    42,   -42,    42,   -42,   229,
     266,   282,   297,   311,   324,     1,     1,   -25,   -25,   -25,
     -25,   -16,   -16,   -33,   -33,   -42,   -42,   -42,    42,    67,
      42,   249,   249,    42,   138,   -42,   161,   249,   -42,   -42
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       3,     0,     4,     1,     0,    11,     0,     0,     0,     0,
      17,    18,    24,    27,    29,     0,     0,     0,     0,     0,
       0,    19,    10,     5,     6,     7,     8,     9,    13,    12,
       0,    14,    43,    42,    45,    46,     0,     0,    44,     0,
       0,     0,    15,    16,     0,     0,     0,    32,    20,     0,
      22,    23,    26,    41,    66,    67,     0,    68,    69,     0,
       0,     0,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,     0,     0,    40,    37,
      25,    34,    36,    28,    30,     0,    31,     0,    47,     0,
      64,    65,    52,    51,    50,    60,    61,    56,    57,    58,
      59,    62,    63,    48,    49,    53,    54,    55,     0,     0,
       0,    33,    21,     0,     0,    35,     0,    70,    39,    38
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -42,   -42,   -42,   -42,   -42,   -42,   -42,   -42,   -42,   -42,
     -42,   -42,   -41,   -29,   -42,    -8
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
       0,     1,     2,     4,    23,    24,    25,    26,    44,    45,
      46,    86,    80,    81,    27,    82
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int8 yytable[] =
{
      42,    43,     3,    28,    83,    84,    29,    47,    75,    76,
      77,    51,    71,    72,    73,    74,    75,    76,    77,    31,
      48,    49,    53,    73,    74,    75,    76,    77,    54,    55,
      30,    56,    57,    58,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    32,    50,    33,    52,    34,
      35,    89,    90,    91,    92,    93,    94,    95,    96,    97,
      98,    99,   100,   101,   102,   103,   104,   105,   106,   107,
      32,    78,    33,    87,    34,    35,   108,   111,   109,   112,
     115,    36,    37,     0,     0,     0,     0,     0,     0,     0,
       0,    79,    38,    39,     0,    40,    41,     0,     0,     0,
     114,     0,   116,     0,     0,   117,    36,    37,     0,     0,
       0,     0,     0,    -2,     5,     0,     6,    38,    39,     7,
      40,    41,     8,     9,    10,    11,    12,    13,    14,    15,
      16,    17,    18,    19,    20,    59,    21,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,     0,     0,     0,    22,     0,
       0,     0,    59,    88,    60,    61,    62,    63,    64,    65,
      66,    67,    68,    69,    70,    71,    72,    73,    74,    75,
      76,    77,     0,     0,     0,    59,   118,    60,    61,    62,
      63,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,     0,     0,     0,    59,   119,
      60,    61,    62,    63,    64,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    74,    75,    76,    77,     0,     0,
      59,   110,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
       0,     0,    85,    59,   113,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    59,     0,    60,    61,    62,    63,    64,
      65,    66,    67,    68,    69,    70,    71,    72,    73,    74,
      75,    76,    77,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
      62,    63,    64,    65,    66,    67,    68,    69,    70,    71,
      72,    73,    74,    75,    76,    77,    63,    64,    65,    66,
      67,    68,    69,    70,    71,    72,    73,    74,    75,    76,
      77,    64,    65,    66,    67,    68,    69,    70,    71,    72,
      73,    74,    75,    76,    77,    65,    66,    67,    68,    69,
      70,    71,    72,    73,    74,    75,    76,    77
};

static const yytype_int8 yycheck[] =
{
       8,     9,     0,    22,    45,    46,    25,    15,    41,    42,
      43,    19,    37,    38,    39,    40,    41,    42,    43,    25,
       3,     3,    30,    39,    40,    41,    42,    43,    36,    37,
      49,    39,    40,    41,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,     3,     3,     5,     4,     7,
       8,    59,    60,    61,    62,    63,    64,    65,    66,    67,
      68,    69,    70,    71,    72,    73,    74,    75,    76,    77,
       3,     4,     5,    46,     7,     8,    47,    85,    46,    87,
     109,    39,    40,    -1,    -1,    -1,    -1,    -1,    -1,    -1,
      -1,    24,    50,    51,    -1,    53,    54,    -1,    -1,    -1,
     108,    -1,   110,    -1,    -1,   113,    39,    40,    -1,    -1,
      -1,    -1,    -1,     0,     1,    -1,     3,    50,    51,     6,
      53,    54,     9,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    24,    23,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    -1,    -1,    -1,    45,    -1,
      -1,    -1,    24,    52,    26,    27,    28,    29,    30,    31,
      32,    33,    34,    35,    36,    37,    38,    39,    40,    41,
      42,    43,    -1,    -1,    -1,    24,    48,    26,    27,    28,
      29,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    -1,    -1,    -1,    24,    48,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,    -1,    -1,
      24,    47,    26,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      -1,    -1,    46,    24,    25,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    24,    -1,    26,    27,    28,    29,    30,
      31,    32,    33,    34,    35,    36,    37,    38,    39,    40,
      41,    42,    43,    27,    28,    29,    30,    31,    32,    33,
      34,    35,    36,    37,    38,    39,    40,    41,    42,    43,
      28,    29,    30,    31,    32,    33,    34,    35,    36,    37,
      38,    39,    40,    41,    42,    43,    29,    30,    31,    32,
      33,    34,    35,    36,    37,    38,    39,    40,    41,    42,
      43,    30,    31,    32,    33,    34,    35,    36,    37,    38,
      39,    40,    41,    42,    43,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,    56,    57,     0,    58,     1,     3,     6,     9,    10,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      21,    23,    45,    59,    60,    61,    62,    69,    22,    25,
      49,    25,     3,     5,     7,     8,    39,    40,    50,    51,
      53,    54,    70,    70,    63,    64,    65,    70,     3,     3,
       3,    70,     4,    70,    70,    70,    70,    70,    70,    24,
      26,    27,    28,    29,    30,    31,    32,    33,    34,    35,
      36,    37,    38,    39,    40,    41,    42,    43,     4,    24,
      67,    68,    70,    67,    67,    46,    66,    46,    52,    70,
      70,    70,    70,    70,    70,    70,    70,    70,    70,    70,
      70,    70,    70,    70,    70,    70,    70,    70,    47,    46,
      47,    70,    70,    25,    70,    68,    70,    70,    48,    48
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    55,    56,    57,    58,    58,    59,    59,    59,    59,
      59,    59,    60,    60,    60,    61,    61,    61,    61,    62,
      62,    62,    62,    62,    63,    62,    62,    64,    62,    65,
      62,    62,    66,    66,    67,    67,    68,    68,    68,    68,
      68,    69,    70,    70,    70,    70,    70,    70,    70,    70,
      70,    70,    70,    70,    70,    70,    70,    70,    70,    70,
      70,    70,    70,    70,    70,    70,    70,    70,    70,    70,
      70
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     2,     0,     0,     2,     1,     1,     1,     1,
       1,     1,     2,     2,     2,     2,     2,     1,     1,     1,
       2,     4,     2,     2,     0,     3,     2,     0,     3,     0,
       3,     3,     0,     2,     1,     3,     1,     1,     4,     4,
       1,     3,     1,     1,     1,     1,     1,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     2,     2,     2,     2,
       5
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* source: initialize statements  */
#line 90 "parse.y"
                                {
	    /*
	     * Tie ends at the end of parsing
	     */
	    int i;

	    /* line number would not be meaningful in the error()s
               below; turn it off */
	    error_report->line_number_ptr = NULL;

	    /* check whether all forward refs to digital labels have
               met their labels */
	    for (i=0; i <= 9; i++) {
		if (forwlab[i] != NULL) {
		    error ("hanging forward reference to digital label %d\n");
		    forwlab[i]->defined = 1;  /* do not catch it again below */
		}
	    }

	    /* complain if there are undefined name references */
	    table_walk (&general_names, check_defined);

	    /* Add deferred needs now that all names are defined */
	    for (i=0; i < ndeferred_needs; i++) {
		expr e = expr_name (deferred_needs[i].dep);

		expr_reduce (e);
		record_needs_name = deferred_needs[i].name;
		record_needs (e);
		EXPR_FREE (e);
	    }
	}
#line 1419 "y.tab.c"
    break;

  case 3: /* initialize: %empty  */
#line 125 "parse.y"
                      {
	    /*
	     * Initialization of data used in actions
	     */

	    /* We make default fragments noname, because default
	       fragments in different segments/files should not be
	       unified, and noname symbols provide exactly this
	       semantics. */
	    name deffrag = table_add_noname (&general_names);

	    /* Unlike default fragments, default segment is not
               noname: instances of the default segment in different
               files should be merged into a single segment during
               link phase. */
	    name segname = table_lookup (&segment_names, DEFAULT_SEGMENT_IDENT,
					 sizeof (DEFAULT_SEGMENT_IDENT)-1);

	    globalize_name (deffrag);
	    deffrag->fragment = 1;
	    deffrag->defined = 1;
	    curseg = add_segment (segname, deffrag);

	    memset (forwlab, 0, sizeof (forwlab));
	    memset (backlab, 0, sizeof (backlab));
	}
#line 1450 "y.tab.c"
    break;

  case 5: /* statements: statements statement  */
#line 154 "parse.y"
                               {
	    if (traceset & TRACE_POINT) {
		printf ("** POINT ");
		expr_print (stdout, curseg->point);
		printf ("\n");
	    }
	    if (traceset & TRACE_EXPR)
		expr_fsck ();
	}
#line 1464 "y.tab.c"
    break;

  case 12: /* label: TOK_NAME ':'  */
#line 179 "parse.y"
                       {
	    if ((yyvsp[-1].u.name)->defined)
		error2 ((yyvsp[-1].file), (yyvsp[-1].line),
			"'%s' already defined\n", (yyvsp[-1].u.name)->ident);
	    (yyvsp[-1].u.name)->defined = 1;
	    if ((yyvsp[-1].u.name)->global) {
		/* public label -- start new fragment */
		(yyvsp[-1].u.name)->fragment = 1;
		(yyvsp[-1].u.name)->align = curseg->pending_alignment;
		drop_pending_alignment (curseg);
		add_fragment (curseg, (yyvsp[-1].u.name));
	    }
	    else {
		(yyvsp[-1].u.name)->align = curseg->pending_alignment;
		if (curseg->pending_alignment > 1)
		    do_pending_alignment (curseg);
		/* local label; assign it the current point value */
		name_assign_value ((yyvsp[-1].u.name), curseg->point);
	    }
	}
#line 1489 "y.tab.c"
    break;

  case 13: /* label: TOK_NAME TOK_DCOLON  */
#line 200 "parse.y"
                              {
	    if ((yyvsp[-1].u.name)->defined)
		error2 ((yyvsp[-1].file), (yyvsp[-1].line),
			"'%s' already defined\n", (yyvsp[-1].u.name)->ident);
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    (yyvsp[-1].u.name)->defined = 1;
	    name_assign_value ((yyvsp[-1].u.name), curseg->point);
	}
#line 1503 "y.tab.c"
    break;

  case 14: /* label: TOK_DIGLAB ':'  */
#line 210 "parse.y"
                         {	
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    if (backlab[(yyvsp[-1].u.num)])
		EXPR_FREE (backlab[(yyvsp[-1].u.num)]);
	    backlab[(yyvsp[-1].u.num)] = expr_share (curseg->point);
	    if (forwlab[(yyvsp[-1].u.num)] != NULL) {
		name_assign_value (forwlab[(yyvsp[-1].u.num)], curseg->point);
		forwlab[(yyvsp[-1].u.num)]->defined = 1;
		forwlab[(yyvsp[-1].u.num)] = NULL;
	    }
	}
#line 1520 "y.tab.c"
    break;

  case 15: /* command: TOK_CMD_DIRECT expr  */
#line 225 "parse.y"
                              {
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    emit_cmd (curseg, (yyvsp[-1].u.num), (yyvsp[0].u.expr));
	    EXPR_FREE ((yyvsp[0].u.expr));
	}
#line 1531 "y.tab.c"
    break;

  case 16: /* command: TOK_CMD_DIRECT_PCREL expr  */
#line 232 "parse.y"
                                    {
	    name np = table_add_noname (&general_names);
	    expr temp;

	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);

	    temp = expr_binop_owned (EXPR_MINUS, (yyvsp[0].u.expr), expr_name (np));
	    emit_cmd (curseg, (yyvsp[-1].u.num), temp);
	    EXPR_FREE (temp);

	    name_assign_value (np, curseg->point);
	    np->defined = 1;
	}
#line 1550 "y.tab.c"
    break;

  case 17: /* command: TOK_CMD_INDIR  */
#line 247 "parse.y"
                        {
	    expr temp;
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    emit_cmd (curseg, OPCODE_OPR, temp = expr_const ((yyvsp[0].u.num)));
	    EXPR_FREE (temp);
	}
#line 1562 "y.tab.c"
    break;

  case 18: /* command: TOK_CMD_FPU  */
#line 255 "parse.y"
                      {
	    expr temp;
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    emit_cmd (curseg, OPCODE_LDC, temp = expr_const ((yyvsp[0].u.num)));
	    EXPR_FREE (temp);
	    emit_cmd (curseg, OPCODE_OPR, temp = expr_const (OPCODE_FPENTRY));
	    EXPR_FREE (temp);
	}
#line 1576 "y.tab.c"
    break;

  case 19: /* directive: TOK_SEGMENT  */
#line 267 "parse.y"
                      {
	    if ((yyvsp[0].u.name)->val)
		curseg = (segment) (yyvsp[0].u.name)->val;
	    else {
		name deffrag = table_add_noname (&general_names);

		globalize_name (deffrag);
		deffrag->fragment = 1;
		deffrag->defined = 1;

		curseg = add_segment ((yyvsp[0].u.name), deffrag);

		/* For .bss, issue the second default fragment.  See
                   the comment on the .comm handling code. */
		if (strcmp ((yyvsp[0].u.name)->ident, "bss") == 0) {
		    deffrag = table_add_noname (&general_names);
		    globalize_name (deffrag);
		    deffrag->fragment = 1;
		    deffrag->defined = 1;
		    add_fragment (curseg, deffrag);
		}
	    }
	}
#line 1604 "y.tab.c"
    break;

  case 20: /* directive: TOK_GLOBL TOK_NAME  */
#line 291 "parse.y"
                             {
	    if (! (yyvsp[0].u.name)->global && (yyvsp[0].u.name)->defined)
		error2 ((yyvsp[0].file), (yyvsp[0].line),
			"'%s' already used as non-global\n", (yyvsp[0].u.name)->ident);
	    globalize_name ((yyvsp[0].u.name));
	}
#line 1615 "y.tab.c"
    break;

  case 21: /* directive: TOK_COMM TOK_NAME ',' expr  */
#line 309 "parse.y"
                                     {
	    expr temp;
	    segment segbss;
	    name segname = table_lookup (&segment_names, "bss",
					 sizeof ("bss")-1);
	    if (segname->val)
		segbss = (segment) segname->val;
	    else {
		name deffrag = table_add_noname (&general_names);

		globalize_name (deffrag);
		deffrag->fragment = 1;
		deffrag->defined = 1;

		segbss = add_segment (segname, deffrag);

		/* add 2nd default fragment, .bss-specific */
		deffrag = table_add_noname (&general_names);
		globalize_name (deffrag);
		deffrag->fragment = 1;
		deffrag->defined = 1;
		add_fragment (segbss, deffrag);
	    }

	    if ((yyvsp[-2].u.name)->defined)
		error2 ((yyvsp[-2].file), (yyvsp[-2].line),
			"'%s' already defined\n", (yyvsp[-2].u.name)->ident);
	    globalize_name ((yyvsp[-2].u.name));
	    (yyvsp[-2].u.name)->fragment = 1;
	    (yyvsp[-2].u.name)->defined = 1;
	    (yyvsp[-2].u.name)->common = 1;
	    (yyvsp[-2].u.name)->align = curseg->pending_alignment;  /* curseg, not segbss. */
	    drop_pending_alignment (curseg);

	    add_fragment (segbss, (yyvsp[-2].u.name));
	    emit_data (segbss, 1, temp = expr_const (0), (yyvsp[0].u.expr));
	    EXPR_FREE (temp);
	    swap_last_fragments (segbss);
	    EXPR_FREE ((yyvsp[0].u.expr));
	}
#line 1660 "y.tab.c"
    break;

  case 22: /* directive: TOK_NEED TOK_NAME  */
#line 350 "parse.y"
                            {
	    if ((yyvsp[0].u.name)->global)
		add_need (curseg->fragments[curseg->nfragments-1].name, (yyvsp[0].u.name));
	    else {
		/* For a nonglobal name, we should add needs for all
                   global names this one is defined through.  However,
                   we cannot do this right away, since the name might
                   not be defined yet.  So we defer actual need-adding
                   until the end of parsing */
		if (VECTOR_FULL (64, ndeferred_needs))
		    VECTOR_GROW (64, ndeferred_needs,
				 struct deferred_needs, deferred_needs);
		deferred_needs[ndeferred_needs].name
		    = curseg->fragments[curseg->nfragments-1].name;
		deferred_needs[ndeferred_needs].dep = (yyvsp[0].u.name);
		ndeferred_needs++;
	    }
	}
#line 1683 "y.tab.c"
    break;

  case 23: /* directive: TOK_SLOT expr  */
#line 369 "parse.y"
                        {
	    if (! EXPR_ISCONST ((yyvsp[0].u.expr))) {
		error2 ((yyvsp[0].file), (yyvsp[0].line),
			"%s should be a constant expression\n",
			"the argument for .slot");
		EXPR_FREE ((yyvsp[0].u.expr));
		break;
	    }
	    curseg->slot = (yyvsp[0].u.expr)->u.constval;
	    EXPR_FREE ((yyvsp[0].u.expr));
	}
#line 1699 "y.tab.c"
    break;

  case 24: /* $@1: %empty  */
#line 381 "parse.y"
                   {data_width = 1;}
#line 1705 "y.tab.c"
    break;

  case 26: /* directive: TOK_STRING_DIR TOK_STRING  */
#line 383 "parse.y"
                                    {
	    /* .string "..." — emit string data plus null terminator */
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    emit_block (curseg, (yyvsp[0].u.str).ptr, (yyvsp[0].u.str).len);
	    emit_block (curseg, "\0", 1);
	}
#line 1717 "y.tab.c"
    break;

  case 27: /* $@2: %empty  */
#line 391 "parse.y"
                   {
	    data_width = 2;
	    if (curseg->pending_alignment < 2
		&& expr_check_alignment (curseg->point, 2) >= 2)
		warning2 ((yyvsp[0].file), (yyvsp[0].line),
			"data don't seem to be aligned properly\n");
	  }
#line 1729 "y.tab.c"
    break;

  case 29: /* $@3: %empty  */
#line 399 "parse.y"
                   {
	    data_width = 4;
	    if (curseg->pending_alignment < 4
		&& expr_check_alignment (curseg->point, 4) >= 4)
		warning2 ((yyvsp[0].file), (yyvsp[0].line),
			"data don't seem to be aligned properly\n");
	  }
#line 1741 "y.tab.c"
    break;

  case 31: /* directive: TOK_ALIGN expr maybe_filler  */
#line 407 "parse.y"
                                      {
	    if (! EXPR_ISCONST ((yyvsp[-1].u.expr))) {
		error2 ((yyvsp[-1].file), (yyvsp[-1].line),
			"%s should be a constant expression\n",
			"alignment factor");
		EXPR_FREE ((yyvsp[-1].u.expr));
		EXPR_FREE ((yyvsp[0].u.expr));
		break;
	    }
	    if ((yyvsp[-1].u.expr)->u.constval < 1) {
		warning2 ((yyvsp[-1].file), (yyvsp[-1].line),
			  "alignment of %d is meaningless\n", (yyvsp[-1].u.expr)->u.constval);
		break;
	    }
	    /* Semantics of two succesive .align's is that the first
               one is emitted in the current fragment, and the second
               one becomes the pending alignment. */
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    curseg->pending_alignment = (yyvsp[-1].u.expr)->u.constval;
	    curseg->pending_alignment_filler = (yyvsp[0].u.expr);
	    EXPR_FREE ((yyvsp[-1].u.expr));
	}
#line 1769 "y.tab.c"
    break;

  case 32: /* maybe_filler: %empty  */
#line 433 "parse.y"
                                            {
	    (yyval.u.expr) = expr_const (strcmp (curseg->name->ident, "text") == 0
			     ? OPCODE_PFIX
			     : 0);
	}
#line 1779 "y.tab.c"
    break;

  case 33: /* maybe_filler: ',' expr  */
#line 438 "parse.y"
                   {
	    (yyval.u.expr) = (yyvsp[0].u.expr);
	}
#line 1787 "y.tab.c"
    break;

  case 36: /* datum: expr  */
#line 448 "parse.y"
               {
	    expr temp;
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    emit_data (curseg, data_width, (yyvsp[0].u.expr), temp = expr_const (1));
	    EXPR_FREE (temp);
	    EXPR_FREE ((yyvsp[0].u.expr));
	}
#line 1800 "y.tab.c"
    break;

  case 37: /* datum: '?'  */
#line 457 "parse.y"
              {
	    expr count = expr_const (1);
	    expr data = expr_const (0);
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    emit_data (curseg, data_width, data, count);
	    EXPR_FREE (data);
	    EXPR_FREE (count);
	}
#line 1814 "y.tab.c"
    break;

  case 38: /* datum: expr '[' expr ']'  */
#line 467 "parse.y"
                            {
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    emit_data (curseg, data_width, (yyvsp[-3].u.expr), (yyvsp[-1].u.expr));
	    EXPR_FREE ((yyvsp[-3].u.expr));
	    EXPR_FREE ((yyvsp[-1].u.expr));
	}
#line 1826 "y.tab.c"
    break;

  case 39: /* datum: '?' '[' expr ']'  */
#line 475 "parse.y"
                           {
	    expr data = expr_const (0);
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    emit_data (curseg, data_width, data, (yyvsp[-1].u.expr));
	    EXPR_FREE (data);
	    EXPR_FREE ((yyvsp[-1].u.expr));
	}
#line 1839 "y.tab.c"
    break;

  case 40: /* datum: TOK_STRING  */
#line 484 "parse.y"
                     {
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    if (data_width != 1) {
		error2 ((yyvsp[0].file), (yyvsp[0].line),
		  "strings are only allowed with .byte\n");
		break;
	    }
	    emit_block (curseg, (yyvsp[0].u.str).ptr, (yyvsp[0].u.str).len);
	}
#line 1854 "y.tab.c"
    break;

  case 41: /* assignment: TOK_NAME '=' expr  */
#line 497 "parse.y"
                            {
	    if ((yyvsp[-2].u.name)->defined) {
		error2 ((yyvsp[-2].file), (yyvsp[-2].line),
			"'%s' already defined\n", (yyvsp[-2].u.name)->ident);
		EXPR_FREE ((yyvsp[0].u.expr));
		break;
	    }
	    (yyvsp[-2].u.name)->defined = 1;
	    name_assign_value ((yyvsp[-2].u.name), (yyvsp[0].u.expr));
	    EXPR_FREE ((yyvsp[0].u.expr));
	}
#line 1870 "y.tab.c"
    break;

  case 42: /* expr: TOK_NUMBER  */
#line 511 "parse.y"
                     {
	    (yyval.u.expr) = expr_const ((yyvsp[0].u.lnum));
	}
#line 1878 "y.tab.c"
    break;

  case 43: /* expr: TOK_NAME  */
#line 515 "parse.y"
                   {
	    (yyval.u.expr) = expr_name ((yyvsp[0].u.name));
	}
#line 1886 "y.tab.c"
    break;

  case 44: /* expr: '.'  */
#line 519 "parse.y"
              {
	    if (curseg->pending_alignment > 1)
		do_pending_alignment (curseg);
	    (yyval.u.expr) = expr_share (curseg->point);
	}
#line 1896 "y.tab.c"
    break;

  case 45: /* expr: TOK_DIGREF_B  */
#line 525 "parse.y"
                       {
	    (yyval.u.expr) = expr_share (backlab[(yyvsp[0].u.num)]);
	}
#line 1904 "y.tab.c"
    break;

  case 46: /* expr: TOK_DIGREF_F  */
#line 529 "parse.y"
                       {
	    if (forwlab[(yyvsp[0].u.num)] == NULL) {
		forwlab[(yyvsp[0].u.num)] = table_add_noname (&general_names);
	    }
	    (yyval.u.expr) = expr_name (forwlab[(yyvsp[0].u.num)]);
	}
#line 1915 "y.tab.c"
    break;

  case 47: /* expr: '(' expr ')'  */
#line 536 "parse.y"
                                { (yyval.u.expr) = (yyvsp[-1].u.expr); }
#line 1921 "y.tab.c"
    break;

  case 48: /* expr: expr '+' expr  */
#line 537 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_PLUS, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 1927 "y.tab.c"
    break;

  case 49: /* expr: expr '-' expr  */
#line 538 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_MINUS, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 1933 "y.tab.c"
    break;

  case 50: /* expr: expr '&' expr  */
#line 539 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_AND, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 1939 "y.tab.c"
    break;

  case 51: /* expr: expr '^' expr  */
#line 540 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_XOR, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 1945 "y.tab.c"
    break;

  case 52: /* expr: expr '|' expr  */
#line 541 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_OR, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 1951 "y.tab.c"
    break;

  case 53: /* expr: expr '*' expr  */
#line 542 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_MUL, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 1957 "y.tab.c"
    break;

  case 54: /* expr: expr '/' expr  */
#line 543 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_DIV, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 1963 "y.tab.c"
    break;

  case 55: /* expr: expr '%' expr  */
#line 544 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_REM, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 1969 "y.tab.c"
    break;

  case 56: /* expr: expr '<' expr  */
#line 545 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_LT, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 1975 "y.tab.c"
    break;

  case 57: /* expr: expr '>' expr  */
#line 546 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_GT, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 1981 "y.tab.c"
    break;

  case 58: /* expr: expr TOK_LE expr  */
#line 547 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_LE, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 1987 "y.tab.c"
    break;

  case 59: /* expr: expr TOK_GE expr  */
#line 548 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_GE, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 1993 "y.tab.c"
    break;

  case 60: /* expr: expr TOK_EQ expr  */
#line 549 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_EQ, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 1999 "y.tab.c"
    break;

  case 61: /* expr: expr TOK_NE expr  */
#line 550 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_NE, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 2005 "y.tab.c"
    break;

  case 62: /* expr: expr TOK_LSH expr  */
#line 551 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_LSH, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 2011 "y.tab.c"
    break;

  case 63: /* expr: expr TOK_RSH expr  */
#line 552 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_RSH, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 2017 "y.tab.c"
    break;

  case 64: /* expr: expr TOK_OROR expr  */
#line 553 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_OROR, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 2023 "y.tab.c"
    break;

  case 65: /* expr: expr TOK_ANDAND expr  */
#line 554 "parse.y"
                                { (yyval.u.expr) = expr_binop_owned (EXPR_ANDAND, (yyvsp[-2].u.expr), (yyvsp[0].u.expr)); }
#line 2029 "y.tab.c"
    break;

  case 66: /* expr: '+' expr  */
#line 555 "parse.y"
                                { (yyval.u.expr) = (yyvsp[0].u.expr); }
#line 2035 "y.tab.c"
    break;

  case 67: /* expr: '-' expr  */
#line 556 "parse.y"
                                { (yyval.u.expr) = expr_unop_owned (EXPR_NEG, (yyvsp[0].u.expr)); }
#line 2041 "y.tab.c"
    break;

  case 68: /* expr: '!' expr  */
#line 557 "parse.y"
                                { (yyval.u.expr) = expr_unop_owned (EXPR_NOT, (yyvsp[0].u.expr)); }
#line 2047 "y.tab.c"
    break;

  case 69: /* expr: '~' expr  */
#line 558 "parse.y"
                                { (yyval.u.expr) = expr_unop_owned (EXPR_COMPL, (yyvsp[0].u.expr)); }
#line 2053 "y.tab.c"
    break;

  case 70: /* expr: expr '?' expr ':' expr  */
#line 559 "parse.y"
                                 {
	    (yyval.u.expr) = expr_questop ((yyvsp[-4].u.expr), (yyvsp[-2].u.expr), (yyvsp[0].u.expr));
	    EXPR_FREE ((yyvsp[-4].u.expr));
	    EXPR_FREE ((yyvsp[-2].u.expr));
	    EXPR_FREE ((yyvsp[0].u.expr));
	}
#line 2064 "y.tab.c"
    break;


#line 2068 "y.tab.c"

      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}

#line 566 "parse.y"


static void
yyerror (char *errstr)
{
       error ("%s\n", errstr);
}

static void
do_pending_alignment (struct segment *seg)
{

    if (emit_align (seg, seg->pending_alignment, seg->pending_alignment_filler)) {
	/* emit_align has actually emitted something; let's make the
           point look aligned for expr_check_alignment to be able to
           do its job  */
	name np = table_add_noname (&general_names);

	name_assign_value (np, seg->point);
	np->align = seg->pending_alignment;
	np->defined = 1;
	EXPR_FREE (seg->point);
	seg->point = expr_name (np);
    }

    drop_pending_alignment (seg);
}

static void
drop_pending_alignment (struct segment *seg)
{
    seg->pending_alignment = 1;
    if (seg->pending_alignment_filler) {
	EXPR_FREE (seg->pending_alignment_filler);
	seg->pending_alignment_filler = NULL;
    }
}

/* Issue an error message if name is not defined.  Used over the whole
   namespace once the source parsing is complete. */

static void *
check_defined (name np)
{
    if (!np->defined && !np->global)
	error ("`%s' undefined\n", np->ident);
    return NULL;
}

/* Print a token in readable form for debugging purposes */

void
print_token (FILE *stream, int token, YYSTYPE *tp)
{
    fprintf (stream, "** LEX %s:%d ", tp->file, tp->line);
    switch (token) {
    case TOK_NAME:
	fprintf (stream, "NAME %s\n", tp->u.name->ident);
	return;
    case TOK_STRING:
	{
	    int i;
	    fprintf (stream, "STRING \"");
	    for (i=0; i < tp->u.str.len; i++)
		fprintf (stream, isprint (tp->u.str.ptr[i])? "%c": "\\%03o",
			 tp->u.str.ptr[i]);
	    fprintf (stream, "\"\n");
	    return;
	}
    case TOK_NUMBER:
	fprintf (stream, "NUMBER %ld (0x%lx)\n", tp->u.lnum, tp->u.lnum);
	return;
    case TOK_DIGLAB:
	fprintf (stream, "DIGLAB %d\n", tp->u.num);
	return;
    case TOK_DIGREF_B:
	fprintf (stream, "DIGREF_B %d\n", tp->u.num);
	return;
    case TOK_DIGREF_F:
	fprintf (stream, "DIGREF_F %d\n", tp->u.num);
	return;
    case TOK_CMD_DIRECT:
	fprintf (stream, "CMD_DIRECT 0x%x\n", tp->u.num);
	return;
    case TOK_CMD_DIRECT_PCREL:
	fprintf (stream, "CMD_DIRECT_PCREL 0x%x\n", tp->u.num);
	return;
    case TOK_CMD_INDIR:
	fprintf (stream, "CMD_INDIR 0x%x\n", tp->u.num);
	return;
    case TOK_CMD_FPU:
	fprintf (stream, "CMD_FPU 0x%x\n", tp->u.num);
	return;
    case TOK_BYTE:
	fprintf (stream, "BYTE\n");
	return;
    case TOK_HALF:
	fprintf (stream, "HALF\n");
	return;
    case TOK_WORD:
	fprintf (stream, "WORD\n");
	return;
    case TOK_ALIGN:
	fprintf (stream, "ALIGN\n");
	return;
    case TOK_GLOBL:
	fprintf (stream, "GLOBL\n");
	return;
    case TOK_NEED:
	fprintf (stream, "NEED\n");
	return;
    case TOK_SLOT:
	fprintf (stream, "SLOT\n");
	return;
    case TOK_DCOLON:
	fprintf (stream, "DCOLON\n");
	return;
    case TOK_SEGMENT:
	fprintf (stream, "SEGMENT %s\n", tp->u.name->ident);
	return;
    case TOK_OROR:
	fprintf (stream, "OROR\n");
	return;
    case TOK_ANDAND:
	fprintf (stream, "ANDAND\n");
	return;
    case TOK_EQ:
	fprintf (stream, "EQ\n");
	return;
    case TOK_NE:
	fprintf (stream, "NE\n");
	return;
    case TOK_LE:
	fprintf (stream, "LE\n");
	return;
    case TOK_GE:
	fprintf (stream, "GE\n");
	return;
    case TOK_LSH:
	fprintf (stream, "LSH\n");
	return;
    case TOK_RSH:
	fprintf (stream, "RSH\n");
	return;
    default:
	if (token >= ' ' && token < 0x7f)
	    fprintf (stream, "'%c'\n", token);
	else
	    abort ();
	return;
    }
}

void
expr_fsck ()
{
#ifdef EXPR_DEBUG
    expr e;
    int i;

    for (e = expr_list->forw; e != expr_list; e = e->forw)
	e->refcount = 0;

    pass_count_refs ();
    table_walk (&general_names, name_count_refs);
    for (i=0; i < nsegments; i++) {
	if (segments[i]->point)
	    expr_count_refs (segments[i]->point);
	if (segments[i]->pending_alignment_filler)
	    expr_count_refs (segments[i]->pending_alignment_filler);
    }
    for (i=0; i <= 9; i++) {
	if (backlab[i])
	    expr_count_refs (backlab[i]);
    }

    for (e = expr_list->forw; e != expr_list; e = e->forw) {
	if (e->refcount != e->nrefs) {
	    printf ("** EXPR %p nrefs %u -- counted %u\n",
		    e, e->nrefs, e->refcount);
	}
	else if (e->refcount == 0) {
	    printf ("** EXPR %p unreferenced\n", e);
	}
    }
#endif
}
