/* Experimental source-to-bytecode compiler for a bounded fragment subset. */
#ifndef TG_GLSL_FRAGMENT_COMPILER_H
#define TG_GLSL_FRAGMENT_COMPILER_H
#include "glsl_vm.h"
#define TG_GLSL_SYMBOLS 48
enum { TG_GLSL_UNIFORM=1,TG_GLSL_VARYING=2,TG_GLSL_SAMPLER=3,TG_GLSL_LOCAL=4,TG_GLSL_ATTRIBUTE=5 };
typedef struct { char name[32]; unsigned char kind,width,slot; } tg_glsl_symbol;
typedef struct {
    tg_glsl_symbol symbols[TG_GLSL_SYMBOLS];
    tg_vm_instruction code[TG_VM_MAX_INSTRUCTIONS];
    float initial[TG_VM_REGS][4];
    unsigned char symbol_count,register_count,output_register,entry_pc;
    unsigned short instruction_count;
} tg_glsl_fragment_program;
/* Supports declarations of scalar/vec2/vec3/vec4 uniforms and varyings,
 * sampler2D uniforms, and one gl_FragColor expression using texture2D and
 * component-wise + - * /, swizzles, vector constructors and dot, cross,
 * normalize, max and pow. It rejects all other syntax, rather than silently
 * accepting a shader with different semantics. Not full GLSL ES. */
int tg_glsl_compile_fragment(const char *source,tg_glsl_fragment_program *out);
int tg_glsl_compile_vertex(const char *source,tg_glsl_fragment_program *out);
/* Preprocess the dEngine uber feature branches, then compile. */
int tg_glsl_compile_fragment_variant(const char *source,unsigned int features,tg_glsl_fragment_program *out);
int tg_glsl_compile_vertex_variant(const char *source,unsigned int features,tg_glsl_fragment_program *out);
int tg_glsl_find_symbol(const tg_glsl_fragment_program *,const char *name);
void tg_glsl_fragment_init(const tg_glsl_fragment_program *,tg_vm_state *);
#endif
