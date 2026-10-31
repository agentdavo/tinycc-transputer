/* $Id: litload.h,v 1.1.1.1 1995/12/22 12:24:39 sizif Exp $ */

#ifndef NO_LIBC
#  include "shared.h"
#else /* NO_LIBC */
#  include "cmd.h"
#  include "expr.h"
#  include "litio.h"
#endif /* NO_LIBC */


/* Max length of segment name lit_load_segments() can handle */
#ifndef MAX_SEGMENT_NAME_LENGTH
#define MAX_SEGMENT_NAME_LENGTH  (32)
#endif

/* Max depth of expressions litload can handle */
#ifndef MAX_EXPRESSION_DEPTH
#define MAX_EXPRESSION_DEPTH  (30)
#endif

/* Debugging printout routine: define to nothing if not defined by user */
#ifndef lit_load_debug
#define lit_load_debug(X)  ((void)0)
#endif


/* Set by lit_load_segment: size of the last segment loaded */
extern long lit_load_segsize;

/* Set by lit_load_segment: name of the next segment or "\0" if final
   MARKER reached */
extern char lit_load_segname[];


int lit_load_segment (char *area, int areasz, long *ntab, int ntabsz);

/* prototypes for primitives used by lit_load_segment; these are
   conditionalized in case user chooses to supply them in the form of
   macros */

#ifndef lit_load_in_byte
int lit_load_in_byte (void);
#endif

#ifndef lit_load_in_block
void lit_load_in_block (char *area, int size);
#endif

#ifndef lit_load_fatal
void lit_load_fatal (int line, char *errstr, ...);
#endif

