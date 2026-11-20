/* Host-only independent arithmetic oracle; never linked into the guest. */
#include <stdio.h>
#include "softfloat.h"
int main(void) {
 unsigned op,a,b;
 while (scanf("%u %x %x", &op,&a,&b)==3) {
  float32_t x={a},y={b},r;
  softfloat_roundingMode=softfloat_round_near_even;
  softfloat_exceptionFlags=0;
  r=op?f32_mul(x,y):f32_add(x,y);
  printf("%08x %x\n",r.v,softfloat_exceptionFlags);
 }
 return 0;
}
