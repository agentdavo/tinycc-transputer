/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison interface for Yacc-like parsers in C

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

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

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
