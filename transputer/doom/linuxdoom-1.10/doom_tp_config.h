// doom_tp_config.h — Transputer compatibility header for DOOM
//
// Injected via -include doom_tp_config.h in the build script.
// Fixes TCC/transputer-specific issues without modifying original source.

#ifndef DOOM_TP_CONFIG_H
#define DOOM_TP_CONFIG_H

// We compile with -DNORMALUNIX but NOT -DLINUX.
// This avoids <values.h> and <alloca.h> includes which don't exist
// in the transputer uClibc-ng headers.

// SNDSERV is defined in doomdef.h — override it to 0 so the
// i_sound_tp.c stub doesn't try to use popen/FILE for sound server.
// We undef it before doomdef.h can set it.
#ifdef SNDSERV
#undef SNDSERV
#endif

// TCC doesn't define __GNUG__ so the #pragma interface/implementation
// guards are already inactive. No fix needed there.

// Provide alloca via TCC builtin (in case any code path needs it)
#ifndef alloca
#define alloca __builtin_alloca
#endif

// DOOM uses malloc.h in w_wad.c under NORMALUNIX.
// uClibc-ng provides malloc in <stdlib.h>. We'll handle this
// by providing the needed prototypes here since <malloc.h> may
// not exist in our include path.

// Fix: DOOM declares 'extern int errno;' in a couple of places.
// uClibc-ng uses errno as a macro (__errno_location()). This is
// handled automatically since we include <errno.h> from libc.

// Ensure we have fixed-width integer limits
#ifndef MAXINT
#define MAXINT      ((int)0x7fffffff)
#endif
#ifndef MININT
#define MININT      ((int)0x80000000)
#endif
#ifndef MAXLONG
#define MAXLONG     ((long)0x7fffffff)
#endif
#ifndef MINLONG
#define MINLONG     ((long)0x80000000)
#endif
#ifndef MAXSHORT
#define MAXSHORT    ((short)0x7fff)
#endif
#ifndef MINSHORT
#define MINSHORT    ((short)0x8000)
#endif
#ifndef MAXCHAR
#define MAXCHAR     ((char)0x7f)
#endif
#ifndef MINCHAR
#define MINCHAR     ((char)0x80)
#endif

// Default WAD directory for TransputerOS package
#ifndef DEFAULT_WADDIR
#define DEFAULT_WADDIR "/usr/share/doom"
#endif

// Workaround: uClibc-ng's <math.h> has __MATHCALLI macros that
// trigger a TCC parser bug on the 'rint' declaration.
// DOOM only uses atan() (in r_main.c) and pow() (in i_sound.c, stubbed).
// Provide the needed prototypes directly and skip <math.h>.
#define _MATH_H
#define __MATH_H
extern double atan(double __x);
extern double pow(double __base, double __exponent);
extern double floor(double __x);
extern double ceil(double __x);
extern double sqrt(double __x);
extern double fabs(double __x);

#endif /* DOOM_TP_CONFIG_H */
