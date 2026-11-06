#include "gles2_uber_vm.h"
#include "glsl_vm.h"
#define I(op,d,a,b,w,f,x) {op,d,a,b,w,f,x}
/* Registers 0..5 are vertex inputs/light/camera, 6..13 hold matrices.
 * Temporaries 14..22 correspond to the GLSL vertex calculations. */
static const tg_vm_instruction common[]={
    I(TG_VM_MAT4_VEC4,21,6,0,4,0,0), I(TG_VM_MAT4_VEC4,22,10,0,4,0,0),
    I(TG_VM_SUB,14,4,0,3,0,0), I(TG_VM_SUB,15,5,0,3,0,0)
};
static const tg_vm_instruction bumped[]={
    I(TG_VM_CROSS,16,1,2,3,0,0),
    I(TG_VM_DOT,20,14,2,3,0,0), I(TG_VM_SET_COMPONENT,17,20,0,1,0,0),
    I(TG_VM_DOT,20,14,16,3,0,0), I(TG_VM_SET_COMPONENT,17,20,0,1,0,1),
    I(TG_VM_DOT,20,14,1,3,0,0), I(TG_VM_SET_COMPONENT,17,20,0,1,0,2),
    I(TG_VM_DOT,20,15,2,3,0,0), I(TG_VM_SET_COMPONENT,18,20,0,1,0,0),
    I(TG_VM_DOT,20,15,16,3,0,0), I(TG_VM_SET_COMPONENT,18,20,0,1,0,1),
    I(TG_VM_DOT,20,15,1,3,0,0), I(TG_VM_SET_COMPONENT,18,20,0,1,0,2),
    I(TG_VM_NORMALIZE,18,18,0,3,0,0), I(TG_VM_NORMALIZE,17,17,0,3,0,0),
    I(TG_VM_ADD,19,18,17,3,0,0), I(TG_VM_MUL,19,19,23,3,2,0), I(TG_VM_NORMALIZE,19,19,0,3,0,0)
};
static const tg_vm_instruction plain[]={
    I(TG_VM_NORMALIZE,17,14,0,3,0,0), I(TG_VM_ADD,19,15,17,3,0,0),
    I(TG_VM_MUL,19,19,23,3,2,0), I(TG_VM_NORMALIZE,19,19,0,3,0,0)
};
int tg_uber_vertex_vm(const tg_uber_vertex_input *in,const tg_uber_vertex_uniforms *u,tg_uber_vertex_output *out)
{
    static tg_vm_state s; tg_uber_vertex_output result; int i,j,status;
    if(!in || !u || !out || !u->sqrt_fn || (u->features & ~(TG_FRAG_BUMP|TG_FRAG_SPEC|TG_FRAG_SHADOW)))return TG_FRAGMENT_INVALID;
    s.sqrt_fn=u->sqrt_fn;
    for(i=0;i<3;i++) { s.reg[0][i]=in->position[i]; s.reg[1][i]=in->normal[i]; s.reg[2][i]=in->tangent[i]; s.reg[4][i]=u->light_position[i]; s.reg[5][i]=u->camera_position[i]; }
    s.reg[0][3]=1; s.reg[23][0]=0.5f;
    for(i=0;i<4;i++) for(j=0;j<4;j++) { s.reg[6+i][j]=u->mvp[i*4+j]; s.reg[10+i][j]=u->light_mvp[i*4+j]; }
    status=tg_glsl_vm_run(&s,common,sizeof(common)/sizeof(common[0]));
    if(status==TG_VM_OK) {
        if(u->features&TG_FRAG_BUMP)status=tg_glsl_vm_run(&s,bumped,sizeof(bumped)/sizeof(bumped[0]));
        else status=tg_glsl_vm_run(&s,plain,sizeof(plain)/sizeof(plain[0]));
    }
    if(status!=TG_VM_OK)return status==TG_VM_UNDEFINED?TG_FRAGMENT_UNDEFINED:TG_FRAGMENT_INVALID;
    for(i=0;i<4;i++) { result.clip[i]=s.reg[21][i]; result.varying.light_clip[i]=(u->features&TG_FRAG_SHADOW)?s.reg[22][i]:0; }
    for(i=0;i<3;i++) { result.varying.light[i]=s.reg[17][i]; result.varying.half_vector[i]=s.reg[19][i]; result.varying.normal[i]=(u->features&TG_FRAG_BUMP)?0:in->normal[i]; }
    result.varying.uv[0]=in->uv[0]; result.varying.uv[1]=in->uv[1]; *out=result; return TG_FRAGMENT_OK;
}
