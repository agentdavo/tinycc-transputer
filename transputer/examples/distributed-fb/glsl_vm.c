#include "glsl_vm.h"
int tg_glsl_vm_run(tg_vm_state *s,const tg_vm_instruction *code,unsigned int count)
{
    unsigned int pc,i,j; float a[4],b[4],r[4],sum;
    if(!s || (!code && count) || count>TG_VM_MAX_INSTRUCTIONS) return TG_VM_INVALID;
    for(pc=0;pc<count;pc++) {
        const tg_vm_instruction *in=&code[pc];
        if(in->dst>=TG_VM_REGS || in->a>=TG_VM_REGS || in->b>=TG_VM_REGS || !in->width || in->width>4 || (in->flags&~3u)) return TG_VM_INVALID;
        for(i=0;i<4;i++) { a[i]=s->reg[in->a][(in->flags&1)?0:i]; b[i]=s->reg[in->b][(in->flags&2)?0:i]; r[i]=0; }
        switch(in->op) {
        case TG_VM_MOV: for(i=0;i<in->width;i++) r[i]=a[i]; break;
        case TG_VM_ADD: for(i=0;i<in->width;i++) r[i]=a[i]+b[i]; break;
        case TG_VM_SUB: for(i=0;i<in->width;i++) r[i]=a[i]-b[i]; break;
        case TG_VM_MUL: for(i=0;i<in->width;i++) r[i]=a[i]*b[i]; break;
        case TG_VM_DIV: for(i=0;i<in->width;i++) { if(b[i]==0)return TG_VM_UNDEFINED; r[i]=a[i]/b[i]; } break;
        case TG_VM_DOT: for(i=0;i<in->width;i++)r[0]+=a[i]*b[i]; break;
        case TG_VM_CROSS:
            if(in->width!=3)return TG_VM_INVALID;
            r[0]=a[1]*b[2]-a[2]*b[1]; r[1]=a[2]*b[0]-a[0]*b[2]; r[2]=a[0]*b[1]-a[1]*b[0]; break;
        case TG_VM_NORMALIZE:
            if(!s->sqrt_fn)return TG_VM_INVALID;
            sum=0; for(i=0;i<in->width;i++)sum+=a[i]*a[i];
            if(!(sum>0))return TG_VM_UNDEFINED; sum=s->sqrt_fn(sum); if(!(sum>0))return TG_VM_UNDEFINED;
            for(i=0;i<in->width;i++)r[i]=a[i]/sum; break;
        case TG_VM_MAX: for(i=0;i<in->width;i++)r[i]=a[i]>b[i]?a[i]:b[i]; break;
        case TG_VM_POW:
            if(!s->pow_fn)return TG_VM_INVALID;
            for(i=0;i<in->width;i++) { if(a[i]<0 || (a[i]==0 && b[i]<=0))return TG_VM_UNDEFINED; r[i]=s->pow_fn(a[i],b[i]); } break;
        case TG_VM_TEXTURE2D:
            if(in->width!=4 || in->aux>=TG_VM_TEXTURES || tg_fragment_sample(s->texture[in->aux],a[0],a[1],r)!=TG_FRAGMENT_OK) return TG_VM_INVALID;
            break;
        case TG_VM_SWIZZLE: for(i=0;i<in->width;i++)r[i]=a[(in->aux>>(2*i))&3]; break;
        case TG_VM_MAT4_VEC4:
            if(in->width!=4 || in->a>TG_VM_REGS-4 || in->flags)return TG_VM_INVALID;
            for(i=0;i<4;i++) for(j=0;j<4;j++)r[i]+=s->reg[in->a+j][i]*b[j]; break;
        default: return TG_VM_INVALID;
        }
        for(i=0;i<(in->op==TG_VM_DOT?1u:in->width);i++)s->reg[in->dst][i]=r[i];
    }
    return TG_VM_OK;
}
