#ifndef DE_CTYPE_H
#define DE_CTYPE_H
#define isspace de_isspace
#define isdigit de_isdigit
#define isxdigit de_isxdigit
#define isalpha de_isalpha
#define isalnum de_isalnum
#define isupper de_isupper
#define islower de_islower
#define isprint de_isprint
#define toupper de_toupper
#define tolower de_tolower
int isspace(int c);
int isdigit(int c);
int isxdigit(int c);
int isalpha(int c);
int isalnum(int c);
int isupper(int c);
int islower(int c);
int isprint(int c);
int toupper(int c);
int tolower(int c);
#endif
