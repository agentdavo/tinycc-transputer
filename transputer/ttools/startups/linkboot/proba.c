/* $Id: proba.c,v 1.2 1996/02/25 16:04:17 sizif Exp $ */

extern int boot_link_in;
extern int boot_link_out;

int
main()
{
    static char message[] =
"\n"
"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n"
" G r e e t i n g s   f r o m   r o o t   t r a n s p u t e r       \n"
"%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%\n"
"\n"
"(hit Ctrl-C to get back to shell prompt)\n";

    asm volatile
      ("out"
       : /* no outputs */
       : "cP" (message),
         "bP" (boot_link_out),
         "aP" (sizeof (message) - 1)
       : "FAreg", "FBreg", "FCreg");

    return 0;
}

/* As we link w/o libgcc, define a dummy __main here */
__main()
{
}
