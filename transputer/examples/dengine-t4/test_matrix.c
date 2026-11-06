/* Verify the cloned dEngine matrix implementation, without modifying it. */
#include "../dEngine/src/matrix.h"
#ifndef DENGINE_TARGET
#include <stdio.h>
#endif
static int check(void)
{
    matrix_t identity, translate, combined;
    vec3_t p={1.0f,2.0f,3.0f}, out;
    vec4_t v={1.0f,2.0f,3.0f,1.0f}, vout;
    int i;
    matrixLoadIdentity(identity); matrixLoadIdentity(translate);
    translate[12]=2.0f; translate[13]=-3.0f; translate[14]=4.0f;
    matrix_multiply(translate,identity,combined);
    for (i=0;i<16;i++) if (combined[i]!=translate[i]) return 1;
    matrix_transform_vec4t(combined,v,vout);
    if (vout[0]!=3.0f || vout[1]!=-1.0f || vout[2]!=7.0f || vout[3]!=1.0f) return 2;
    matrix_multiplyVertexByMatrix(p,combined,out);
    if (out[0]!=3.0f || out[1]!=-1.0f || out[2]!=7.0f) return 3;
    return 0;
}
#ifdef DENGINE_TARGET
int test_main(void)
{
    volatile unsigned int *uart=(volatile unsigned int *)0x10000000u;
    const char *message=check() ? "DENGINE MATRIX BAD\n" : "DENGINE MATRIX OK\n";
    while (*message) *uart=(unsigned char)*message++;
    return 0;
}
#else
int main(void) { int result=check(); if (!result) puts("dEngine matrix host tests OK"); return result; }
#endif
