#include "gles2_fragment.h"

/* Bounded coordinates prevent undefined float-to-int conversions on T800.
 * No implicit loop over large repeats and no dependency on the host libm. */
static int coord(float value, unsigned int size, int wrap, unsigned int *out)
{
    int whole;
    if (value!=value || value < -32768.0f || value > 32768.0f) return 0;
    if (wrap==TG_FRAG_CLAMP) {
        if (value<=0.0f) { *out=0; return 1; }
        if (value>=1.0f) { *out=size-1; return 1; }
    } else if (wrap==TG_FRAG_REPEAT) {
        whole=(int)value;
        if ((float)whole>value) --whole;
        value-=(float)whole;
    } else return 0;
    *out=(unsigned int)(value*(float)size);
    if (*out>=size) *out=size-1;
    return 1;
}

int tg_fragment_sample(const tg_fragment_texture *tex, float s, float t, float out[4])
{
    unsigned int x,y,offset; int i;
    if (!tex || !tex->rgba || !out || !tex->width || !tex->height ||
        tex->width>32767u || tex->height>32767u ||
        tex->height>0x3fffffffu/tex->width ||
        !coord(s,tex->width,tex->wrap_s,&x) || !coord(t,tex->height,tex->wrap_t,&y))
        return TG_FRAGMENT_INVALID;
    offset=(y*tex->width+x)*4u;
    for (i=0;i<4;i++) out[i]=(float)tex->rgba[offset+i]/255.0f;
    return TG_FRAGMENT_OK;
}

static float dot3(const float *a,const float *b)
{
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}

int tg_fragment_uber(const tg_fragment_material *m, const tg_fragment_varyings *v, float out[4])
{
    float base[4], bump[4], spec[4], shadow[4], normal[3], rgb[4];
    float lambert, highlight, specbase, shadow_factor=1.0f; int i;
    if (!m || !v || !out || !m->pow_fn || (m->features & ~(TG_FRAG_BUMP|TG_FRAG_SPEC|TG_FRAG_SHADOW)))
        return TG_FRAGMENT_INVALID;
    if (tg_fragment_sample(m->base,v->uv[0],v->uv[1],base)!=TG_FRAGMENT_OK) return TG_FRAGMENT_INVALID;
    if (m->features & TG_FRAG_BUMP) {
        if (tg_fragment_sample(m->bump,v->uv[0],v->uv[1],bump)!=TG_FRAGMENT_OK) return TG_FRAGMENT_INVALID;
        for (i=0;i<3;i++) normal[i]=2.0f*bump[i]-1.0f;
    } else for (i=0;i<3;i++) normal[i]=v->normal[i];
    if (m->features & TG_FRAG_SPEC) {
        if (tg_fragment_sample(m->specular,v->uv[0],v->uv[1],spec)!=TG_FRAGMENT_OK) return TG_FRAGMENT_INVALID;
    } else for (i=0;i<3;i++) spec[i]=m->specular_material[i];
    lambert=dot3(v->light,normal); if (lambert<0.0f) lambert=0.0f;
    specbase=dot3(v->half_vector,normal);
    if (m->clamp_specular_base && specbase<0.0f) specbase=0.0f;
    /* The shader evaluates pow before max, so negative bases are undefined. */
    if (specbase!=specbase || m->shininess!=m->shininess || specbase<0.0f ||
        (specbase==0.0f && m->shininess<=0.0f)) return TG_FRAGMENT_UNDEFINED;
    highlight=m->pow_fn(specbase,m->shininess);
    if (highlight<0.0f) highlight=0.0f;
    if (m->features & TG_FRAG_SHADOW) {
        float w=v->light_clip[3], lx,ly,lz,unpacked;
        if (w==0.0f || w!=w) return TG_FRAGMENT_UNDEFINED;
        lx=(v->light_clip[0]/w+1.0f)*0.5f;
        ly=(v->light_clip[1]/w+1.0f)*0.5f;
        lz=(v->light_clip[2]/w+1.0f)*0.5f;
        if (tg_fragment_sample(m->shadow,lx,ly,shadow)!=TG_FRAGMENT_OK) return TG_FRAGMENT_INVALID;
        unpacked=shadow[0]*(1.0f/16777216.0f)+shadow[1]*(1.0f/65536.0f)+shadow[2]*(1.0f/256.0f)+shadow[3];
        shadow_factor=(unpacked+0.0001f)>lz ? 1.0f : 0.0f;
    }
    for (i=0;i<3;i++)
        rgb[i]=m->ambient[i]*base[i]+(m->diffuse[i]*base[i]*lambert+m->specular_light[i]*spec[i]*highlight)*shadow_factor;
    rgb[3]=base[3];
    for (i=0;i<4;i++) out[i]=rgb[i];
    return TG_FRAGMENT_OK;
}

static unsigned int byte_color(float x)
{
    if (x!=x || x<=0.0f) return 0;
    if (x>=1.0f) return 255;
    return (unsigned int)(x*255.0f+0.5f);
}
unsigned int tg_fragment_argb(const float rgba[4])
{
    return (byte_color(rgba[3])<<24)|(byte_color(rgba[0])<<16)|(byte_color(rgba[1])<<8)|byte_color(rgba[2]);
}
