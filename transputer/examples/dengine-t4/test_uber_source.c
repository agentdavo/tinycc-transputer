#include "../distributed-fb/gles2_program.h"
#include "../distributed-fb/glsl_fragment_compiler.h"
#include "../distributed-fb/glsl_fragment_shader.h"
#include "../distributed-fb/gles2_uber_vertex.h"
#include "../distributed-fb/glsl_uber_vertex_shader.h"
#ifndef __transputer__
#include <math.h>
static float root(float x){return sqrtf(x);}
#else
static float root(float x){return __fpsqrt(x);}
#endif
#include "uber_source.h"
#ifndef TG_TARGET_TEST
#include <stdio.h>
#endif
static tg_context context; static tg_gles_state gl; static tg_gles_program_state programs;
static tg_glsl_fragment_program fragment_program;
static tg_glsl_fragment_shader fragment_shader;
static float square(float x,float y) { (void)y;return x*x; }
static int near(float a,float b) { float d=a-b;return d<0.0001f && d>-0.0001f; }
static int check_vertex(unsigned int mode)
{
    static tg_vm_state vm;static tg_uber_vertex_input in;static tg_uber_vertex_uniforms u;static tg_uber_vertex_output ref,compiled;
    static tg_glsl_uber_vertex_shader vertex_shader;
    int i,j,k;
    if(!tg_glsl_compile_vertex_variant(vertex_source,mode,&fragment_program))return 0;
    tg_glsl_fragment_init(&fragment_program,&vm);vm.sqrt_fn=root;
    in.normal[2]=1;in.tangent[0]=1;in.uv[0]=0.25f;in.uv[1]=0.75f;
    u.light_position[0]=3;u.light_position[2]=4;u.camera_position[2]=5;u.sqrt_fn=root;u.features=mode;
    for(i=0;i<4;i++){u.mvp[i*5]=1;u.light_mvp[i*5]=1;}u.mvp[12]=2;u.light_mvp[13]=3;
#define BIND_VEC(name,src,n) do{k=tg_glsl_find_symbol(&fragment_program,name);if(k<0)return 0;for(j=0;j<(n);j++)vm.reg[fragment_program.symbols[k].slot][j]=(src)[j];}while(0)
    BIND_VEC("a_vertex",in.position,3);BIND_VEC("a_normal",in.normal,3);BIND_VEC("a_texcoord0",in.uv,2);
    if(mode&TG_FRAG_BUMP){BIND_VEC("a_tangent",in.tangent,3);}
    BIND_VEC("lightPosition",u.light_position,3);BIND_VEC("cameraPosition",u.camera_position,3);
#undef BIND_VEC
    k=tg_glsl_find_symbol(&fragment_program,"modelViewProjectionMatrix");if(k<0)return 0;
    for(i=0;i<4;i++)for(j=0;j<4;j++)vm.reg[fragment_program.symbols[k].slot+i][j]=u.mvp[i*4+j];
    if(mode&TG_FRAG_SHADOW){k=tg_glsl_find_symbol(&fragment_program,"lightPOVPVMMatrix");if(k<0)return 0;for(i=0;i<4;i++)for(j=0;j<4;j++)vm.reg[fragment_program.symbols[k].slot+i][j]=u.light_mvp[i*4+j];}
    if(tg_glsl_vm_run_from(&vm,fragment_program.code,fragment_program.instruction_count,fragment_program.entry_pc)!=TG_VM_OK || tg_uber_vertex(&in,&u,&ref)!=TG_FRAGMENT_OK)return 0;
    for(i=0;i<4;i++)if(!near(vm.reg[fragment_program.output_register][i],ref.clip[i]))return 0;
    k=tg_glsl_find_symbol(&fragment_program,"lightVec");if(k<0)return 0;for(i=0;i<3;i++)if(!near(vm.reg[fragment_program.symbols[k].slot][i],ref.varying.light[i]))return 0;
    k=tg_glsl_find_symbol(&fragment_program,"halfVec");if(k<0)return 0;for(i=0;i<3;i++)if(!near(vm.reg[fragment_program.symbols[k].slot][i],ref.varying.half_vector[i]))return 0;
    if(mode&TG_FRAG_SHADOW){k=tg_glsl_find_symbol(&fragment_program,"lightPOVPosition");if(k<0)return 0;for(i=0;i<4;i++)if(!near(vm.reg[fragment_program.symbols[k].slot][i],ref.varying.light_clip[i]))return 0;}
    if(!tg_glsl_uber_vertex_init(&vertex_shader,&fragment_program,mode) || tg_glsl_uber_vertex_run(&vertex_shader,&in,&u,&compiled)!=TG_FRAGMENT_OK)return 0;
    for(i=0;i<4;i++)if(!near(compiled.clip[i],ref.clip[i]) || !near(compiled.varying.light_clip[i],ref.varying.light_clip[i]))return 0;
    for(i=0;i<3;i++)if(!near(compiled.varying.light[i],ref.varying.light[i]) || !near(compiled.varying.half_vector[i],ref.varying.half_vector[i]) || !near(compiled.varying.normal[i],ref.varying.normal[i]))return 0;
    for(i=0;i<2;i++)if(!near(compiled.varying.uv[i],ref.varying.uv[i]))return 0;
    return 1;
}
static int check_fragment(unsigned int mode)
{
    static const unsigned char base_data[]={128,64,32,200},bump_data[]={128,128,255,255},spec_data[]={64,128,255,255};
    static unsigned char shadow_data[]={0,0,0,255};
    static const tg_fragment_texture base={base_data,1,1,0,0},bump={bump_data,1,1,0,0},spec={spec_data,1,1,0,0},shadow={shadow_data,1,1,0,0};
    static const float ambient[]={0.25f,0.25f,0.25f},diffuse[]={0.5f,0.5f,0.5f},light[]={0.2f,0.2f,0.2f},mat[]={0.5f,0.5f,0.5f},shininess[]={2};
    static tg_fragment_varyings varying;static tg_fragment_material material;float actual[4],expected[4];int i;
    if(!tg_glsl_compile_fragment_variant(fragment_source,mode,&fragment_program)) {
#ifndef TG_TARGET_TEST
        printf("compile mode %u failed after %u regs, %u instructions\n",mode,(unsigned)fragment_program.register_count,(unsigned)fragment_program.instruction_count);
#endif
        return 0;
    }
    tg_glsl_shader_init(&fragment_shader,&fragment_program);fragment_shader.vm.pow_fn=square;
    if(!tg_glsl_shader_varying(&fragment_shader,"lightVec",TG_GLSL_INPUT_LIGHT) ||
       !tg_glsl_shader_varying(&fragment_shader,"halfVec",TG_GLSL_INPUT_HALF) ||
       !tg_glsl_shader_varying(&fragment_shader,"v_texcoord",TG_GLSL_INPUT_UV) ||
       ((mode&TG_FRAG_SHADOW) && !tg_glsl_shader_varying(&fragment_shader,"lightPOVPosition",TG_GLSL_INPUT_LIGHT_CLIP)) ||
       (!(mode&TG_FRAG_BUMP) && !tg_glsl_shader_varying(&fragment_shader,"v_normal",TG_GLSL_INPUT_NORMAL)) ||
       !tg_glsl_shader_uniform(&fragment_shader,"lightColorAmbient",ambient,3) ||
       !tg_glsl_shader_uniform(&fragment_shader,"lightColorDiffuse",diffuse,3) ||
       !tg_glsl_shader_uniform(&fragment_shader,"lightColorSpecular",light,3) ||
       !tg_glsl_shader_uniform(&fragment_shader,"matColorSpecular",mat,3) ||
       !tg_glsl_shader_uniform(&fragment_shader,"materialShininess",shininess,1) ||
       !tg_glsl_shader_texture(&fragment_shader,"s_baseMap",&base) ||
       ((mode&TG_FRAG_BUMP) && !tg_glsl_shader_texture(&fragment_shader,"s_bumpMap",&bump)) ||
       ((mode&TG_FRAG_SPEC) && !tg_glsl_shader_texture(&fragment_shader,"s_specularMap",&spec)) ||
       ((mode&TG_FRAG_SHADOW) && !tg_glsl_shader_texture(&fragment_shader,"s_shadowpMap",&shadow)))return 0;
    varying.normal[2]=varying.light[2]=varying.half_vector[2]=varying.light_clip[3]=1;
    material.base=&base;material.bump=&bump;material.specular=&spec;material.shadow=&shadow;material.features=mode;material.pow_fn=square;material.shininess=2;
    for(i=0;i<3;i++){material.ambient[i]=ambient[i];material.diffuse[i]=diffuse[i];material.specular_light[i]=light[i];material.specular_material[i]=mat[i];}
    if(tg_glsl_shader_fragment(&fragment_shader,&varying,actual)!=TG_FRAGMENT_OK || tg_fragment_uber(&material,&varying,expected)!=TG_FRAGMENT_OK)return 0;
    for(i=0;i<4;i++)if(!near(actual[i],expected[i]))return 0;
    if(mode&TG_FRAG_SHADOW) {
        shadow_data[3]=0;
        if(tg_glsl_shader_fragment(&fragment_shader,&varying,actual)!=TG_FRAGMENT_OK || tg_fragment_uber(&material,&varying,expected)!=TG_FRAGMENT_OK)return 0;
        for(i=0;i<4;i++)if(!near(actual[i],expected[i]))return 0;
        shadow_data[3]=255;
    }
    return 1;
}
static int same(const char *a,const char *b) { while(*a && *a==*b) { a++; b++; } return *a==*b; }
static int run(void)
{
    static const char *flags[]={"#define BUMP_MAPPING\n", "#define SPEC_MAPPING\n", "#define SHADO_MAPPING\n"};
    const char *src[4]; char log[128]; GLuint id; GLint ok; int mode,stage,n,i;
    tgGlesInit(&gl,&context); tgGlesMakeCurrent(&gl); tgGlesInitPrograms(&gl,&programs);
    /* The new source compiler must also reject unsupported full uber source. */
    if(tg_glsl_compile_fragment(fragment_source,&fragment_program)) return 2;
    for(mode=0;mode<8;mode++)if(!check_fragment((unsigned int)mode))return 10+mode;
    for(mode=0;mode<8;mode++)if(!check_vertex((unsigned int)mode))return 20+mode;
    for(stage=0;stage<2;stage++) for(mode=0;mode<8;mode++) {
        n=0; for(i=0;i<3;i++)if(mode&(1<<i))src[n++]=flags[i];
        src[n++]=stage?fragment_source:vertex_source;
        id=glCreateShader(stage?GL_FRAGMENT_SHADER:GL_VERTEX_SHADER);
        glShaderSource(id,n,src,0); glCompileShader(id); glGetShaderiv(id,GL_COMPILE_STATUS,&ok);
        glGetShaderInfoLog(id,sizeof(log),0,log);
        /* The real source reaches the parser but must not be reported as
         * compiled while its arithmetic and varyings are unimplemented. */
        if(ok || !same(log,"unsupported GLSL ES shader syntax or operations")) return 1;
        glDeleteShader(id);
    }
    tgGlesMakeCurrent(0); return 0;
}
#ifdef TG_TARGET_TEST
int test_main(void) { volatile unsigned int *u=(volatile unsigned int *)0x10000000u; int n=run(); const char *p=n?"UBER BAD\n":"UBER OK\n"; while(*p)*u=*p++; return n; }
#else
int main(void) { int n=run(); if(!n) puts("dEngine uber preprocessing host tests OK"); else printf("uber test error %d\n",n); return n; }
#endif
