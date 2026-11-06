#ifndef TG_GLES2_VM_BRIDGE_H
#define TG_GLES2_VM_BRIDGE_H
#include "gles2_program.h"
int tgGlesVmLink(tg_gles_program *);
void tgGlesVmUse(tg_gles_program *,tg_gles_draw_state *);
int tgGlesVmAttrib(tg_gles_program *,const char *);
int tgGlesVmUniformLocation(tg_gles_program *,const char *);
int tgGlesVmUniformMatrix(tg_gles_program *,int,const float *);
int tgGlesVmUniformVector(tg_gles_program *,int,const float *,int);
int tgGlesVmUniformSampler(tg_gles_program *,int,int);
#endif
