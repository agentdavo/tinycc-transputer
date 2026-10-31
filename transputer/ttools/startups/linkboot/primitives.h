/* $Id: primitives.h,v 1.2 1996/05/14 10:39:42 sizif Exp $ */

/*
 * This file is part of TTOOLS package.
 * Copyright (c) 1995 Yury Shevchuk <sizif@botik.ru>
 */

extern int boot_link_in;
extern int boot_link_out;


#ifdef DEBUG_LITLOAD
#define lit_load_debug(N) \
  {						        	\
    asm __volatile				        	\
	("outword\n\t"				        	\
         : /* no outputs */			        	\
         : "bP" (boot_link_out),		        	\
           "aP" (N)				        	\
         : "Areg", "Breg", "Creg", "FAreg", "FBreg", "FCreg", "Wreg[0]"); \
  }
#else
#define lit_load_debug(N)  ((void)0)
#endif

/* This is a macro rather than inline function as this avoids
   compiling in any string constants given for ERRSTR */
#define lit_load_fatal(LINE, ERRSTR, ARG) \
  {								\
    asm __volatile				        	\
	("outword\n\t"				        	\
	 "stopp"				        	\
         : /* no outputs */			        	\
         : "bP" (boot_link_out),		        	\
           "aP" (LINE|0xAAAA0000)		        	\
         : "Areg", "Breg", "Creg", "FAreg", "FBreg", "FCreg", "Wreg[0]"); \
  }

static inline int
lit_load_in_byte (void)
{
    int tmp = 0;

    asm __volatile
	("in"
         : /* no outputs */
         : "cP" (&tmp),
           "bP" (boot_link_in),
           "aP" (1)
         : "FAreg", "FBreg", "FCreg", "memory");

    lit_load_debug (__LINE__|0x77770000);
    lit_load_debug (tmp);
    return tmp;
}

static inline void
lit_load_in_block (char *buf, int count)
{
    asm __volatile
	("in"
         : /* no outputs */
         : "cP" (buf),
           "bP" (boot_link_in),
           "aP" (count)
         : "FAreg", "FBreg", "FCreg", "memory");

    lit_load_debug (__LINE__|0x77770000);
    lit_load_debug (count);
}

/* We have to be more restrictive than litload.h, or else
   lit_load_in_expr will require more stack space than we have in
   current crt0.S implementation.  Hope this is overkill anyway: only
   a deep right-biased expression can exhaust this, and those are
   rare.  */

#define MAX_EXPRESSION_DEPTH  (10)
