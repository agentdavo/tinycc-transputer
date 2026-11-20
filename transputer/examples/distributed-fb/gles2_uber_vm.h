#ifndef TG_GLES2_UBER_VM_H
#define TG_GLES2_UBER_VM_H
#include "gles2_uber_vertex.h"
/* Hand-translated v_uber instruction sequence; no source compilation. */
int tg_uber_vertex_vm(const tg_uber_vertex_input *,const tg_uber_vertex_uniforms *,tg_uber_vertex_output *);
#endif
