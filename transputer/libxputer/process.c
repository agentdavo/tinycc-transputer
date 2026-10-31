/*
 *   process.c  (libxputer.a)
 *
 *   October 22, 1996, Roman Pozlevich <roma@botik.ru>
 *
 *   Priority switching.  The other process handling functions 
 *   are implemented as inline ones in xputer.h.
 */


#include <xputer.h>


void process_tolow (void) {
    __asm__ __volatile__ (
	"ldlp  0  \n\t"
        "ldc   1  \n\t"
        "or       \n\t"
        "runp     \n\t"
        "stopp    "
	:
	:
	: "Areg", "Breg", "Creg", "FAreg", "FBreg", "FCreg"
    );
}


#define STARTERSTACKSIZE	0x10

static void starter (channel_t *) __attribute__ ((noreturn));

static void __attribute__((optimize("O0"))) starter (channel_t *trap) {
    volatile channel_t *p = trap;
    channel_t val = *p;
    val = val & ~1u;
    *p = val;
    __chan_outbyte (trap, 0);
    /* NOT REACHED */
}

void process_tohigh (void) {
    channel_t trap = NOTPROCESS;
    int wspace[STARTERSTACKSIZE];
    channel_t *trap_ptr = &trap;
    int one = 1;

    wspace[STARTERSTACKSIZE-1] = (int) &trap;
    process_start (&wspace[STARTERSTACKSIZE-2], &starter);

    __asm__ __volatile__ (
	"alt    \n\t"
        "enbc   \n\t"
        "altwt  "
        : "+b" (trap_ptr), "+a" (one)
        :
	: "Creg", "FAreg", "FBreg", "FCreg", "Wreg[0]"
    );
}
