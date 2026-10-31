/*
 *   misc.c  (libxputer.a)
 *
 *   October 22, 1996, Roman Pozlevich <roma@botik.ru>
 *
 *   Miscellaneous transputer features.
 */

#include <xputer.h>


/*
 *  int lddevid (void);
 *  
 *  Short description of `lddevid' instruction:
 *
 *      asm (
 *             ldc 2; ldc 1; ldc 0
 *             lddevid
 *             stl tmp1
 *             stl tmp2
 *      );
 *      
 *      chip_id = tmp1 / 10;  chip_revision = tmp1 % 10;
 *      
 *      switch (tmp2) {
 *        2: T800
 *        1: T212 or T222 or T414
 *        0:
 *           switch (chip_id) {
 *             0: T425
 *             1: T805
 *             2: T801
 *             4: T225
 *             5: T400
 *           }
 *      }
 */

#define CHIP_UNKNOWN	(-3)

int lddevid (void) {
    int t1, t2, id;

    __asm__ __volatile__ (
	"ldc 2   \n\t"
	"ldc 1   \n\t"
	"ldc 0   \n\t"
	"lddevid"
        : "=a" (t1), "=b" (t2)
        :
        : "Creg"
    );

    if (t2 == 0)
	id = t1;
    else
	id = ((t2>0 && t2<=2)? -t2 : CHIP_UNKNOWN);

    return id;
}
