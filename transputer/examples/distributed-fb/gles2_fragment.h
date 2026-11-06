/* dEngine-style material fragment backend. This is not a GLSL compiler. */
#ifndef T4_GLES2_FRAGMENT_H
#define T4_GLES2_FRAGMENT_H

#define TG_FRAG_BUMP   1u
#define TG_FRAG_SPEC   2u
#define TG_FRAG_SHADOW 4u
#define TG_FRAG_CLAMP  0
#define TG_FRAG_REPEAT 1

/* Native byte layout is RGBA8, row-major. `t=0` addresses row zero; the
 * caller must supply rows in the orientation expected by its texture state. */
typedef struct {
    const unsigned char *rgba;
    unsigned int width, height;
    int wrap_s, wrap_t;
} tg_fragment_texture;

typedef struct {
    float light[3], half_vector[3], normal[3], uv[2];
    float light_clip[4];
} tg_fragment_varyings;

typedef float (*tg_fragment_pow_fn)(float base, float exponent);
typedef struct {
    const tg_fragment_texture *base, *bump, *specular, *shadow;
    float ambient[3], diffuse[3], specular_light[3], specular_material[3];
    float shininess;
    unsigned int features;
    tg_fragment_pow_fn pow_fn;
} tg_fragment_material;

enum { TG_FRAGMENT_OK=0, TG_FRAGMENT_INVALID=1, TG_FRAGMENT_UNDEFINED=2 };

/* Nearest-only texture2D sampling, with CLAMP_TO_EDGE or REPEAT. */
int tg_fragment_sample(const tg_fragment_texture *tex, float s, float t, float rgba[4]);
/* Executes the corresponding f_uber.glsl material formula on interpolated
 * inputs. `pow_fn` must implement GLSL pow for its defined real domain.
 * Returns UNDEFINED for the shader's negative-base/invalid pow or zero shadow
 * divisor; on any error out[] is left unchanged. Outputs are unclamped. */
int tg_fragment_uber(const tg_fragment_material *material,
                     const tg_fragment_varyings *varyings, float out[4]);
/* Clamp to an ARGB8888 framebuffer word after fragment evaluation. */
unsigned int tg_fragment_argb(const float rgba[4]);
#endif
