/* Bounded GLSL-style vector execution primitives. This is an instruction
 * engine, not a GLSL parser or OpenGL ES conformance implementation. */
#ifndef TG_GLSL_VM_H
#define TG_GLSL_VM_H
#include "gles2_fragment.h"
#define TG_VM_REGS 160
#define TG_VM_TEXTURES 8
#define TG_VM_MAX_INSTRUCTIONS 256
enum { TG_VM_MOV=1,TG_VM_ADD,TG_VM_SUB,TG_VM_MUL,TG_VM_DIV,TG_VM_DOT,
       TG_VM_CROSS,TG_VM_NORMALIZE,TG_VM_MAX,TG_VM_POW,TG_VM_TEXTURE2D,
       TG_VM_SWIZZLE,TG_VM_MAT4_VEC4,TG_VM_SET_COMPONENT,TG_VM_GREATER,TG_VM_CALL,TG_VM_RETURN };
typedef struct { unsigned char op,dst,a,b,width,flags,aux; } tg_vm_instruction;
/* flags bit 0/1 broadcasts the first/second source scalar. SWIZZLE aux
 * packs two-bit component indices. MAT4_VEC4 reads four column registers
 * starting at a and multiplies them by vector register b. SET_COMPONENT
 * writes source a.x to destination component aux. */
typedef struct {
    float reg[TG_VM_REGS][4];
    const tg_fragment_texture *texture[TG_VM_TEXTURES];
    float (*sqrt_fn)(float);
    float (*pow_fn)(float,float);
    int clamp_negative_pow; /* Optional policy for GLSL's undefined domain. */
} tg_vm_state;
enum { TG_VM_OK=0,TG_VM_INVALID=1,TG_VM_UNDEFINED=2 };
int tg_glsl_vm_run(tg_vm_state *,const tg_vm_instruction *,unsigned int count);
int tg_glsl_vm_run_from(tg_vm_state *,const tg_vm_instruction *,unsigned int count,unsigned int entry);
#endif
