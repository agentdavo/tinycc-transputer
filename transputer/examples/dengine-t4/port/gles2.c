/* Software OpenGL ES 2.0 for the dEngine T4 port.
 *
 * Programs are compiled by the bounded GLSL compiler of distributed-fb
 * (glsl_fragment_compiler.c) after a small preprocessor here, and run by a
 * pre-validated interpreter for its instruction set. Drawing is triangles
 * (lists, strips and fans) with client arrays or buffer objects: vertex
 * shading with an index cache, homogeneous clipping, a scanline rasteriser
 * with perspective-correct varyings, depth test, blending and write masks,
 * into the window or a framebuffer object (RGBA8 texture or colour
 * renderbuffer, float depth renderbuffer).
 *
 * Rasterisation is deterministic per pixel: every value at a pixel depends
 * only on the triangle and the pixel, never on which rows are being drawn,
 * so nodes that each draw some rows of the window produce exactly the image
 * one node would.
 *
 * Not implemented: points and lines (not drawn), stencil testing (no stencil
 * buffer), mipmaps and level-of-detail selection (level 0 is sampled with
 * the magnification filter), cube maps, dithering, polygon offset and
 * multisampling. Depth is a 32-bit float whatever format is requested. */
#include "include/stdlib.h"
#include "include/string.h"
#include "include/stdio.h"
#include "include/math.h"
#include "include/OpenGLES/ES2/glext.h"
#include "gles2_port.h"
#include "pvrtc.h"
#include "../../distributed-fb/glsl_fragment_compiler.h"

#define MAX_ATTRIBS 8
#define MAX_UNITS 8
#define MAX_VARYING_FLOATS 32
#define MAX_UNIFORMS 48
#define MAX_TEXTURE_SIZE 2048

/* ---------------------------------------------------------------- objects */
typedef struct { GLuint name; void *object; } entry;
typedef struct { entry *items; unsigned int count, cap; GLuint next; } table;

static void *table_get(const table *t, GLuint name)
{
    unsigned int i;
    if (!name) return 0;
    for (i = 0; i < t->count; i++) if (t->items[i].name == name) return t->items[i].object;
    return 0;
}
static int table_put(table *t, GLuint name, void *object)
{
    if (t->count == t->cap) {
        unsigned int cap = t->cap ? t->cap * 2 : 16;
        entry *items = realloc(t->items, cap * sizeof(entry));
        if (!items) return 0;
        t->items = items; t->cap = cap;
    }
    t->items[t->count].name = name; t->items[t->count].object = object; t->count++;
    if (name >= t->next) t->next = name + 1;
    return 1;
}
static void table_remove(table *t, GLuint name)
{
    unsigned int i;
    for (i = 0; i < t->count; i++) if (t->items[i].name == name) { t->items[i] = t->items[--t->count]; return; }
}
static GLuint table_fresh(table *t)
{
    if (!t->next) t->next = 1;
    while (table_get(t, t->next)) t->next++;
    return t->next++;
}

typedef struct { unsigned char *data; GLsizeiptr size; GLenum usage; } buffer;

typedef struct {
    const unsigned char *rgba;
    int w, h, linear;
    GLenum wrap_s, wrap_t;
    int wmask, hmask;      /* size - 1 for a power-of-two REPEAT axis, else -1 */
    float fw, fh;
} sampler;

typedef struct {
    int width, height;
    unsigned char *rgba;
    GLenum format, min_filter, mag_filter, wrap_s, wrap_t;
    sampler s;
    int complete;
} texture;

typedef struct { int width, height; GLenum format; unsigned char *rgba; float *depth; } renderbuffer;

typedef struct {
    GLenum color_type; GLuint color;   /* GL_TEXTURE or GL_RENDERBUFFER */
    GLuint depth;                      /* renderbuffer */
    GLuint stencil;
    /* deGlDistributeFramebuffer: the rows this context draws, and the
     * platform's gather, called when the framebuffer is unbound dirty. */
    unsigned char *owned;
    int owned_rows, band_rows, dirty;
    de_gl_gather gather;
} framebuffer;

typedef struct {
    GLenum type;
    char *source;
    char *log;
    GLboolean compiled, deleted;
    tg_glsl_fragment_program *ir;
    int attached;
} shader;

typedef struct { char name[32]; int width, vreg, freg, vsampler, fsampler, unit; } uniform_info;
typedef struct { char name[32]; int width, reg, location; } attrib_info;

/* Linked code for the interpreter: compiler instructions decoded into scalar
 * operations on pointers into the register file (see decode_program). */
typedef struct { int op; float *d, *a, *b; int aux; } fop;
typedef struct { fop *ops; int count, cap; float scratch[4]; } fast_code;

typedef struct {
    GLuint vs_name, fs_name;
    GLboolean linked, deleted, validated;
    char *log;
    struct { char name[32]; GLuint index; } bindings[MAX_ATTRIBS];
    int binding_count;
    /* Linked executable. */
    tg_glsl_fragment_program *vs, *fs;
    float (*vregs)[4], (*fregs)[4];
    int vs_out, fs_out;
    attrib_info attribs[MAX_ATTRIBS];
    int attrib_count;
    uniform_info uniforms[MAX_UNIFORMS];
    int uniform_count;
    int varying_floats;
    unsigned char vary_vreg[MAX_VARYING_FLOATS], vary_freg[MAX_VARYING_FLOATS], vary_comp[MAX_VARYING_FLOATS];
    int vsamplers, fsamplers;
    fast_code vfast, ffast;
    /* Only what gl_Position needs, and whether running it first pays. */
    fast_code vpos;
    int vpos_split;
} program;

typedef struct {
    GLboolean enabled, normalized;
    GLint size;
    GLenum type;
    GLsizei stride;
    const void *pointer;
    GLuint buffer;
} attrib_array;

/* Where fragments go. Rows are bottom first; `owned` limits the window. */
typedef struct {
    int width, height;
    unsigned char *color;
    float *depth;
    const unsigned char *owned;
    int valid;
} target;

static struct {
    int ready;
    GLenum error;
    table buffers, textures, renderbuffers, framebuffers, shaders, programs;
    GLuint array_buffer, element_buffer, renderbuffer_binding, framebuffer_binding, program;
    GLuint unit_texture[MAX_UNITS];
    GLuint active_unit;
    attrib_array arrays[MAX_ATTRIBS];
    float generic[MAX_ATTRIBS][4];
    GLint viewport[4], scissor[4];
    float depth_near, depth_far;
    GLboolean depth_test, cull, blend, scissor_test, dither, stencil_test, polygon_offset, sample_coverage, sample_alpha;
    GLenum cull_face, front_face, depth_func;
    GLenum blend_src_rgb, blend_dst_rgb, blend_src_alpha, blend_dst_alpha, blend_eq_rgb, blend_eq_alpha;
    float blend_color[4];
    GLboolean color_mask[4], depth_mask;
    float clear_color[4], clear_depth;
    GLint clear_stencil;
    GLint unpack_alignment, pack_alignment;
    float line_width, offset_factor, offset_units, coverage_value;
    GLboolean coverage_invert;
    GLenum mipmap_hint;
    GLenum stencil_func[2], stencil_fail[2], stencil_zfail[2], stencil_zpass[2];
    GLint stencil_ref[2];
    GLuint stencil_value_mask[2], stencil_write_mask[2];
    /* Default framebuffer. */
    int window_width, window_height;
    unsigned char *window_color;
    float *window_depth;
    const unsigned char *window_owned;
    de_gl_stats stats;
} gl;

static void set_error(GLenum e) { if (gl.error == GL_NO_ERROR) gl.error = e; }
GLenum glGetError(void) { GLenum e = gl.error; gl.error = GL_NO_ERROR; return e; }

static texture black_texture_object;
static const unsigned char black_texel[4] = {0, 0, 0, 255};
static float byte_to_float[256];

void deGlInit(void)
{
    int i;
    if (gl.ready) return;
    memset(&gl, 0, sizeof(gl));
    gl.ready = 1;
    gl.depth_near = 0.0f; gl.depth_far = 1.0f;
    gl.cull_face = GL_BACK; gl.front_face = GL_CCW; gl.depth_func = GL_LESS;
    gl.blend_src_rgb = gl.blend_src_alpha = GL_ONE; gl.blend_dst_rgb = gl.blend_dst_alpha = GL_ZERO;
    gl.blend_eq_rgb = gl.blend_eq_alpha = GL_FUNC_ADD;
    for (i = 0; i < 4; i++) gl.color_mask[i] = GL_TRUE;
    gl.depth_mask = GL_TRUE; gl.clear_depth = 1.0f; gl.dither = GL_TRUE;
    gl.unpack_alignment = gl.pack_alignment = 4;
    gl.line_width = 1.0f; gl.coverage_value = 1.0f; gl.mipmap_hint = GL_DONT_CARE;
    for (i = 0; i < 2; i++) {
        gl.stencil_func[i] = GL_ALWAYS; gl.stencil_fail[i] = gl.stencil_zfail[i] = gl.stencil_zpass[i] = GL_KEEP;
        gl.stencil_value_mask[i] = gl.stencil_write_mask[i] = 0xffffffffu;
    }
    for (i = 0; i < MAX_ATTRIBS; i++) { gl.generic[i][3] = 1.0f; gl.arrays[i].size = 4; gl.arrays[i].type = GL_FLOAT; }
    for (i = 0; i < 256; i++) byte_to_float[i] = (float)i / 255.0f;
    black_texture_object.width = black_texture_object.height = 1;
    black_texture_object.s.rgba = black_texel; black_texture_object.s.w = black_texture_object.s.h = 1;
    black_texture_object.s.wrap_s = black_texture_object.s.wrap_t = GL_REPEAT;
    black_texture_object.s.wmask = black_texture_object.s.hmask = 0;
    black_texture_object.s.fw = black_texture_object.s.fh = 1.0f;
}

void deGlSetWindow(int width, int height, unsigned char *color, float *depth, const unsigned char *owned)
{
    deGlInit();
    gl.window_width = width; gl.window_height = height;
    gl.window_color = color; gl.window_depth = depth; gl.window_owned = owned;
    if (!gl.viewport[2]) { gl.viewport[2] = gl.scissor[2] = width; gl.viewport[3] = gl.scissor[3] = height; }
}

void deGlStats(de_gl_stats *out, int reset)
{
    if (out) *out = gl.stats;
    if (reset) memset(&gl.stats, 0, sizeof(gl.stats));
}

/* ------------------------------------------------------------ simple state */
static GLboolean *capability(GLenum cap)
{
    switch (cap) {
    case GL_DEPTH_TEST: return &gl.depth_test;
    case GL_CULL_FACE: return &gl.cull;
    case GL_BLEND: return &gl.blend;
    case GL_SCISSOR_TEST: return &gl.scissor_test;
    case GL_DITHER: return &gl.dither;
    case GL_STENCIL_TEST: return &gl.stencil_test;
    case GL_POLYGON_OFFSET_FILL: return &gl.polygon_offset;
    case GL_SAMPLE_COVERAGE: return &gl.sample_coverage;
    case GL_SAMPLE_ALPHA_TO_COVERAGE: return &gl.sample_alpha;
    default: return 0;
    }
}
void glEnable(GLenum cap) { GLboolean *p = capability(cap); if (p) *p = GL_TRUE; else set_error(GL_INVALID_ENUM); }
void glDisable(GLenum cap) { GLboolean *p = capability(cap); if (p) *p = GL_FALSE; else set_error(GL_INVALID_ENUM); }
GLboolean glIsEnabled(GLenum cap) { GLboolean *p = capability(cap); if (p) return *p; set_error(GL_INVALID_ENUM); return GL_FALSE; }

static float clampf01(float v) { return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; }
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{ gl.clear_color[0] = clampf01(r); gl.clear_color[1] = clampf01(g); gl.clear_color[2] = clampf01(b); gl.clear_color[3] = clampf01(a); }
void glClearDepthf(GLclampf d) { gl.clear_depth = clampf01(d); }
void glClearStencil(GLint s) { gl.clear_stencil = s; }
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { gl.color_mask[0] = r; gl.color_mask[1] = g; gl.color_mask[2] = b; gl.color_mask[3] = a; }
void glDepthMask(GLboolean flag) { gl.depth_mask = flag; }
void glDepthRangef(GLclampf n, GLclampf f) { gl.depth_near = clampf01(n); gl.depth_far = clampf01(f); }
static int compare_func(GLenum f) { return f >= GL_NEVER && f <= GL_ALWAYS; }
void glDepthFunc(GLenum func) { if (compare_func(func)) gl.depth_func = func; else set_error(GL_INVALID_ENUM); }
void glCullFace(GLenum mode)
{ if (mode == GL_FRONT || mode == GL_BACK || mode == GL_FRONT_AND_BACK) gl.cull_face = mode; else set_error(GL_INVALID_ENUM); }
void glFrontFace(GLenum mode) { if (mode == GL_CW || mode == GL_CCW) gl.front_face = mode; else set_error(GL_INVALID_ENUM); }
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    if (w < 0 || h < 0) { set_error(GL_INVALID_VALUE); return; }
    if (w > MAX_TEXTURE_SIZE) w = MAX_TEXTURE_SIZE;
    if (h > MAX_TEXTURE_SIZE) h = MAX_TEXTURE_SIZE;
    gl.viewport[0] = x; gl.viewport[1] = y; gl.viewport[2] = w; gl.viewport[3] = h;
}
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    if (w < 0 || h < 0) { set_error(GL_INVALID_VALUE); return; }
    gl.scissor[0] = x; gl.scissor[1] = y; gl.scissor[2] = w; gl.scissor[3] = h;
}
static int blend_factor(GLenum f, int is_dst)
{
    switch (f) {
    case GL_ZERO: case GL_ONE: case GL_SRC_COLOR: case GL_ONE_MINUS_SRC_COLOR: case GL_DST_COLOR:
    case GL_ONE_MINUS_DST_COLOR: case GL_SRC_ALPHA: case GL_ONE_MINUS_SRC_ALPHA: case GL_DST_ALPHA:
    case GL_ONE_MINUS_DST_ALPHA: case GL_CONSTANT_COLOR: case GL_ONE_MINUS_CONSTANT_COLOR:
    case GL_CONSTANT_ALPHA: case GL_ONE_MINUS_CONSTANT_ALPHA: return 1;
    case GL_SRC_ALPHA_SATURATE: return !is_dst;
    default: return 0;
    }
}
void glBlendFuncSeparate(GLenum sr, GLenum dr, GLenum sa, GLenum da)
{
    if (!blend_factor(sr, 0) || !blend_factor(dr, 1) || !blend_factor(sa, 0) || !blend_factor(da, 1)) { set_error(GL_INVALID_ENUM); return; }
    gl.blend_src_rgb = sr; gl.blend_dst_rgb = dr; gl.blend_src_alpha = sa; gl.blend_dst_alpha = da;
}
void glBlendFunc(GLenum s, GLenum d) { glBlendFuncSeparate(s, d, s, d); }
static int blend_equation(GLenum e) { return e == GL_FUNC_ADD || e == GL_FUNC_SUBTRACT || e == GL_FUNC_REVERSE_SUBTRACT; }
void glBlendEquationSeparate(GLenum rgb, GLenum alpha)
{
    if (!blend_equation(rgb) || !blend_equation(alpha)) { set_error(GL_INVALID_ENUM); return; }
    gl.blend_eq_rgb = rgb; gl.blend_eq_alpha = alpha;
}
void glBlendEquation(GLenum mode) { glBlendEquationSeparate(mode, mode); }
void glBlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{ gl.blend_color[0] = clampf01(r); gl.blend_color[1] = clampf01(g); gl.blend_color[2] = clampf01(b); gl.blend_color[3] = clampf01(a); }
void glLineWidth(GLfloat w) { if (w <= 0.0f) set_error(GL_INVALID_VALUE); else gl.line_width = w; }
void glPolygonOffset(GLfloat factor, GLfloat units) { gl.offset_factor = factor; gl.offset_units = units; }
void glSampleCoverage(GLclampf value, GLboolean invert) { gl.coverage_value = clampf01(value); gl.coverage_invert = invert; }
void glHint(GLenum target, GLenum mode)
{
    if (target != GL_GENERATE_MIPMAP_HINT) { set_error(GL_INVALID_ENUM); return; }
    if (mode != GL_DONT_CARE && mode != GL_FASTEST && mode != GL_NICEST) { set_error(GL_INVALID_ENUM); return; }
    gl.mipmap_hint = mode;
}
void glPixelStorei(GLenum pname, GLint param)
{
    if (param != 1 && param != 2 && param != 4 && param != 8) { set_error(GL_INVALID_VALUE); return; }
    if (pname == GL_UNPACK_ALIGNMENT) gl.unpack_alignment = param;
    else if (pname == GL_PACK_ALIGNMENT) gl.pack_alignment = param;
    else set_error(GL_INVALID_ENUM);
}
static void stencil_faces(GLenum face, int *f0, int *f1)
{
    *f0 = face == GL_FRONT || face == GL_FRONT_AND_BACK ? 1 : 0;
    *f1 = face == GL_BACK || face == GL_FRONT_AND_BACK ? 1 : 0;
}
void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask)
{
    int f, b;
    if (!compare_func(func)) { set_error(GL_INVALID_ENUM); return; }
    stencil_faces(face, &f, &b);
    if (!f && !b) { set_error(GL_INVALID_ENUM); return; }
    if (f) { gl.stencil_func[0] = func; gl.stencil_ref[0] = ref; gl.stencil_value_mask[0] = mask; }
    if (b) { gl.stencil_func[1] = func; gl.stencil_ref[1] = ref; gl.stencil_value_mask[1] = mask; }
}
void glStencilFunc(GLenum func, GLint ref, GLuint mask) { glStencilFuncSeparate(GL_FRONT_AND_BACK, func, ref, mask); }
void glStencilMaskSeparate(GLenum face, GLuint mask)
{
    int f, b;
    stencil_faces(face, &f, &b);
    if (!f && !b) { set_error(GL_INVALID_ENUM); return; }
    if (f) gl.stencil_write_mask[0] = mask;
    if (b) gl.stencil_write_mask[1] = mask;
}
void glStencilMask(GLuint mask) { glStencilMaskSeparate(GL_FRONT_AND_BACK, mask); }
void glStencilOpSeparate(GLenum face, GLenum fail, GLenum zfail, GLenum zpass)
{
    int f, b;
    stencil_faces(face, &f, &b);
    if (!f && !b) { set_error(GL_INVALID_ENUM); return; }
    if (f) { gl.stencil_fail[0] = fail; gl.stencil_zfail[0] = zfail; gl.stencil_zpass[0] = zpass; }
    if (b) { gl.stencil_fail[1] = fail; gl.stencil_zfail[1] = zfail; gl.stencil_zpass[1] = zpass; }
}
void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) { glStencilOpSeparate(GL_FRONT_AND_BACK, fail, zfail, zpass); }
void glFinish(void) { }
void glFlush(void) { }
void glReleaseShaderCompiler(void) { }

const GLubyte *glGetString(GLenum name)
{
    switch (name) {
    case GL_VENDOR: return (const GLubyte *)"tinycc-transputer";
    case GL_RENDERER: return (const GLubyte *)"T800 software rasteriser";
    case GL_VERSION: return (const GLubyte *)"OpenGL ES 2.0 (subset)";
    case GL_SHADING_LANGUAGE_VERSION: return (const GLubyte *)"OpenGL ES GLSL ES 1.00 (subset)";
    case GL_EXTENSIONS: return (const GLubyte *)"GL_IMG_texture_compression_pvrtc GL_OES_element_index_uint";
    default: set_error(GL_INVALID_ENUM); return 0;
    }
}

/* ---------------------------------------------------------------- buffers */
void glGenBuffers(GLsizei n, GLuint *names)
{
    GLsizei i;
    if (n < 0) { set_error(GL_INVALID_VALUE); return; }
    for (i = 0; i < n; i++) {
        buffer *b = calloc(1, sizeof(buffer));
        names[i] = table_fresh(&gl.buffers);
        if (!b || !table_put(&gl.buffers, names[i], b)) { set_error(GL_OUT_OF_MEMORY); return; }
    }
}
static buffer *buffer_named(GLuint name, int create)
{
    buffer *b = table_get(&gl.buffers, name);
    if (!b && name && create) {
        b = calloc(1, sizeof(buffer));
        if (!b || !table_put(&gl.buffers, name, b)) { set_error(GL_OUT_OF_MEMORY); return 0; }
    }
    return b;
}
void glBindBuffer(GLenum target, GLuint name)
{
    if (target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER) { set_error(GL_INVALID_ENUM); return; }
    if (name) buffer_named(name, 1);
    if (target == GL_ARRAY_BUFFER) gl.array_buffer = name; else gl.element_buffer = name;
}
static buffer *bound_buffer(GLenum target)
{
    GLuint name = target == GL_ARRAY_BUFFER ? gl.array_buffer : target == GL_ELEMENT_ARRAY_BUFFER ? gl.element_buffer : 0;
    if (target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER) { set_error(GL_INVALID_ENUM); return 0; }
    if (!name) { set_error(GL_INVALID_OPERATION); return 0; }
    return buffer_named(name, 0);
}
void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage)
{
    buffer *b;
    unsigned char *store;
    if (usage != GL_STREAM_DRAW && usage != GL_STATIC_DRAW && usage != GL_DYNAMIC_DRAW) { set_error(GL_INVALID_ENUM); return; }
    if (size < 0) { set_error(GL_INVALID_VALUE); return; }
    if (!(b = bound_buffer(target))) return;
    store = malloc(size ? (size_t)size : 1u);
    if (!store) { set_error(GL_OUT_OF_MEMORY); return; }
    if (data) memcpy(store, data, (size_t)size); else memset(store, 0, (size_t)size);
    free(b->data); b->data = store; b->size = size; b->usage = usage;
}
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data)
{
    buffer *b = bound_buffer(target);
    if (!b) return;
    if (offset < 0 || size < 0 || offset + size > b->size) { set_error(GL_INVALID_VALUE); return; }
    memcpy(b->data + offset, data, (size_t)size);
}
void glDeleteBuffers(GLsizei n, const GLuint *names)
{
    GLsizei i;
    int k;
    if (n < 0) { set_error(GL_INVALID_VALUE); return; }
    for (i = 0; i < n; i++) {
        buffer *b = table_get(&gl.buffers, names[i]);
        if (!b) continue;
        if (gl.array_buffer == names[i]) gl.array_buffer = 0;
        if (gl.element_buffer == names[i]) gl.element_buffer = 0;
        for (k = 0; k < MAX_ATTRIBS; k++) if (gl.arrays[k].buffer == names[i]) gl.arrays[k].buffer = 0;
        free(b->data); free(b);
        table_remove(&gl.buffers, names[i]);
    }
}
GLboolean glIsBuffer(GLuint name) { return table_get(&gl.buffers, name) != 0; }
void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
    buffer *b = bound_buffer(target);
    if (!b) return;
    if (pname == GL_BUFFER_SIZE) *params = (GLint)b->size;
    else if (pname == GL_BUFFER_USAGE) *params = (GLint)(b->usage ? b->usage : GL_STATIC_DRAW);
    else set_error(GL_INVALID_ENUM);
}

/* --------------------------------------------------------------- textures */
static int power_of_two(int v) { return v > 0 && !(v & (v - 1)); }
static void texture_update(texture *t)
{
    GLenum filter = t->mag_filter;
    t->complete = t->rgba && t->width > 0 && t->height > 0;
    /* ES 2.0: a non-power-of-two texture must clamp and not mipmap. */
    if ((!power_of_two(t->width) || !power_of_two(t->height)) &&
        (t->wrap_s != GL_CLAMP_TO_EDGE || t->wrap_t != GL_CLAMP_TO_EDGE ||
         (t->min_filter != GL_NEAREST && t->min_filter != GL_LINEAR))) t->complete = 0;
    t->s.rgba = t->rgba; t->s.w = t->width; t->s.h = t->height;
    t->s.wrap_s = t->wrap_s; t->s.wrap_t = t->wrap_t;
    t->s.linear = filter == GL_LINEAR;
    t->s.fw = (float)t->width; t->s.fh = (float)t->height;
    t->s.wmask = t->wrap_s == GL_REPEAT && power_of_two(t->width) ? t->width - 1 : -1;
    t->s.hmask = t->wrap_t == GL_REPEAT && power_of_two(t->height) ? t->height - 1 : -1;
}
static texture *texture_named(GLuint name, int create)
{
    texture *t = table_get(&gl.textures, name);
    if (!t && name && create) {
        t = calloc(1, sizeof(texture));
        if (!t || !table_put(&gl.textures, name, t)) { set_error(GL_OUT_OF_MEMORY); free(t); return 0; }
        t->min_filter = GL_NEAREST_MIPMAP_LINEAR; t->mag_filter = GL_LINEAR;
        t->wrap_s = t->wrap_t = GL_REPEAT;
        texture_update(t);
    }
    return t;
}
void glGenTextures(GLsizei n, GLuint *names)
{
    GLsizei i;
    if (n < 0) { set_error(GL_INVALID_VALUE); return; }
    for (i = 0; i < n; i++) { names[i] = table_fresh(&gl.textures); texture_named(names[i], 1); }
}
void glActiveTexture(GLenum unit)
{
    if (unit < GL_TEXTURE0 || unit >= GL_TEXTURE0 + MAX_UNITS) { set_error(GL_INVALID_ENUM); return; }
    gl.active_unit = unit - GL_TEXTURE0;
}
void glBindTexture(GLenum target, GLuint name)
{
    if (target != GL_TEXTURE_2D) { set_error(target == GL_TEXTURE_CUBE_MAP ? GL_INVALID_OPERATION : GL_INVALID_ENUM); return; }
    if (name && !texture_named(name, 1)) return;
    gl.unit_texture[gl.active_unit] = name;
}
static texture *bound_texture(GLenum target)
{
    texture *t;
    if (target != GL_TEXTURE_2D) { set_error(GL_INVALID_ENUM); return 0; }
    t = texture_named(gl.unit_texture[gl.active_unit], 0);
    if (!t) set_error(GL_INVALID_OPERATION);   /* texture object 0 is not modifiable here */
    return t;
}
static void tex_parameter(GLenum target, GLenum pname, GLint v)
{
    texture *t = bound_texture(target);
    if (!t) return;
    switch (pname) {
    case GL_TEXTURE_MIN_FILTER:
        if (v != GL_NEAREST && v != GL_LINEAR && v != GL_NEAREST_MIPMAP_NEAREST && v != GL_LINEAR_MIPMAP_NEAREST &&
            v != GL_NEAREST_MIPMAP_LINEAR && v != GL_LINEAR_MIPMAP_LINEAR) { set_error(GL_INVALID_ENUM); return; }
        t->min_filter = (GLenum)v; break;
    case GL_TEXTURE_MAG_FILTER:
        if (v != GL_NEAREST && v != GL_LINEAR) { set_error(GL_INVALID_ENUM); return; }
        t->mag_filter = (GLenum)v; break;
    case GL_TEXTURE_WRAP_S: case GL_TEXTURE_WRAP_T:
        if (v != GL_REPEAT && v != GL_CLAMP_TO_EDGE && v != GL_MIRRORED_REPEAT) { set_error(GL_INVALID_ENUM); return; }
        if (pname == GL_TEXTURE_WRAP_S) t->wrap_s = (GLenum)v; else t->wrap_t = (GLenum)v;
        break;
    default: set_error(GL_INVALID_ENUM); return;
    }
    texture_update(t);
}
void glTexParameteri(GLenum target, GLenum pname, GLint param) { tex_parameter(target, pname, param); }
void glTexParameterf(GLenum target, GLenum pname, GLfloat param) { tex_parameter(target, pname, (GLint)param); }
void glTexParameteriv(GLenum target, GLenum pname, const GLint *params) { tex_parameter(target, pname, params[0]); }
void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) { tex_parameter(target, pname, (GLint)params[0]); }
void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params)
{
    texture *t = bound_texture(target);
    if (!t) return;
    switch (pname) {
    case GL_TEXTURE_MIN_FILTER: *params = (GLint)t->min_filter; break;
    case GL_TEXTURE_MAG_FILTER: *params = (GLint)t->mag_filter; break;
    case GL_TEXTURE_WRAP_S: *params = (GLint)t->wrap_s; break;
    case GL_TEXTURE_WRAP_T: *params = (GLint)t->wrap_t; break;
    default: set_error(GL_INVALID_ENUM);
    }
}
void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params)
{ GLint v = 0; glGetTexParameteriv(target, pname, &v); *params = (GLfloat)v; }

static int pixel_bytes(GLenum format, GLenum type)
{
    if (type == GL_UNSIGNED_BYTE) {
        switch (format) {
        case GL_RGBA: return 4; case GL_RGB: return 3; case GL_LUMINANCE_ALPHA: return 2;
        case GL_LUMINANCE: case GL_ALPHA: return 1; default: return 0;
        }
    }
    if (type == GL_UNSIGNED_SHORT_4_4_4_4 || type == GL_UNSIGNED_SHORT_5_5_5_1) return format == GL_RGBA ? 2 : 0;
    if (type == GL_UNSIGNED_SHORT_5_6_5) return format == GL_RGB ? 2 : 0;
    return -1;
}
static unsigned char expand(unsigned int v, int bits) { return (unsigned char)((v * 255u + ((1u << bits) - 1u) / 2u) / ((1u << bits) - 1u)); }
static void unpack_pixel(const unsigned char *p, GLenum format, GLenum type, unsigned char *o)
{
    if (type == GL_UNSIGNED_BYTE) {
        switch (format) {
        case GL_RGBA: o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = p[3]; return;
        case GL_RGB: o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = 255; return;
        case GL_LUMINANCE_ALPHA: o[0] = o[1] = o[2] = p[0]; o[3] = p[1]; return;
        case GL_LUMINANCE: o[0] = o[1] = o[2] = p[0]; o[3] = 255; return;
        default: o[0] = o[1] = o[2] = 0; o[3] = p[0]; return;
        }
    } else {
        unsigned int v = (unsigned int)p[0] | (unsigned int)p[1] << 8;
        if (type == GL_UNSIGNED_SHORT_4_4_4_4) { o[0] = expand(v >> 12, 4); o[1] = expand((v >> 8) & 15u, 4); o[2] = expand((v >> 4) & 15u, 4); o[3] = expand(v & 15u, 4); }
        else if (type == GL_UNSIGNED_SHORT_5_5_5_1) { o[0] = expand(v >> 11, 5); o[1] = expand((v >> 6) & 31u, 5); o[2] = expand((v >> 1) & 31u, 5); o[3] = (v & 1u) ? 255 : 0; }
        else { o[0] = expand(v >> 11, 5); o[1] = expand((v >> 5) & 63u, 6); o[2] = expand(v & 31u, 5); o[3] = 255; }
    }
}
static void copy_pixels(unsigned char *dst, int dst_width, int x0, int y0, int w, int h, GLenum format, GLenum type, const unsigned char *src)
{
    int bytes = pixel_bytes(format, type), x, y;
    size_t row = (size_t)w * (size_t)bytes;
    row = (row + (size_t)gl.unpack_alignment - 1u) & ~(size_t)(gl.unpack_alignment - 1);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            unpack_pixel(src + (size_t)y * row + (size_t)x * (size_t)bytes, format, type, dst + (((size_t)(y0 + y) * (size_t)dst_width + (size_t)(x0 + x)) * 4u));
}
static int texture_target_level(GLenum target, GLint level)
{
    if (target != GL_TEXTURE_2D) { set_error(GL_INVALID_ENUM); return 0; }
    if (level < 0) { set_error(GL_INVALID_VALUE); return 0; }
    return 1;
}
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    texture *t;
    unsigned char *store;
    int bytes;
    if (!texture_target_level(target, level)) return;
    bytes = pixel_bytes(format, type);
    if (bytes < 0) { set_error(GL_INVALID_ENUM); return; }
    if (!bytes || (GLenum)internalformat != format) { set_error(bytes ? GL_INVALID_OPERATION : GL_INVALID_ENUM); return; }
    if (w < 0 || h < 0 || w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE || border) { set_error(GL_INVALID_VALUE); return; }
    if (!(t = bound_texture(target))) return;
    if (level > 0) return;   /* mipmap levels are accepted and not sampled */
    store = malloc((size_t)(w ? w : 1) * (size_t)(h ? h : 1) * 4u);
    if (!store) { set_error(GL_OUT_OF_MEMORY); return; }
    if (pixels) copy_pixels(store, w, 0, 0, w, h, format, type, pixels);
    else memset(store, 0, (size_t)w * (size_t)h * 4u);
    free(t->rgba);
    t->rgba = store; t->width = w; t->height = h; t->format = format;
    texture_update(t);
}
void glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const GLvoid *pixels)
{
    texture *t;
    int bytes;
    if (!texture_target_level(target, level)) return;
    bytes = pixel_bytes(format, type);
    if (bytes <= 0) { set_error(GL_INVALID_ENUM); return; }
    if (!(t = bound_texture(target))) return;
    if (level > 0) return;
    if (x < 0 || y < 0 || w < 0 || h < 0 || x + w > t->width || y + h > t->height) { set_error(GL_INVALID_VALUE); return; }
    if (format != t->format) { set_error(GL_INVALID_OPERATION); return; }
    copy_pixels(t->rgba, t->width, x, y, w, h, format, type, pixels);
}
void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei w, GLsizei h, GLint border, GLsizei size, const GLvoid *data)
{
    texture *t;
    unsigned char *store;
    int two_bpp, opaque;
    if (!texture_target_level(target, level)) return;
    if (internalformat < GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG || internalformat > GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG) { set_error(GL_INVALID_ENUM); return; }
    two_bpp = internalformat == GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG || internalformat == GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG;
    opaque = internalformat == GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG || internalformat == GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG;
    if (w <= 0 || h <= 0 || w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE || border || !power_of_two(w) || !power_of_two(h) ||
        size != (GLsizei)de_pvrtc_size((unsigned)w, (unsigned)h, two_bpp)) { set_error(GL_INVALID_VALUE); return; }
    if (!(t = bound_texture(target))) return;
    if (level > 0) return;
    store = malloc((size_t)w * (size_t)h * 4u);
    if (!store || !de_pvrtc_decode(data, (unsigned)w, (unsigned)h, two_bpp, opaque, store)) { free(store); set_error(GL_OUT_OF_MEMORY); return; }
    free(t->rgba);
    t->rgba = store; t->width = w; t->height = h; t->format = opaque ? GL_RGB : GL_RGBA;
    texture_update(t);
}
void glCompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLsizei size, const GLvoid *data)
{
    (void)level; (void)x; (void)y; (void)w; (void)h; (void)format; (void)size; (void)data;
    if (target != GL_TEXTURE_2D) set_error(GL_INVALID_ENUM);
    else set_error(GL_INVALID_OPERATION);   /* PVRTC allows no sub-image updates */
}
void glGenerateMipmap(GLenum target)
{
    texture *t = bound_texture(target);
    if (t && (!power_of_two(t->width) || !power_of_two(t->height))) set_error(GL_INVALID_OPERATION);
    /* Level 0 only is ever sampled, so there is nothing to build. */
}
void glDeleteTextures(GLsizei n, const GLuint *names)
{
    GLsizei i;
    unsigned int u;
    if (n < 0) { set_error(GL_INVALID_VALUE); return; }
    for (i = 0; i < n; i++) {
        texture *t = table_get(&gl.textures, names[i]);
        if (!t) continue;
        for (u = 0; u < MAX_UNITS; u++) if (gl.unit_texture[u] == names[i]) gl.unit_texture[u] = 0;
        for (u = 0; u < gl.framebuffers.count; u++) {
            framebuffer *f = gl.framebuffers.items[u].object;
            if (f->color_type == GL_TEXTURE && f->color == names[i]) { f->color = 0; f->color_type = GL_NONE; }
        }
        free(t->rgba); free(t);
        table_remove(&gl.textures, names[i]);
    }
}
GLboolean glIsTexture(GLuint name) { return table_get(&gl.textures, name) != 0; }

/* ------------------------------------------------------ renderbuffers, FBOs */
void glGenRenderbuffers(GLsizei n, GLuint *names)
{
    GLsizei i;
    if (n < 0) { set_error(GL_INVALID_VALUE); return; }
    for (i = 0; i < n; i++) {
        renderbuffer *r = calloc(1, sizeof(renderbuffer));
        names[i] = table_fresh(&gl.renderbuffers);
        if (!r || !table_put(&gl.renderbuffers, names[i], r)) { set_error(GL_OUT_OF_MEMORY); return; }
    }
}
void glBindRenderbuffer(GLenum target, GLuint name)
{
    if (target != GL_RENDERBUFFER) { set_error(GL_INVALID_ENUM); return; }
    if (name && !table_get(&gl.renderbuffers, name)) {
        renderbuffer *r = calloc(1, sizeof(renderbuffer));
        if (!r || !table_put(&gl.renderbuffers, name, r)) { set_error(GL_OUT_OF_MEMORY); return; }
    }
    gl.renderbuffer_binding = name;
}
void glRenderbufferStorage(GLenum target, GLenum format, GLsizei w, GLsizei h)
{
    renderbuffer *r;
    if (target != GL_RENDERBUFFER) { set_error(GL_INVALID_ENUM); return; }
    if (format != GL_RGBA4 && format != GL_RGB5_A1 && format != GL_RGB565 && format != GL_DEPTH_COMPONENT16 && format != GL_STENCIL_INDEX8) { set_error(GL_INVALID_ENUM); return; }
    if (w < 0 || h < 0 || w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE) { set_error(GL_INVALID_VALUE); return; }
    if (!(r = table_get(&gl.renderbuffers, gl.renderbuffer_binding))) { set_error(GL_INVALID_OPERATION); return; }
    free(r->rgba); free(r->depth); r->rgba = 0; r->depth = 0;
    r->width = w; r->height = h; r->format = format;
    if (format == GL_DEPTH_COMPONENT16) r->depth = malloc((size_t)(w ? w : 1) * (size_t)(h ? h : 1) * sizeof(float));
    else if (format != GL_STENCIL_INDEX8) r->rgba = calloc((size_t)(w ? w : 1) * (size_t)(h ? h : 1), 4u);
    if (format != GL_STENCIL_INDEX8 && !r->rgba && !r->depth) set_error(GL_OUT_OF_MEMORY);
}
void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
    renderbuffer *r;
    if (target != GL_RENDERBUFFER) { set_error(GL_INVALID_ENUM); return; }
    if (!(r = table_get(&gl.renderbuffers, gl.renderbuffer_binding))) { set_error(GL_INVALID_OPERATION); return; }
    if (pname == GL_RENDERBUFFER_WIDTH) *params = r->width;
    else if (pname == GL_RENDERBUFFER_HEIGHT) *params = r->height;
    else if (pname == GL_RENDERBUFFER_INTERNAL_FORMAT) *params = (GLint)(r->format ? r->format : GL_RGBA4);
    else set_error(GL_INVALID_ENUM);
}
void glDeleteRenderbuffers(GLsizei n, const GLuint *names)
{
    GLsizei i;
    unsigned int u;
    if (n < 0) { set_error(GL_INVALID_VALUE); return; }
    for (i = 0; i < n; i++) {
        renderbuffer *r = table_get(&gl.renderbuffers, names[i]);
        if (!r) continue;
        if (gl.renderbuffer_binding == names[i]) gl.renderbuffer_binding = 0;
        for (u = 0; u < gl.framebuffers.count; u++) {
            framebuffer *f = gl.framebuffers.items[u].object;
            if (f->color_type == GL_RENDERBUFFER && f->color == names[i]) { f->color = 0; f->color_type = GL_NONE; }
            if (f->depth == names[i]) f->depth = 0;
            if (f->stencil == names[i]) f->stencil = 0;
        }
        free(r->rgba); free(r->depth); free(r);
        table_remove(&gl.renderbuffers, names[i]);
    }
}
GLboolean glIsRenderbuffer(GLuint name) { return table_get(&gl.renderbuffers, name) != 0; }

void glGenFramebuffers(GLsizei n, GLuint *names)
{
    GLsizei i;
    if (n < 0) { set_error(GL_INVALID_VALUE); return; }
    for (i = 0; i < n; i++) {
        framebuffer *f = calloc(1, sizeof(framebuffer));
        names[i] = table_fresh(&gl.framebuffers);
        if (!f || !table_put(&gl.framebuffers, names[i], f)) { set_error(GL_OUT_OF_MEMORY); return; }
    }
}
static GLenum framebuffer_status(const framebuffer *f, target *out);
void glBindFramebuffer(GLenum target_enum, GLuint name)
{
    framebuffer *old;
    if (target_enum != GL_FRAMEBUFFER) { set_error(GL_INVALID_ENUM); return; }
    if (name && !table_get(&gl.framebuffers, name)) {
        framebuffer *f = calloc(1, sizeof(framebuffer));
        if (!f || !table_put(&gl.framebuffers, name, f)) { set_error(GL_OUT_OF_MEMORY); return; }
    }
    old = table_get(&gl.framebuffers, gl.framebuffer_binding);
    gl.framebuffer_binding = name;
    if (old && old != table_get(&gl.framebuffers, name) && old->gather && old->dirty) {
        target t;
        old->dirty = 0;
        if (framebuffer_status(old, &t) == GL_FRAMEBUFFER_COMPLETE && t.color)
            old->gather(t.color, t.width, t.height, old->band_rows);
    }
}

int deGlDistributeFramebuffer(GLuint name, int band_rows, int nodes, int node, de_gl_gather gather)
{
    framebuffer *f = table_get(&gl.framebuffers, name);
    target t;
    int y;
    if (!f || band_rows < 1 || nodes < 1 || node < 0 || node >= nodes || framebuffer_status(f, &t) != GL_FRAMEBUFFER_COMPLETE) return 0;
    free(f->owned);
    f->owned = malloc((size_t)t.height);
    if (!f->owned) return 0;
    for (y = 0; y < t.height; y++) f->owned[y] = (y / band_rows) % nodes == node;
    f->owned_rows = t.height; f->band_rows = band_rows; f->gather = gather; f->dirty = 0;
    return 1;
}
static framebuffer *bound_framebuffer(GLenum target)
{
    framebuffer *f;
    if (target != GL_FRAMEBUFFER) { set_error(GL_INVALID_ENUM); return 0; }
    f = table_get(&gl.framebuffers, gl.framebuffer_binding);
    if (!f) set_error(GL_INVALID_OPERATION);
    return f;
}
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint name, GLint level)
{
    framebuffer *f = bound_framebuffer(target);
    if (!f) return;
    if (attachment != GL_COLOR_ATTACHMENT0) { set_error(attachment == GL_DEPTH_ATTACHMENT || attachment == GL_STENCIL_ATTACHMENT ? GL_INVALID_OPERATION : GL_INVALID_ENUM); return; }
    if (name && (textarget != GL_TEXTURE_2D || !table_get(&gl.textures, name))) { set_error(GL_INVALID_OPERATION); return; }
    if (name && level) { set_error(GL_INVALID_VALUE); return; }
    f->color_type = name ? GL_TEXTURE : GL_NONE; f->color = name;
}
void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum rbtarget, GLuint name)
{
    framebuffer *f = bound_framebuffer(target);
    if (!f) return;
    if (name && (rbtarget != GL_RENDERBUFFER || !table_get(&gl.renderbuffers, name))) { set_error(GL_INVALID_OPERATION); return; }
    if (attachment == GL_COLOR_ATTACHMENT0) { f->color_type = name ? GL_RENDERBUFFER : GL_NONE; f->color = name; }
    else if (attachment == GL_DEPTH_ATTACHMENT) f->depth = name;
    else if (attachment == GL_STENCIL_ATTACHMENT) f->stencil = name;
    else set_error(GL_INVALID_ENUM);
}

static GLenum framebuffer_status(const framebuffer *f, target *out)
{
    int w = -1, h = -1, any = 0;
    target t;
    memset(&t, 0, sizeof(t));
    if (f->color_type == GL_TEXTURE) {
        texture *x = table_get(&gl.textures, f->color);
        if (!x || !x->rgba || !x->width || !x->height || (x->format != GL_RGBA && x->format != GL_RGB)) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        w = x->width; h = x->height; t.color = x->rgba; any = 1;
    } else if (f->color_type == GL_RENDERBUFFER) {
        renderbuffer *r = table_get(&gl.renderbuffers, f->color);
        if (!r || !r->rgba || !r->width || !r->height) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        w = r->width; h = r->height; t.color = r->rgba; any = 1;
    }
    if (f->depth) {
        renderbuffer *r = table_get(&gl.renderbuffers, f->depth);
        if (!r || !r->depth || !r->width || !r->height) return GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        if (any && (r->width != w || r->height != h)) return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
        w = r->width; h = r->height; t.depth = r->depth; any = 1;
    }
    if (f->stencil) return GL_FRAMEBUFFER_UNSUPPORTED;  /* no stencil buffer */
    if (!any) return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    t.width = w; t.height = h; t.valid = 1;
    if (out) *out = t;
    return GL_FRAMEBUFFER_COMPLETE;
}
GLenum glCheckFramebufferStatus(GLenum target)
{
    framebuffer *f;
    if (target != GL_FRAMEBUFFER) { set_error(GL_INVALID_ENUM); return 0; }
    if (!gl.framebuffer_binding) return gl.window_color ? GL_FRAMEBUFFER_COMPLETE : GL_FRAMEBUFFER_UNSUPPORTED;
    f = table_get(&gl.framebuffers, gl.framebuffer_binding);
    return f ? framebuffer_status(f, 0) : GL_FRAMEBUFFER_UNSUPPORTED;
}
void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *params)
{
    framebuffer *f = bound_framebuffer(target);
    GLenum type;
    GLuint name;
    if (!f) return;
    if (attachment == GL_COLOR_ATTACHMENT0) { type = f->color_type; name = f->color; }
    else if (attachment == GL_DEPTH_ATTACHMENT) { type = f->depth ? GL_RENDERBUFFER : GL_NONE; name = f->depth; }
    else if (attachment == GL_STENCIL_ATTACHMENT) { type = f->stencil ? GL_RENDERBUFFER : GL_NONE; name = f->stencil; }
    else { set_error(GL_INVALID_ENUM); return; }
    if (pname == GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE) *params = (GLint)type;
    else if (pname == GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME && type != GL_NONE) *params = (GLint)name;
    else if (pname == GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL && type == GL_TEXTURE) *params = 0;
    else set_error(GL_INVALID_ENUM);
}
void glDeleteFramebuffers(GLsizei n, const GLuint *names)
{
    GLsizei i;
    if (n < 0) { set_error(GL_INVALID_VALUE); return; }
    for (i = 0; i < n; i++) {
        framebuffer *f = table_get(&gl.framebuffers, names[i]);
        if (!f) continue;
        if (gl.framebuffer_binding == names[i]) gl.framebuffer_binding = 0;
        free(f);
        table_remove(&gl.framebuffers, names[i]);
    }
}
GLboolean glIsFramebuffer(GLuint name) { return table_get(&gl.framebuffers, name) != 0; }

/* The current draw target, or 0 with INVALID_FRAMEBUFFER_OPERATION set. */
static int current_target(target *t)
{
    if (!gl.framebuffer_binding) {
        if (!gl.window_color) { set_error(GL_INVALID_FRAMEBUFFER_OPERATION); return 0; }
        t->width = gl.window_width; t->height = gl.window_height;
        t->color = gl.window_color; t->depth = gl.window_depth; t->owned = gl.window_owned; t->valid = 1;
        return 1;
    }
    {
        framebuffer *f = table_get(&gl.framebuffers, gl.framebuffer_binding);
        if (!f || framebuffer_status(f, t) != GL_FRAMEBUFFER_COMPLETE) { set_error(GL_INVALID_FRAMEBUFFER_OPERATION); return 0; }
        t->owned = f->owned && f->owned_rows == t->height ? f->owned : 0;
        if (f->gather) f->dirty = 1;
        return 1;
    }
}

/* ------------------------------------------------------------------ shaders */
GLuint glCreateShader(GLenum type)
{
    shader *s;
    GLuint name;
    if (type != GL_VERTEX_SHADER && type != GL_FRAGMENT_SHADER) { set_error(GL_INVALID_ENUM); return 0; }
    s = calloc(1, sizeof(shader));
    if (!s) { set_error(GL_OUT_OF_MEMORY); return 0; }
    s->type = type;
    /* Shaders and programs share one name space. */
    name = table_fresh(&gl.shaders);
    while (table_get(&gl.programs, name)) name = table_fresh(&gl.shaders);
    if (!table_put(&gl.shaders, name, s)) { free(s); set_error(GL_OUT_OF_MEMORY); return 0; }
    gl.programs.next = gl.shaders.next > gl.programs.next ? gl.shaders.next : gl.programs.next;
    return name;
}
static shader *shader_named(GLuint name)
{
    shader *s = table_get(&gl.shaders, name);
    if (!s) set_error(table_get(&gl.programs, name) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
    return s;
}
void glShaderSource(GLuint name, GLsizei count, const GLchar *const *strings, const GLint *lengths)
{
    shader *s = shader_named(name);
    size_t total = 0, at = 0;
    GLsizei i;
    char *text;
    if (!s) return;
    if (count < 0) { set_error(GL_INVALID_VALUE); return; }
    for (i = 0; i < count; i++) total += lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
    text = malloc(total + 1);
    if (!text) { set_error(GL_OUT_OF_MEMORY); return; }
    for (i = 0; i < count; i++) {
        size_t n = lengths && lengths[i] >= 0 ? (size_t)lengths[i] : strlen(strings[i]);
        memcpy(text + at, strings[i], n); at += n;
    }
    text[at] = 0;
    free(s->source); s->source = text;
}
void glGetShaderSource(GLuint name, GLsizei bufsize, GLsizei *length, GLchar *source)
{
    shader *s = shader_named(name);
    GLsizei n = 0;
    if (!s) return;
    if (bufsize < 0) { set_error(GL_INVALID_VALUE); return; }
    if (bufsize > 0) {
        const char *src = s->source ? s->source : "";
        while (src[n] && n < bufsize - 1) { source[n] = src[n]; n++; }
        source[n] = 0;
    }
    if (length) *length = n;
}

static char *copy_string(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

/* The preprocessor: object-like macros without values, #ifdef/#ifndef/
 * #if/#elif/#else/#endif over defined(), integers and !, and #undef.
 * Directive lines become blank so line numbers survive. */
#define PP_MACROS 64
typedef struct { char names[PP_MACROS][32]; int count; } macros;
static int pp_defined(const macros *m, const char *name)
{
    int i;
    for (i = 0; i < m->count; i++) if (!strcmp(m->names[i], name)) return 1;
    return 0;
}
static const char *pp_word(const char *p, const char *end, char *out)
{
    int n = 0;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    while (p < end && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')) {
        if (n < 31) out[n++] = *p;
        p++;
    }
    out[n] = 0;
    return p;
}
static int pp_expression(const macros *m, const char *p, const char *end, int *ok)
{
    char word[32];
    int negate = 0, value;
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    while (p < end && *p == '!') { negate ^= 1; p++; while (p < end && (*p == ' ' || *p == '\t')) p++; }
    p = pp_word(p, end, word);
    if (!strcmp(word, "defined")) {
        int paren;
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        paren = p < end && *p == '(';
        if (paren) p++;
        p = pp_word(p, end, word);
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (paren) { if (p < end && *p == ')') p++; else *ok = 0; }
        value = pp_defined(m, word);
    } else if (word[0] >= '0' && word[0] <= '9') value = atoi(word) != 0;
    else if (!strcmp(word, "GL_ES")) value = 1;
    else { *ok = 0; value = 0; }
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
    if (p < end && !(p + 1 < end && p[0] == '/' && p[1] == '/')) *ok = 0;
    return negate ? !value : value;
}
static char *preprocess(const char *src, const char **error)
{
    macros m;
    size_t n = strlen(src);
    char *out = malloc(n + 1), *o = out;
    int depth = 0, active[16], taken[16], parent_active = 1, live = 1;
    m.count = 0;
    if (!out) { *error = "out of memory"; return 0; }
    while (*src) {
        const char *line = src, *end, *p;
        while (*src && *src != '\n') src++;
        end = src;
        if (*src) src++;
        p = line;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
        if (p < end && *p == '#') {
            char op[32], name[32];
            p = pp_word(p + 1, end, op);
            if (!strcmp(op, "define") || !strcmp(op, "undef") || !strcmp(op, "ifdef") || !strcmp(op, "ifndef")) {
                const char *rest = pp_word(p, end, name);
                while (rest < end && (*rest == ' ' || *rest == '\t' || *rest == '\r')) rest++;
                if (!name[0]) { *error = "missing macro name"; free(out); return 0; }
                if (!strcmp(op, "define")) {
                    if (rest < end && !(rest + 1 < end && rest[0] == '/' && rest[1] == '/')) { *error = "macros with values are not supported"; free(out); return 0; }
                    if (live && !pp_defined(&m, name)) {
                        if (m.count == PP_MACROS) { *error = "too many macros"; free(out); return 0; }
                        strcpy(m.names[m.count++], name);
                    }
                } else if (!strcmp(op, "undef")) {
                    if (live) {
                        int i;
                        for (i = 0; i < m.count; i++) if (!strcmp(m.names[i], name)) { m.names[i][0] = 0; strcpy(m.names[i], m.names[--m.count]); break; }
                    }
                } else {
                    int v = pp_defined(&m, name);
                    if (depth == 16) { *error = "conditionals nested too deeply"; free(out); return 0; }
                    if (!strcmp(op, "ifndef")) v = !v;
                    active[depth] = parent_active; taken[depth] = v; depth++;
                    parent_active = live; live = live && v;
                }
            } else if (!strcmp(op, "if") || !strcmp(op, "elif")) {
                int ok = 1, v = pp_expression(&m, p, end, &ok);
                if (!ok) { *error = "unsupported #if expression"; free(out); return 0; }
                if (!strcmp(op, "if")) {
                    if (depth == 16) { *error = "conditionals nested too deeply"; free(out); return 0; }
                    active[depth] = parent_active; taken[depth] = v; depth++;
                    parent_active = live; live = live && v;
                } else {
                    if (!depth) { *error = "#elif without #if"; free(out); return 0; }
                    live = parent_active && !taken[depth - 1] && v;
                    if (v) taken[depth - 1] = 1;
                }
            } else if (!strcmp(op, "else")) {
                if (!depth) { *error = "#else without #if"; free(out); return 0; }
                live = parent_active && !taken[depth - 1];
                taken[depth - 1] = 1;
            } else if (!strcmp(op, "endif")) {
                if (!depth) { *error = "#endif without #if"; free(out); return 0; }
                depth--;
                live = parent_active; parent_active = active[depth];
            } else if (!strcmp(op, "version") || !strcmp(op, "extension") || !strcmp(op, "pragma") || !strcmp(op, "line") || !op[0]) {
                /* accepted and ignored */
            } else if (!strcmp(op, "error")) {
                if (live) { *error = "#error"; free(out); return 0; }
            } else { *error = "unsupported preprocessor directive"; free(out); return 0; }
            *o++ = '\n';
            continue;
        }
        if (live) { memcpy(o, line, (size_t)(end - line)); o += end - line; }
        if (end < src) *o++ = '\n';
    }
    if (depth) { *error = "unterminated conditional"; free(out); return 0; }
    *o = 0;
    return out;
}

void glCompileShader(GLuint name)
{
    shader *s = shader_named(name);
    const char *error = 0;
    char *text;
    if (!s) return;
    s->compiled = GL_FALSE;
    free(s->log); s->log = 0;
    if (!s->ir) s->ir = malloc(sizeof(tg_glsl_fragment_program));
    if (!s->ir) { set_error(GL_OUT_OF_MEMORY); return; }
    text = preprocess(s->source ? s->source : "", &error);
    if (!text) { s->log = copy_string(error); return; }
    if (s->type == GL_VERTEX_SHADER ? tg_glsl_compile_vertex(text, s->ir) : tg_glsl_compile_fragment(text, s->ir)) s->compiled = GL_TRUE;
    else s->log = copy_string("unsupported or invalid GLSL ES for this bounded compiler (declarations, assignments, "
                              "vector arithmetic, swizzles, dot/cross/normalize/max/pow/fract, texture2D and float(vec4) helpers only)");
    free(text);
}
void glGetShaderiv(GLuint name, GLenum pname, GLint *params)
{
    shader *s = shader_named(name);
    if (!s) return;
    switch (pname) {
    case GL_SHADER_TYPE: *params = (GLint)s->type; break;
    case GL_DELETE_STATUS: *params = s->deleted; break;
    case GL_COMPILE_STATUS: *params = s->compiled; break;
    case GL_INFO_LOG_LENGTH: *params = s->log ? (GLint)strlen(s->log) + 1 : 0; break;
    case GL_SHADER_SOURCE_LENGTH: *params = s->source ? (GLint)strlen(s->source) + 1 : 0; break;
    default: set_error(GL_INVALID_ENUM);
    }
}
static void copy_log(const char *log, GLsizei bufsize, GLsizei *length, GLchar *out)
{
    GLsizei n = 0;
    if (bufsize < 0) { set_error(GL_INVALID_VALUE); return; }
    if (bufsize > 0) {
        if (log) while (log[n] && n < bufsize - 1) { out[n] = log[n]; n++; }
        out[n] = 0;
    }
    if (length) *length = n;
}
void glGetShaderInfoLog(GLuint name, GLsizei bufsize, GLsizei *length, GLchar *log)
{ shader *s = shader_named(name); if (s) copy_log(s->log, bufsize, length, log); }
void glGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype, GLint *range, GLint *precision)
{
    if (shadertype != GL_VERTEX_SHADER && shadertype != GL_FRAGMENT_SHADER) { set_error(GL_INVALID_ENUM); return; }
    if (precisiontype >= GL_LOW_FLOAT && precisiontype <= GL_HIGH_FLOAT) { range[0] = 127; range[1] = 127; *precision = 23; }
    else if (precisiontype >= GL_LOW_INT && precisiontype <= GL_HIGH_INT) { range[0] = 24; range[1] = 24; *precision = 0; }
    else set_error(GL_INVALID_ENUM);
}
void glShaderBinary(GLsizei n, const GLuint *shaders, GLenum format, const GLvoid *binary, GLsizei length)
{ (void)n; (void)shaders; (void)format; (void)binary; (void)length; set_error(GL_INVALID_ENUM); }
void glDeleteShader(GLuint name)
{
    shader *s;
    if (!name) return;
    if (!(s = shader_named(name))) return;
    s->deleted = GL_TRUE;
    if (!s->attached) {
        free(s->source); free(s->log); free(s->ir); free(s);
        table_remove(&gl.shaders, name);
    }
}
GLboolean glIsShader(GLuint name) { return table_get(&gl.shaders, name) != 0; }

/* --------------------------------------------------------------- programs */
GLuint glCreateProgram(void)
{
    program *p = calloc(1, sizeof(program));
    GLuint name;
    if (!p) { set_error(GL_OUT_OF_MEMORY); return 0; }
    name = table_fresh(&gl.programs);
    while (table_get(&gl.shaders, name)) name = table_fresh(&gl.programs);
    if (!table_put(&gl.programs, name, p)) { free(p); set_error(GL_OUT_OF_MEMORY); return 0; }
    gl.shaders.next = gl.programs.next > gl.shaders.next ? gl.programs.next : gl.shaders.next;
    return name;
}
static program *program_named(GLuint name)
{
    program *p = table_get(&gl.programs, name);
    if (!p) set_error(table_get(&gl.shaders, name) ? GL_INVALID_OPERATION : GL_INVALID_VALUE);
    return p;
}
void glAttachShader(GLuint pname, GLuint sname)
{
    program *p = program_named(pname);
    shader *s;
    if (!p) return;
    if (!(s = shader_named(sname))) return;
    if (s->type == GL_VERTEX_SHADER ? p->vs_name != 0 : p->fs_name != 0) { set_error(GL_INVALID_OPERATION); return; }
    if (s->type == GL_VERTEX_SHADER) p->vs_name = sname; else p->fs_name = sname;
    s->attached++;
}
static void release_shader(GLuint name)
{
    shader *s = table_get(&gl.shaders, name);
    if (!s) return;
    s->attached--;
    if (s->deleted && !s->attached) {
        free(s->source); free(s->log); free(s->ir); free(s);
        table_remove(&gl.shaders, name);
    }
}
void glDetachShader(GLuint pname, GLuint sname)
{
    program *p = program_named(pname);
    if (!p) return;
    if (!shader_named(sname)) return;
    if (p->vs_name == sname) p->vs_name = 0;
    else if (p->fs_name == sname) p->fs_name = 0;
    else { set_error(GL_INVALID_OPERATION); return; }
    release_shader(sname);
}
void glGetAttachedShaders(GLuint pname, GLsizei maxcount, GLsizei *count, GLuint *shaders)
{
    program *p = program_named(pname);
    GLsizei n = 0;
    if (!p) return;
    if (maxcount < 0) { set_error(GL_INVALID_VALUE); return; }
    if (p->vs_name && n < maxcount) shaders[n++] = p->vs_name;
    if (p->fs_name && n < maxcount) shaders[n++] = p->fs_name;
    if (count) *count = n;
}
void glBindAttribLocation(GLuint pname, GLuint index, const GLchar *name)
{
    program *p = program_named(pname);
    int i;
    if (!p) return;
    if (index >= MAX_ATTRIBS) { set_error(GL_INVALID_VALUE); return; }
    if (!strncmp(name, "gl_", 3)) { set_error(GL_INVALID_OPERATION); return; }
    for (i = 0; i < p->binding_count; i++) if (!strcmp(p->bindings[i].name, name)) { p->bindings[i].index = index; return; }
    if (p->binding_count == MAX_ATTRIBS || strlen(name) > 31) return;
    strcpy(p->bindings[p->binding_count].name, name);
    p->bindings[p->binding_count++].index = index;
}

/* Checks every instruction against the interpreter's register file, so the
 * per-fragment loop can run without bounds checks. */
static int validate_code(const tg_glsl_fragment_program *ir, int samplers)
{
    int i;
    for (i = 0; i < ir->instruction_count; i++) {
        const tg_vm_instruction *in = &ir->code[i];
        if (in->op == TG_VM_CALL) { if (in->aux >= ir->instruction_count) return 0; continue; }
        if (in->op == TG_VM_RETURN) continue;
        if (in->op < TG_VM_MOV || in->op > TG_VM_FRACT) return 0;
        if (in->dst >= TG_VM_REGS || in->a >= TG_VM_REGS || in->b >= TG_VM_REGS || !in->width || in->width > 4 || (in->flags & ~3u)) return 0;
        if (in->op == TG_VM_MAT4_VEC4 && (in->a > TG_VM_REGS - 4 || in->width != 4)) return 0;
        if (in->op == TG_VM_CROSS && in->width != 3) return 0;
        if (in->op == TG_VM_TEXTURE2D && (in->width != 4 || in->aux >= samplers)) return 0;
        if (in->op == TG_VM_SET_COMPONENT && in->aux > 3) return 0;
        if (in->op == TG_VM_GREATER && in->width != 1) return 0;
    }
    return ir->entry_pc <= ir->instruction_count;
}

#define FAST_SLOTS (TG_VM_REGS * 4 + 4)
static int decode_program(fast_code *fc, const tg_glsl_fragment_program *ir, float (*reg)[4], const unsigned char *outputs);

static void free_executable(program *p)
{
    free(p->vs); free(p->fs); free(p->vregs); free(p->fregs);
    free(p->vfast.ops); free(p->ffast.ops); free(p->vpos.ops);
    memset(&p->vfast, 0, sizeof(p->vfast)); memset(&p->ffast, 0, sizeof(p->ffast)); memset(&p->vpos, 0, sizeof(p->vpos));
    p->vs = p->fs = 0; p->vregs = p->fregs = 0;
    p->attrib_count = p->uniform_count = p->varying_floats = 0;
}
static int link_fail(program *p, const char *why)
{
    free_executable(p);
    p->log = copy_string(why);
    p->linked = GL_FALSE;
    return 0;
}
static int find_uniform(program *p, const char *name)
{
    int i;
    for (i = 0; i < p->uniform_count; i++) if (!strcmp(p->uniforms[i].name, name)) return i;
    return -1;
}
static int add_uniforms(program *p, const tg_glsl_fragment_program *ir, int vertex)
{
    int i;
    for (i = 0; i < ir->symbol_count; i++) {
        const tg_glsl_symbol *s = &ir->symbols[i];
        int k, sampler = s->kind == TG_GLSL_SAMPLER;
        uniform_info *u;
        if (s->kind != TG_GLSL_UNIFORM && !sampler) continue;
        k = find_uniform(p, s->name);
        if (k < 0) {
            if (p->uniform_count == MAX_UNIFORMS) return 0;
            k = p->uniform_count++;
            u = &p->uniforms[k];
            memset(u, 0, sizeof(*u));
            strcpy(u->name, s->name);
            u->width = sampler ? 0 : s->width;
            u->vreg = u->freg = u->vsampler = u->fsampler = -1;
        }
        u = &p->uniforms[k];
        if ((sampler ? 0 : s->width) != u->width) return 0;
        if (sampler) { if (vertex) u->vsampler = s->slot; else u->fsampler = s->slot; }
        else { if (vertex) u->vreg = s->slot; else u->freg = s->slot; }
    }
    return 1;
}
void glLinkProgram(GLuint pname)
{
    program *p = program_named(pname);
    shader *vs, *fs;
    int i, j, used = 0;
    if (!p) return;
    free(p->log); p->log = 0;
    free_executable(p);
    p->linked = GL_FALSE;
    vs = table_get(&gl.shaders, p->vs_name); fs = table_get(&gl.shaders, p->fs_name);
    if (!vs || !fs) { link_fail(p, "a vertex and a fragment shader must be attached"); return; }
    if (!vs->compiled || !fs->compiled) { link_fail(p, "attached shaders are not compiled"); return; }
    p->vs = malloc(sizeof(tg_glsl_fragment_program)); p->fs = malloc(sizeof(tg_glsl_fragment_program));
    p->vregs = malloc(sizeof(float) * 4u * TG_VM_REGS); p->fregs = malloc(sizeof(float) * 4u * TG_VM_REGS);
    if (!p->vs || !p->fs || !p->vregs || !p->fregs) { link_fail(p, "out of memory"); set_error(GL_OUT_OF_MEMORY); return; }
    *p->vs = *vs->ir; *p->fs = *fs->ir;
    memcpy(p->vregs, p->vs->initial, sizeof(float) * 4u * TG_VM_REGS);
    memcpy(p->fregs, p->fs->initial, sizeof(float) * 4u * TG_VM_REGS);
    p->vs_out = p->vs->output_register; p->fs_out = p->fs->output_register;
    p->vsamplers = p->fsamplers = 0;
    for (i = 0; i < p->vs->symbol_count; i++) if (p->vs->symbols[i].kind == TG_GLSL_SAMPLER) p->vsamplers++;
    for (i = 0; i < p->fs->symbol_count; i++) if (p->fs->symbols[i].kind == TG_GLSL_SAMPLER) p->fsamplers++;
    if (!validate_code(p->vs, p->vsamplers) || !validate_code(p->fs, p->fsamplers)) { link_fail(p, "compiled code failed validation"); return; }
    /* Varyings: every one the fragment shader reads must come from the
     * vertex shader with the same width. */
    for (i = 0; i < p->fs->symbol_count; i++) {
        const tg_glsl_symbol *f = &p->fs->symbols[i];
        int v;
        if (f->kind != TG_GLSL_VARYING) continue;
        v = tg_glsl_find_symbol(p->vs, f->name);
        if (v < 0 || p->vs->symbols[v].kind != TG_GLSL_VARYING || p->vs->symbols[v].width != f->width) { link_fail(p, "a fragment varying has no matching vertex varying"); return; }
        if (p->varying_floats + f->width > MAX_VARYING_FLOATS) { link_fail(p, "too many varyings"); return; }
        for (j = 0; j < f->width; j++) {
            p->vary_vreg[p->varying_floats] = p->vs->symbols[v].slot;
            p->vary_freg[p->varying_floats] = f->slot;
            p->vary_comp[p->varying_floats++] = (unsigned char)j;
        }
    }
    /* Attributes: explicit bindings first, then the lowest free location
     * in declaration order. */
    for (i = 0; i < p->vs->symbol_count; i++) {
        const tg_glsl_symbol *s = &p->vs->symbols[i];
        attrib_info *a;
        if (s->kind != TG_GLSL_ATTRIBUTE) continue;
        if (p->attrib_count == MAX_ATTRIBS) { link_fail(p, "too many attributes"); return; }
        a = &p->attribs[p->attrib_count++];
        strcpy(a->name, s->name); a->width = s->width; a->reg = s->slot; a->location = -1;
        for (j = 0; j < p->binding_count; j++) if (!strcmp(p->bindings[j].name, s->name)) {
            if (used & (1 << p->bindings[j].index)) { link_fail(p, "two attributes bound to one location"); return; }
            a->location = (int)p->bindings[j].index; used |= 1 << a->location;
        }
    }
    for (i = 0; i < p->attrib_count; i++) if (p->attribs[i].location < 0) {
        for (j = 0; j < MAX_ATTRIBS && (used & (1 << j)); j++) ;
        if (j == MAX_ATTRIBS) { link_fail(p, "too many attributes"); return; }
        p->attribs[i].location = j; used |= 1 << j;
    }
    if (!add_uniforms(p, p->vs, 1) || !add_uniforms(p, p->fs, 0)) { link_fail(p, "uniform declarations disagree or too many uniforms"); return; }
    {
        /* What each stage must leave: gl_Position and the varyings the
         * fragment shader reads; gl_FragColor. */
        unsigned char *vout = calloc(FAST_SLOTS, 1), *fout = calloc(FAST_SLOTS, 1);
        int ok;
        if (!vout || !fout) { free(vout); free(fout); link_fail(p, "out of memory"); return; }
        for (i = 0; i < 4; i++) { vout[p->vs_out * 4 + i] = 1; fout[p->fs_out * 4 + i] = 1; }
        for (i = 0; i < p->varying_floats; i++) vout[p->vary_vreg[i] * 4 + p->vary_comp[i]] = 1;
        ok = decode_program(&p->vfast, p->vs, p->vregs, vout) && decode_program(&p->ffast, p->fs, p->fregs, fout);
        if (ok) {
            memset(vout, 0, FAST_SLOTS);
            for (i = 0; i < 4; i++) vout[p->vs_out * 4 + i] = 1;
            ok = decode_program(&p->vpos, p->vs, p->vregs, vout);
        }
        free(vout); free(fout);
        if (!ok) { link_fail(p, "code could not be decoded"); return; }
        /* Positions first only if the rest of the vertex program is a
         * good part of it: the shadow pass's is almost all position. */
        p->vpos_split = p->vpos.count * 4 < p->vfast.count * 3;
#ifdef DE_GL_TRACE
        printf("[gltrace] link %u: vertex %d instructions -> %d ops (position %d%s), fragment %d -> %d ops, %d varying floats\n",
               pname, p->vs->instruction_count, p->vfast.count, p->vpos.count, p->vpos_split ? ", split" : "",
               p->fs->instruction_count, p->ffast.count, p->varying_floats);
        {
            static const char *names[] = {"mul", "add", "div", "sub", "mov", "fract", "max", "gt", "pow", "dot2", "dot3", "dot4",
                                          "cross", "norm2", "norm3", "norm4", "tex", "mat4"};
            int counts[18], k;
            memset(counts, 0, sizeof(counts));
            for (k = 0; k < p->ffast.count; k++) counts[p->ffast.ops[k].op]++;
            printf("[gltrace]   fragment ops:");
            for (k = 0; k < 18; k++) if (counts[k]) printf(" %s %d", names[k], counts[k]);
            printf("\n");
        }
#endif
    }
    p->linked = GL_TRUE;
}
void glUseProgram(GLuint name)
{
    GLuint old = gl.program;
    program *p;
    if (name) {
        if (!(p = program_named(name))) return;
        if (!p->linked) { set_error(GL_INVALID_OPERATION); return; }
    }
    gl.program = name;
    /* A program deleted while current goes once it is no longer current. */
    if (old && old != name && (p = table_get(&gl.programs, old)) != 0 && p->deleted) glDeleteProgram(old);
}
void glDeleteProgram(GLuint name)
{
    program *p;
    if (!name) return;
    if (!(p = program_named(name))) return;
    p->deleted = GL_TRUE;
    if (gl.program == name) return;   /* freed when no longer current */
    if (p->vs_name) release_shader(p->vs_name);
    if (p->fs_name) release_shader(p->fs_name);
    free_executable(p); free(p->log); free(p);
    table_remove(&gl.programs, name);
}
GLboolean glIsProgram(GLuint name) { return table_get(&gl.programs, name) != 0; }
void glValidateProgram(GLuint name) { program *p = program_named(name); if (p) p->validated = p->linked; }
void glGetProgramiv(GLuint name, GLenum pname, GLint *params)
{
    program *p = program_named(name);
    int i, longest = 0;
    if (!p) return;
    switch (pname) {
    case GL_DELETE_STATUS: *params = p->deleted; break;
    case GL_LINK_STATUS: *params = p->linked; break;
    case GL_VALIDATE_STATUS: *params = p->validated; break;
    case GL_INFO_LOG_LENGTH: *params = p->log ? (GLint)strlen(p->log) + 1 : 0; break;
    case GL_ATTACHED_SHADERS: *params = (p->vs_name != 0) + (p->fs_name != 0); break;
    case GL_ACTIVE_ATTRIBUTES: *params = p->attrib_count; break;
    case GL_ACTIVE_UNIFORMS: *params = p->uniform_count; break;
    case GL_ACTIVE_ATTRIBUTE_MAX_LENGTH:
        for (i = 0; i < p->attrib_count; i++) if ((int)strlen(p->attribs[i].name) + 1 > longest) longest = (int)strlen(p->attribs[i].name) + 1;
        *params = longest; break;
    case GL_ACTIVE_UNIFORM_MAX_LENGTH:
        for (i = 0; i < p->uniform_count; i++) if ((int)strlen(p->uniforms[i].name) + 1 > longest) longest = (int)strlen(p->uniforms[i].name) + 1;
        *params = longest; break;
    default: set_error(GL_INVALID_ENUM);
    }
}
void glGetProgramInfoLog(GLuint name, GLsizei bufsize, GLsizei *length, GLchar *log)
{ program *p = program_named(name); if (p) copy_log(p->log, bufsize, length, log); }
static GLenum type_of_width(int width)
{ return width == 0 ? GL_SAMPLER_2D : width == 1 ? GL_FLOAT : width == 2 ? GL_FLOAT_VEC2 : width == 3 ? GL_FLOAT_VEC3 : width == 4 ? GL_FLOAT_VEC4 : GL_FLOAT_MAT4; }
static void copy_name(const char *src, GLsizei bufsize, GLsizei *length, GLchar *out) { copy_log(src, bufsize, length, out); }
void glGetActiveAttrib(GLuint name, GLuint index, GLsizei bufsize, GLsizei *length, GLint *size, GLenum *type, GLchar *out)
{
    program *p = program_named(name);
    if (!p) return;
    if (index >= (GLuint)p->attrib_count) { set_error(GL_INVALID_VALUE); return; }
    copy_name(p->attribs[index].name, bufsize, length, out);
    *size = 1; *type = type_of_width(p->attribs[index].width);
}
void glGetActiveUniform(GLuint name, GLuint index, GLsizei bufsize, GLsizei *length, GLint *size, GLenum *type, GLchar *out)
{
    program *p = program_named(name);
    if (!p) return;
    if (index >= (GLuint)p->uniform_count) { set_error(GL_INVALID_VALUE); return; }
    copy_name(p->uniforms[index].name, bufsize, length, out);
    *size = 1; *type = type_of_width(p->uniforms[index].width);
}
GLint glGetAttribLocation(GLuint name, const GLchar *attr)
{
    program *p = program_named(name);
    int i;
    if (!p) return -1;
    if (!p->linked) { set_error(GL_INVALID_OPERATION); return -1; }
    for (i = 0; i < p->attrib_count; i++) if (!strcmp(p->attribs[i].name, attr)) return p->attribs[i].location;
    return -1;
}
GLint glGetUniformLocation(GLuint name, const GLchar *uniform)
{
    program *p = program_named(name);
    if (!p) return -1;
    if (!p->linked) { set_error(GL_INVALID_OPERATION); return -1; }
    return find_uniform(p, uniform);
}

/* Uniform updates go straight into the register files of the linked code. */
static uniform_info *uniform_for(GLint location, program **out)
{
    program *p = table_get(&gl.programs, gl.program);
    if (location == -1) return 0;
    if (!p) { set_error(GL_INVALID_OPERATION); return 0; }
    if (location < 0 || location >= p->uniform_count) { set_error(GL_INVALID_OPERATION); return 0; }
    *out = p;
    return &p->uniforms[location];
}
static void set_uniform(GLint location, GLsizei count, int width, const GLfloat *v)
{
    program *p;
    uniform_info *u = uniform_for(location, &p);
    int i;
    if (!u) return;
    if (count < 0) { set_error(GL_INVALID_VALUE); return; }
    if (u->width != width || !count) { if (u->width != width) set_error(GL_INVALID_OPERATION); return; }
    for (i = 0; i < width; i++) {
        if (u->vreg >= 0) p->vregs[u->vreg][i] = v[i];
        if (u->freg >= 0) p->fregs[u->freg][i] = v[i];
    }
}
static void set_matrix(GLint location, GLsizei count, GLboolean transpose, const GLfloat *v, int n)
{
    program *p;
    uniform_info *u = uniform_for(location, &p);
    int c, r;
    if (!u) return;
    if (count < 0 || transpose) { set_error(GL_INVALID_VALUE); return; }
    if (u->width != n * n || n != 4) { set_error(GL_INVALID_OPERATION); return; }
    if (!count) return;
    for (c = 0; c < 4; c++)
        for (r = 0; r < 4; r++) {
            if (u->vreg >= 0) p->vregs[u->vreg + c][r] = v[c * 4 + r];
            if (u->freg >= 0) p->fregs[u->freg + c][r] = v[c * 4 + r];
        }
}
static void set_sampler(GLint location, GLsizei count, const GLint *v, int width)
{
    program *p;
    uniform_info *u = uniform_for(location, &p);
    if (!u) return;
    if (count < 0) { set_error(GL_INVALID_VALUE); return; }
    if (u->width != 0 || width != 1) { set_error(GL_INVALID_OPERATION); return; }
    if (!count) return;
    if (v[0] < 0 || v[0] >= MAX_UNITS) { set_error(GL_INVALID_VALUE); return; }
    u->unit = v[0];
}
void glUniform1f(GLint l, GLfloat x) { set_uniform(l, 1, 1, &x); }
void glUniform2f(GLint l, GLfloat x, GLfloat y) { GLfloat v[2]; v[0] = x; v[1] = y; set_uniform(l, 1, 2, v); }
void glUniform3f(GLint l, GLfloat x, GLfloat y, GLfloat z) { GLfloat v[3]; v[0] = x; v[1] = y; v[2] = z; set_uniform(l, 1, 3, v); }
void glUniform4f(GLint l, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { GLfloat v[4]; v[0] = x; v[1] = y; v[2] = z; v[3] = w; set_uniform(l, 1, 4, v); }
void glUniform1fv(GLint l, GLsizei n, const GLfloat *v) { set_uniform(l, n, 1, v); }
void glUniform2fv(GLint l, GLsizei n, const GLfloat *v) { set_uniform(l, n, 2, v); }
void glUniform3fv(GLint l, GLsizei n, const GLfloat *v) { set_uniform(l, n, 3, v); }
void glUniform4fv(GLint l, GLsizei n, const GLfloat *v) { set_uniform(l, n, 4, v); }
void glUniform1i(GLint l, GLint x) { set_sampler(l, 1, &x, 1); }
void glUniform1iv(GLint l, GLsizei n, const GLint *v) { set_sampler(l, n, v, 1); }
static void int_uniform(GLint l) { program *p; if (uniform_for(l, &p)) set_error(GL_INVALID_OPERATION); }
void glUniform2i(GLint l, GLint x, GLint y) { (void)x; (void)y; int_uniform(l); }
void glUniform3i(GLint l, GLint x, GLint y, GLint z) { (void)x; (void)y; (void)z; int_uniform(l); }
void glUniform4i(GLint l, GLint x, GLint y, GLint z, GLint w) { (void)x; (void)y; (void)z; (void)w; int_uniform(l); }
void glUniform2iv(GLint l, GLsizei n, const GLint *v) { (void)n; (void)v; int_uniform(l); }
void glUniform3iv(GLint l, GLsizei n, const GLint *v) { (void)n; (void)v; int_uniform(l); }
void glUniform4iv(GLint l, GLsizei n, const GLint *v) { (void)n; (void)v; int_uniform(l); }
void glUniformMatrix2fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v) { set_matrix(l, n, t, v, 2); }
void glUniformMatrix3fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v) { set_matrix(l, n, t, v, 3); }
void glUniformMatrix4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v) { set_matrix(l, n, t, v, 4); }
void glGetUniformfv(GLuint name, GLint location, GLfloat *params)
{
    program *p = program_named(name);
    uniform_info *u;
    int i;
    if (!p) return;
    if (!p->linked || location < 0 || location >= p->uniform_count) { set_error(GL_INVALID_OPERATION); return; }
    u = &p->uniforms[location];
    if (!u->width) { params[0] = (GLfloat)u->unit; return; }
    {
        float (*regs)[4] = u->vreg >= 0 ? p->vregs : p->fregs;
        int base = u->vreg >= 0 ? u->vreg : u->freg;
        /* A mat4 occupies four registers, one column each. */
        for (i = 0; i < u->width; i++) params[i] = regs[base + i / 4][i % 4];
    }
}
void glGetUniformiv(GLuint name, GLint location, GLint *params)
{
    GLfloat v[16];
    program *p = program_named(name);
    int i, n;
    if (!p) return;
    if (!p->linked || location < 0 || location >= p->uniform_count) { set_error(GL_INVALID_OPERATION); return; }
    glGetUniformfv(name, location, v);
    n = p->uniforms[location].width ? p->uniforms[location].width : 1;
    for (i = 0; i < n; i++) params[i] = (GLint)v[i];
}

/* ----------------------------------------------------------- vertex arrays */
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *ptr)
{
    attrib_array *a;
    if (index >= MAX_ATTRIBS || size < 1 || size > 4 || stride < 0) { set_error(GL_INVALID_VALUE); return; }
    if (type != GL_BYTE && type != GL_UNSIGNED_BYTE && type != GL_SHORT && type != GL_UNSIGNED_SHORT && type != GL_FLOAT && type != GL_FIXED) { set_error(GL_INVALID_ENUM); return; }
    a = &gl.arrays[index];
    a->size = size; a->type = type; a->normalized = normalized; a->stride = stride; a->pointer = ptr; a->buffer = gl.array_buffer;
}
void glEnableVertexAttribArray(GLuint index) { if (index >= MAX_ATTRIBS) set_error(GL_INVALID_VALUE); else gl.arrays[index].enabled = GL_TRUE; }
void glDisableVertexAttribArray(GLuint index) { if (index >= MAX_ATTRIBS) set_error(GL_INVALID_VALUE); else gl.arrays[index].enabled = GL_FALSE; }
static void generic(GLuint index, float x, float y, float z, float w)
{
    if (index >= MAX_ATTRIBS) { set_error(GL_INVALID_VALUE); return; }
    gl.generic[index][0] = x; gl.generic[index][1] = y; gl.generic[index][2] = z; gl.generic[index][3] = w;
}
void glVertexAttrib1f(GLuint i, GLfloat x) { generic(i, x, 0, 0, 1); }
void glVertexAttrib2f(GLuint i, GLfloat x, GLfloat y) { generic(i, x, y, 0, 1); }
void glVertexAttrib3f(GLuint i, GLfloat x, GLfloat y, GLfloat z) { generic(i, x, y, z, 1); }
void glVertexAttrib4f(GLuint i, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { generic(i, x, y, z, w); }
void glVertexAttrib1fv(GLuint i, const GLfloat *v) { generic(i, v[0], 0, 0, 1); }
void glVertexAttrib2fv(GLuint i, const GLfloat *v) { generic(i, v[0], v[1], 0, 1); }
void glVertexAttrib3fv(GLuint i, const GLfloat *v) { generic(i, v[0], v[1], v[2], 1); }
void glVertexAttrib4fv(GLuint i, const GLfloat *v) { generic(i, v[0], v[1], v[2], v[3]); }
void glGetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params)
{
    attrib_array *a;
    if (index >= MAX_ATTRIBS) { set_error(GL_INVALID_VALUE); return; }
    a = &gl.arrays[index];
    switch (pname) {
    case GL_CURRENT_VERTEX_ATTRIB: params[0] = gl.generic[index][0]; params[1] = gl.generic[index][1]; params[2] = gl.generic[index][2]; params[3] = gl.generic[index][3]; break;
    case GL_VERTEX_ATTRIB_ARRAY_ENABLED: params[0] = a->enabled; break;
    case GL_VERTEX_ATTRIB_ARRAY_SIZE: params[0] = (GLfloat)a->size; break;
    case GL_VERTEX_ATTRIB_ARRAY_STRIDE: params[0] = (GLfloat)a->stride; break;
    case GL_VERTEX_ATTRIB_ARRAY_TYPE: params[0] = (GLfloat)a->type; break;
    case GL_VERTEX_ATTRIB_ARRAY_NORMALIZED: params[0] = a->normalized; break;
    case GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING: params[0] = (GLfloat)a->buffer; break;
    default: set_error(GL_INVALID_ENUM);
    }
}
void glGetVertexAttribiv(GLuint index, GLenum pname, GLint *params)
{
    GLfloat v[4];
    int i, n = pname == GL_CURRENT_VERTEX_ATTRIB ? 4 : 1;
    glGetVertexAttribfv(index, pname, v);
    for (i = 0; i < n; i++) params[i] = (GLint)v[i];
}
void glGetVertexAttribPointerv(GLuint index, GLenum pname, GLvoid **pointer)
{
    if (index >= MAX_ATTRIBS) { set_error(GL_INVALID_VALUE); return; }
    if (pname != GL_VERTEX_ATTRIB_ARRAY_POINTER) { set_error(GL_INVALID_ENUM); return; }
    *pointer = (GLvoid *)gl.arrays[index].pointer;
}

/* --------------------------------------------------------------- sampling */
static int wrap_index(int i, int n, GLenum mode)
{
    if (mode == GL_CLAMP_TO_EDGE) return i < 0 ? 0 : i >= n ? n - 1 : i;
    if (mode == GL_MIRRORED_REPEAT) {
        int period = 2 * n;
        i %= period;
        if (i < 0) i += period;
        return i < n ? i : period - 1 - i;
    }
    i %= n;
    return i < 0 ? i + n : i;
}
static int floor_int(float v) { int i = (int)v; return (float)i > v ? i - 1 : i; }
/* GLSL leaves huge or NaN coordinates' results undefined and C their
 * conversion: coordinates are clamped to +-2^20, and the texel position to
 * +-2^30. Inline throughout: this runs for every texture2D. */
#define SAFE_COORD(c) ((c) > -1048576.0f ? ((c) < 1048576.0f ? (c) : 1048576.0f) : -1048576.0f)
#define SAFE_TEXEL(c) ((c) > -1073741824.0f ? ((c) < 1073741824.0f ? (c) : 1073741824.0f) : -1073741824.0f)
#define FLOOR_INT(v, out) do { int fi_ = (int)(v); (out) = (float)fi_ > (v) ? fi_ - 1 : fi_; } while (0)
static void sample2d(const sampler *t, float s, float tc, float *out)
{
    float u, v;
    s = SAFE_COORD(s); tc = SAFE_COORD(tc);
    u = s * t->fw; v = tc * t->fh;
    u = SAFE_TEXEL(u); v = SAFE_TEXEL(v);
    if (!t->linear) {
        int i, j;
        const unsigned char *p;
        FLOOR_INT(u, i); FLOOR_INT(v, j);
        i = t->wmask >= 0 ? i & t->wmask : wrap_index(i, t->w, t->wrap_s);
        j = t->hmask >= 0 ? j & t->hmask : wrap_index(j, t->h, t->wrap_t);
        p = t->rgba + ((size_t)j * (size_t)t->w + (size_t)i) * 4u;
        out[0] = byte_to_float[p[0]]; out[1] = byte_to_float[p[1]]; out[2] = byte_to_float[p[2]]; out[3] = byte_to_float[p[3]];
    } else {
        int i0, j0, i1, j1, k;
        float fx, fy;
        const unsigned char *a, *b, *c, *d;
        u -= 0.5f; v -= 0.5f;
        FLOOR_INT(u, i0); FLOOR_INT(v, j0);
        fx = u - (float)i0; fy = v - (float)j0;
        if (t->wmask >= 0) { i1 = (i0 + 1) & t->wmask; i0 &= t->wmask; }
        else { i1 = wrap_index(i0 + 1, t->w, t->wrap_s); i0 = wrap_index(i0, t->w, t->wrap_s); }
        if (t->hmask >= 0) { j1 = (j0 + 1) & t->hmask; j0 &= t->hmask; }
        else { j1 = wrap_index(j0 + 1, t->h, t->wrap_t); j0 = wrap_index(j0, t->h, t->wrap_t); }
        a = t->rgba + ((size_t)j0 * (size_t)t->w + (size_t)i0) * 4u;
        b = t->rgba + ((size_t)j0 * (size_t)t->w + (size_t)i1) * 4u;
        c = t->rgba + ((size_t)j1 * (size_t)t->w + (size_t)i0) * 4u;
        d = t->rgba + ((size_t)j1 * (size_t)t->w + (size_t)i1) * 4u;
        for (k = 0; k < 4; k++) {
            float ta = byte_to_float[a[k]], tc2 = byte_to_float[c[k]];
            float top = ta + (byte_to_float[b[k]] - ta) * fx;
            float bottom = tc2 + (byte_to_float[d[k]] - tc2) * fx;
            out[k] = top + (bottom - top) * fy;
        }
    }
}

/* ------------------------------------------------------------ interpreter */
#ifdef __transputer__
static float vm_sqrt(float x) { return __fpsqrt(x); }
#else
static float vm_sqrt(float x) { return __builtin_sqrtf(x); }
#endif
/* GLSL pow is undefined for x < 0, and for x == 0 with y <= 0. Here, as
 * on GPUs that compute exp2(y*log2(x)) and then clamp, x < 0 behaves as 0. */
static float vm_pow(float x, float y)
{
    if (!(x > 0.0f)) return y > 0.0f ? 0.0f : y == 0.0f ? 1.0f : 3.0e38f;
    if (y >= 0.0f && y <= 1024.0f && (float)(int)y == y) {
        unsigned int n = (unsigned int)y;
        float r = 1.0f;
        while (n) { if (n & 1u) r *= x; x *= x; n >>= 1; }
        return r;
    }
    return powf(x, y);
}

/* Runs validated code. Results are computed into r[] before the store, as in
 * glsl_vm.c, so a destination may alias a source. normalize() of a zero
 * vector gives zero and division by zero follows IEEE: GLSL leaves both
 * undefined. */
static void vm_exec(float (*reg)[4], const tg_vm_instruction *code, int count, int entry, const sampler *const *tex)
{
    int pc, depth = 0, stack[8], i, j;
    float r[4], *a, *b, sum;
    for (pc = entry; pc < count; pc++) {
        const tg_vm_instruction *in = &code[pc];
        int w = in->width;
        a = reg[in->a]; b = reg[in->b];
        switch (in->op) {
        case TG_VM_MOV:
            if (in->flags & 1) { r[0] = r[1] = r[2] = r[3] = a[0]; } else { r[0] = a[0]; r[1] = a[1]; r[2] = a[2]; r[3] = a[3]; }
            break;
        case TG_VM_ADD: case TG_VM_SUB: case TG_VM_MUL: case TG_VM_DIV: case TG_VM_MAX: case TG_VM_POW: {
            float x[4], y[4];
            for (i = 0; i < w; i++) { x[i] = a[(in->flags & 1) ? 0 : i]; y[i] = b[(in->flags & 2) ? 0 : i]; }
            switch (in->op) {
            case TG_VM_ADD: for (i = 0; i < w; i++) r[i] = x[i] + y[i]; break;
            case TG_VM_SUB: for (i = 0; i < w; i++) r[i] = x[i] - y[i]; break;
            case TG_VM_MUL: for (i = 0; i < w; i++) r[i] = x[i] * y[i]; break;
            case TG_VM_DIV: for (i = 0; i < w; i++) r[i] = x[i] / y[i]; break;
            case TG_VM_MAX: for (i = 0; i < w; i++) r[i] = x[i] > y[i] ? x[i] : y[i]; break;
            default: for (i = 0; i < w; i++) r[i] = vm_pow(x[i], y[i]); break;
            }
            break;
        }
        case TG_VM_DOT:
            sum = a[0] * b[0];
            for (i = 1; i < w; i++) sum += a[i] * b[i];
            reg[in->dst][0] = sum;
            continue;
        case TG_VM_CROSS:
            r[0] = a[1] * b[2] - a[2] * b[1]; r[1] = a[2] * b[0] - a[0] * b[2]; r[2] = a[0] * b[1] - a[1] * b[0];
            break;
        case TG_VM_NORMALIZE:
            sum = a[0] * a[0];
            for (i = 1; i < w; i++) sum += a[i] * a[i];
            if (sum > 0.0f) { sum = 1.0f / vm_sqrt(sum); for (i = 0; i < w; i++) r[i] = a[i] * sum; }
            else for (i = 0; i < w; i++) r[i] = 0.0f;
            break;
        case TG_VM_TEXTURE2D:
            sample2d(tex[in->aux], a[0], a[1], r);
            break;
        case TG_VM_SWIZZLE:
            for (i = 0; i < w; i++) r[i] = a[(in->aux >> (2 * i)) & 3];
            break;
        case TG_VM_MAT4_VEC4:
            for (i = 0; i < 4; i++) {
                sum = reg[in->a][i] * b[0];
                for (j = 1; j < 4; j++) sum += reg[in->a + j][i] * b[j];
                r[i] = sum;
            }
            break;
        case TG_VM_SET_COMPONENT:
            reg[in->dst][in->aux] = a[0];
            continue;
        case TG_VM_GREATER:
            reg[in->dst][0] = a[0] > b[0] ? 1.0f : 0.0f;
            continue;
        case TG_VM_FRACT:
            for (i = 0; i < w; i++) {
                float v = a[i];
                if (v != v || v > 8388608.0f || v < -8388608.0f) r[i] = 0.0f;
                else r[i] = v - (float)floor_int(v);
            }
            break;
        case TG_VM_CALL:
            if (depth == 8) return;
            stack[depth++] = pc;
            pc = in->aux - 1;
            continue;
        case TG_VM_RETURN:
            if (!depth) return;
            pc = stack[--depth];
            continue;
        default:
            return;
        }
        for (i = 0; i < w; i++) reg[in->dst][i] = r[i];
    }
}

/* --------------------------------------------------- decoded interpreter */
/* At link time each stage's code is decoded once into scalar operations on
 * pointers into the program's register file: a vector operation becomes one
 * operation per component (broadcasts, swizzles and component writes become
 * plain moves), the helper functions are inlined at each call, and writes
 * nobody reads are dropped. Dot products, the matrix product, cross,
 * normalize and texture2D stay whole. Every value is computed with the same
 * float operations in the same order as vm_exec, so results are identical;
 * the per-fragment loop loses the decoding, the width loops and the calls. */
/* fast_exec's switch on these is a jump table (build.sh compiles this file
 * with -mjump-tables).  They are in order of use in dEngine's shaders for a
 * compiler without that option, whose chain of compares reaches the first
 * codes soonest. */
enum { F_MUL, F_ADD, F_DIV, F_SUB, F_MOV, F_FRACT, F_MAX, F_GT, F_POW,
       F_DOT2, F_DOT3, F_DOT4, F_CROSS, F_NORM2, F_NORM3, F_NORM4, F_TEX, F_MAT4 };

static fop *fast_emit(fast_code *fc, int op, float *d, float *a, float *b, int aux)
{
    fop *o;
    if (fc->count == fc->cap) {
        int cap = fc->cap ? fc->cap * 2 : 64;
        fop *ops = realloc(fc->ops, (size_t)cap * sizeof(fop));
        if (!ops) return 0;
        fc->ops = ops; fc->cap = cap;
    }
    o = &fc->ops[fc->count++];
    o->op = op; o->d = d; o->a = a; o->b = b; o->aux = aux;
    return o;
}

/* Component-wise operation: results go straight to d unless a later
 * component reads one that an earlier one writes (a broadcast or swizzle of
 * the destination), in which case they go through scratch first. */
static int fast_vector(fast_code *fc, int op, float *d, float *const *src_a, float *const *src_b, int w)
{
    int i, j, hazard = 0;
    for (i = 0; i < w && !hazard; i++)
        for (j = i + 1; j < w; j++)
            if (src_a[j] == d + i || (src_b && src_b[j] == d + i)) { hazard = 1; break; }
    for (i = 0; i < w; i++) {
        float *out = hazard ? fc->scratch + i : d + i;
        if (op == F_MOV && out == src_a[i]) continue;
        if (!fast_emit(fc, op, out, src_a[i], src_b ? src_b[i] : 0, 0)) return 0;
    }
    if (hazard)
        for (i = 0; i < w; i++) if (!fast_emit(fc, F_MOV, d + i, fc->scratch + i, 0, 0)) return 0;
    return 1;
}

static int decode_range(fast_code *fc, const tg_glsl_fragment_program *ir, float (*reg)[4], int pc, int depth)
{
    for (; pc < ir->instruction_count; pc++) {
        const tg_vm_instruction *in = &ir->code[pc];
        float *d = reg[in->dst], *a = reg[in->a], *b = reg[in->b], *sa[4], *sb[4];
        int w = in->width, i, op;
        switch (in->op) {
        case TG_VM_RETURN:
            return depth ? 1 : 0;
        case TG_VM_CALL:
            if (depth == 8 || !decode_range(fc, ir, reg, in->aux, depth + 1)) return 0;
            continue;
        case TG_VM_MOV:
            for (i = 0; i < w; i++) sa[i] = a + ((in->flags & 1) ? 0 : i);
            if (!fast_vector(fc, F_MOV, d, sa, 0, w)) return 0;
            continue;
        case TG_VM_ADD: case TG_VM_SUB: case TG_VM_MUL: case TG_VM_DIV: case TG_VM_MAX: case TG_VM_POW:
            op = in->op == TG_VM_ADD ? F_ADD : in->op == TG_VM_SUB ? F_SUB : in->op == TG_VM_MUL ? F_MUL :
                 in->op == TG_VM_DIV ? F_DIV : in->op == TG_VM_MAX ? F_MAX : F_POW;
            for (i = 0; i < w; i++) { sa[i] = a + ((in->flags & 1) ? 0 : i); sb[i] = b + ((in->flags & 2) ? 0 : i); }
            if (!fast_vector(fc, op, d, sa, sb, w)) return 0;
            continue;
        case TG_VM_FRACT:
            for (i = 0; i < w; i++) sa[i] = a + i;
            if (!fast_vector(fc, F_FRACT, d, sa, 0, w)) return 0;
            continue;
        case TG_VM_SWIZZLE:
            for (i = 0; i < w; i++) sa[i] = a + ((in->aux >> (2 * i)) & 3);
            if (!fast_vector(fc, F_MOV, d, sa, 0, w)) return 0;
            continue;
        case TG_VM_SET_COMPONENT:
            if (d + in->aux != a && !fast_emit(fc, F_MOV, d + in->aux, a, 0, 0)) return 0;
            continue;
        case TG_VM_GREATER: op = F_GT; break;
        case TG_VM_DOT: op = w == 2 ? F_DOT2 : w == 3 ? F_DOT3 : w == 4 ? F_DOT4 : F_MUL; break;
        case TG_VM_CROSS: op = F_CROSS; break;
        case TG_VM_NORMALIZE: op = w == 2 ? F_NORM2 : w == 3 ? F_NORM3 : w == 4 ? F_NORM4 : -1; break;
        case TG_VM_TEXTURE2D: op = F_TEX; break;
        case TG_VM_MAT4_VEC4: op = F_MAT4; break;
        default: return 0;
        }
        if (op < 0) {
            /* normalize() of a scalar: sign, or 0 */
            if (!fast_emit(fc, F_NORM2, d, a, 0, 1)) return 0;
        } else if (!fast_emit(fc, op, d, a, b, in->aux)) return 0;
    }
    return !depth;
}

/* What an operation reads and writes, as float offsets: the register file's
 * 640 floats, then the 4 scratch floats (FAST_SLOTS). */
static int fast_slot(const fast_code *fc, float (*reg)[4], const float *p)
{
    if (p >= fc->scratch && p < fc->scratch + 4) return TG_VM_REGS * 4 + (int)(p - fc->scratch);
    return (int)(p - &reg[0][0]);
}
static int fast_writes(const fop *o, int *n)
{
    switch (o->op) {
    case F_CROSS: *n = 3; break;
    case F_NORM2: *n = o->aux == 1 ? 1 : 2; break;
    case F_NORM3: *n = 3; break;
    case F_NORM4: case F_TEX: case F_MAT4: *n = 4; break;
    default: *n = 1;
    }
    return 1;
}
static int fast_reads(const fop *o, const float **p, int *n)
{
    int k = 0, i;
    switch (o->op) {
    case F_MOV: case F_FRACT: p[k++] = o->a; break;
    case F_DOT2: case F_DOT3: case F_DOT4: {
        int w = o->op == F_DOT2 ? 2 : o->op == F_DOT3 ? 3 : 4;
        for (i = 0; i < w; i++) { p[k++] = o->a + i; p[k++] = o->b + i; }
        break;
    }
    case F_CROSS: for (i = 0; i < 3; i++) { p[k++] = o->a + i; p[k++] = o->b + i; } break;
    case F_NORM2: case F_NORM3: case F_NORM4: {
        int w = o->op == F_NORM2 ? (o->aux == 1 ? 1 : 2) : o->op == F_NORM3 ? 3 : 4;
        for (i = 0; i < w; i++) p[k++] = o->a + i;
        break;
    }
    case F_TEX: p[k++] = o->a; p[k++] = o->a + 1; break;
    case F_MAT4: for (i = 0; i < 16; i++) p[k++] = o->a + i; for (i = 0; i < 4; i++) p[k++] = o->b + i; break;
    default: p[k++] = o->a; p[k++] = o->b; break;
    }
    *n = k;
    return 1;
}

/* Drops operations whose results nothing reads: live at the end are the
 * outputs and anything read before it is written (a value carried from the
 * previous invocation, which this keeps exactly as it was). */
static void fast_dead_code(fast_code *fc, float (*reg)[4], const unsigned char *outputs)
{
    unsigned char *live = calloc(FAST_SLOTS, 1), *written = calloc(FAST_SLOTS, 1);
    const float *reads[24];
    int i, j, k, n, m, keep_count = 0;
    if (!live || !written) { free(live); free(written); return; }
    for (i = 0; i < FAST_SLOTS; i++) live[i] = outputs[i];
    for (i = 0; i < fc->count; i++) {
        fast_reads(&fc->ops[i], reads, &n);
        for (k = 0; k < n; k++) { int s = fast_slot(fc, reg, reads[k]); if (!written[s]) live[s] = 1; }
        fast_writes(&fc->ops[i], &m);
        for (k = 0; k < m; k++) written[fast_slot(fc, reg, fc->ops[i].d + k)] = 1;
    }
    for (i = fc->count - 1; i >= 0; i--) {
        fop *o = &fc->ops[i];
        int needed = 0;
        fast_writes(o, &m);
        for (k = 0; k < m; k++) if (live[fast_slot(fc, reg, o->d + k)]) needed = 1;
        if (!needed) { o->op = -1; continue; }
        for (k = 0; k < m; k++) live[fast_slot(fc, reg, o->d + k)] = 0;
        fast_reads(o, reads, &n);
        for (k = 0; k < n; k++) live[fast_slot(fc, reg, reads[k])] = 1;
    }
    for (i = 0, j = 0; i < fc->count; i++) if (fc->ops[i].op >= 0) { fc->ops[j++] = fc->ops[i]; keep_count++; }
    fc->count = keep_count;
    free(live); free(written);
}

/* One float in and one out: operands that can be redirected one by one. */
static int fast_scalar(int op)
{
    return op == F_MOV || op == F_ADD || op == F_SUB || op == F_MUL || op == F_DIV ||
           op == F_MAX || op == F_POW || op == F_GT || op == F_FRACT;
}
static int fast_single_output(int op) { return fast_scalar(op) || op == F_DOT2 || op == F_DOT3 || op == F_DOT4; }
static int fast_op_reads(const fast_code *fc, float (*reg)[4], const fop *o, int slot)
{
    const float *reads[24];
    int n, k;
    if (o->op < 0) return 0;
    fast_reads(o, reads, &n);
    for (k = 0; k < n; k++) if (fast_slot(fc, reg, reads[k]) == slot) return 1;
    return 0;
}
static int fast_op_writes(const fast_code *fc, float (*reg)[4], const fop *o, int slot)
{
    int m, k;
    if (o->op < 0) return 0;
    fast_writes(o, &m);
    for (k = 0; k < m; k++) if (fast_slot(fc, reg, o->d + k) == slot) return 1;
    return 0;
}

/* Is the value in `slot` after op i read again (before it is rewritten,
 * or at the end, where `live_out` says what is read)? */
static int fast_read_after(const fast_code *fc, float (*reg)[4], int i, int slot, const unsigned char *live_out)
{
    int j;
    for (j = i + 1; j < fc->count; j++) {
        if (fast_op_reads(fc, reg, &fc->ops[j], slot)) return 1;
        if (fast_op_writes(fc, reg, &fc->ops[j], slot)) return 0;
    }
    return live_out[slot];
}

/* The compiler parks every expression in a temporary and moves it to its
 * variable, and swizzles copy components: most decoded operations are
 * moves. For `mov d <- s`: if s was made by a single-output operation k
 * and has no other reader, k writes d itself; otherwise later scalar
 * operations read s in place of d while neither is rewritten. Only where
 * intermediate values are kept changes, so results stay the same. */
static void fast_copy_propagate(fast_code *fc, float (*reg)[4], const unsigned char *live_out)
{
    int i, j, k;
    for (i = 0; i < fc->count; i++) {
        fop *mv = &fc->ops[i];
        int d, src, ok;
        if (mv->op != F_MOV) continue;
        d = fast_slot(fc, reg, mv->d); src = fast_slot(fc, reg, mv->a);
        if (d == src) { mv->op = -1; continue; }
        for (k = i - 1; k >= 0 && !fast_op_writes(fc, reg, &fc->ops[k], src); k--) ;
        if (k >= 0 && fast_single_output(fc->ops[k].op) && !fast_read_after(fc, reg, i, src, live_out)) {
            ok = 1;
            for (j = k + 1; j < i && ok; j++)
                if (fast_op_reads(fc, reg, &fc->ops[j], src) || fast_op_reads(fc, reg, &fc->ops[j], d) ||
                    fast_op_writes(fc, reg, &fc->ops[j], d)) ok = 0;
            if (ok) { fc->ops[k].d = mv->d; mv->op = -1; continue; }
        }
        for (j = i + 1; j < fc->count; j++) {
            fop *o = &fc->ops[j];
            if (o->op < 0) continue;
            if (fast_scalar(o->op)) {
                if (o->a == mv->d) o->a = mv->a;
                if (o->op != F_MOV && o->op != F_FRACT && o->b == mv->d) o->b = mv->a;
            }
            if (fast_op_writes(fc, reg, o, d) || fast_op_writes(fc, reg, o, src)) break;
        }
    }
    for (i = 0, j = 0; i < fc->count; i++) if (fc->ops[i].op >= 0) fc->ops[j++] = fc->ops[i];
    fc->count = j;
}

/* Live at the end: the outputs, and whatever is read before it is written
 * (a value carried over from the previous invocation). */
static unsigned char *fast_live_out(fast_code *fc, float (*reg)[4], const unsigned char *outputs)
{
    unsigned char *live = calloc(FAST_SLOTS, 1), *written = calloc(FAST_SLOTS, 1);
    const float *reads[24];
    int i, k, n, m;
    if (!live || !written) { free(live); free(written); return 0; }
    for (i = 0; i < FAST_SLOTS; i++) live[i] = outputs[i];
    for (i = 0; i < fc->count; i++) {
        fast_reads(&fc->ops[i], reads, &n);
        for (k = 0; k < n; k++) { int sl = fast_slot(fc, reg, reads[k]); if (!written[sl]) live[sl] = 1; }
        fast_writes(&fc->ops[i], &m);
        for (k = 0; k < m; k++) written[fast_slot(fc, reg, fc->ops[i].d + k)] = 1;
    }
    free(written);
    return live;
}

static int decode_program(fast_code *fc, const tg_glsl_fragment_program *ir, float (*reg)[4], const unsigned char *outputs)
{
    unsigned char *live_out;
    fc->count = 0;
    if (!decode_range(fc, ir, reg, ir->entry_pc, 0)) return 0;
    fast_dead_code(fc, reg, outputs);
    live_out = fast_live_out(fc, reg, outputs);
    if (live_out) {
#ifndef DE_GL_NO_COPY_PROPAGATION
        fast_copy_propagate(fc, reg, live_out);
#endif
        free(live_out);
        fast_dead_code(fc, reg, outputs);
    }
    return 1;
}

static void fast_exec(const fast_code *fc, const sampler *const *tex)
{
    const fop *o = fc->ops, *end = o + fc->count;
    for (; o < end; o++) {
        float *d = o->d, *a = o->a, *b = o->b;
        switch (o->op) {
        case F_MOV: *d = *a; break;
        case F_ADD: *d = *a + *b; break;
        case F_SUB: *d = *a - *b; break;
        case F_MUL: *d = *a * *b; break;
        case F_DIV: *d = *a / *b; break;
        case F_MAX: *d = *a > *b ? *a : *b; break;
        case F_POW: *d = vm_pow(*a, *b); break;
        case F_GT: *d = *a > *b ? 1.0f : 0.0f; break;
        case F_FRACT: {
            float v = *a;
            *d = (v != v || v > 8388608.0f || v < -8388608.0f) ? 0.0f : v - (float)floor_int(v);
            break;
        }
        case F_DOT2: *d = a[0] * b[0] + a[1] * b[1]; break;
        case F_DOT3: *d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; break;
        case F_DOT4: *d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; break;
        case F_CROSS: {
            float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
            d[0] = x; d[1] = y; d[2] = z;
            break;
        }
        case F_NORM2: case F_NORM3: case F_NORM4: {
            int w = o->op == F_NORM2 ? (o->aux == 1 ? 1 : 2) : o->op == F_NORM3 ? 3 : 4, i;
            float sum = a[0] * a[0];
            for (i = 1; i < w; i++) sum += a[i] * a[i];
            if (sum > 0.0f) { sum = 1.0f / vm_sqrt(sum); for (i = 0; i < w; i++) d[i] = a[i] * sum; }
            else for (i = 0; i < w; i++) d[i] = 0.0f;
            break;
        }
        case F_TEX: { float r[4]; sample2d(tex[o->aux], a[0], a[1], r); d[0] = r[0]; d[1] = r[1]; d[2] = r[2]; d[3] = r[3]; break; }
        case F_MAT4: {
            float r0 = a[0] * b[0] + a[4] * b[1] + a[8] * b[2] + a[12] * b[3];
            float r1 = a[1] * b[0] + a[5] * b[1] + a[9] * b[2] + a[13] * b[3];
            float r2 = a[2] * b[0] + a[6] * b[1] + a[10] * b[2] + a[14] * b[3];
            float r3 = a[3] * b[0] + a[7] * b[1] + a[11] * b[2] + a[15] * b[3];
            d[0] = r0; d[1] = r1; d[2] = r2; d[3] = r3;
            break;
        }
        /* fast_emit writes no other code: a jump table needs no range test */
        default: __builtin_unreachable();
        }
    }
}

/* ---------------------------------------------------------------- drawing */
typedef struct { float x, y, z, iw, v[MAX_VARYING_FLOATS]; } window_vertex;

static struct {
    program *p;
    target t;
    const sampler *vtex[TG_VM_TEXTURES], *ftex[TG_VM_TEXTURES];
    float *vary_dst[MAX_VARYING_FLOATS];   /* where each varying goes in the fragment registers */
    int nv, stride;
    int clip_x0, clip_y0, clip_x1, clip_y1;
    float vx, vy, vw, vh;
    /* Vertex cache, indexed by vertex number. */
    float *cache;
    unsigned int *stamp, *full_stamp, *outcode;
    unsigned int cache_cap, generation;
    /* prefix[y] = owned rows below y, for the target's `owned` mask */
    int *prefix, prefix_rows;
    const unsigned char *prefix_mask;
} draw;

static const sampler *unit_sampler(int unit)
{
    texture *t = table_get(&gl.textures, gl.unit_texture[unit]);
    if (!t || !t->complete) return &black_texture_object.s;
    return &t->s;
}

static float fetch_component(GLenum type, GLboolean normalized, const unsigned char *p, int k)
{
    switch (type) {
    case GL_FLOAT: { float v; memcpy(&v, p + 4 * k, 4); return v; }
    case GL_SHORT: { short v; memcpy(&v, p + 2 * k, 2); return normalized ? ((float)v * 2.0f + 1.0f) / 65535.0f : (float)v; }
    case GL_UNSIGNED_SHORT: { unsigned short v; memcpy(&v, p + 2 * k, 2); return normalized ? (float)v / 65535.0f : (float)v; }
    case GL_BYTE: { signed char v = (signed char)p[k]; return normalized ? ((float)v * 2.0f + 1.0f) / 255.0f : (float)v; }
    case GL_UNSIGNED_BYTE: return normalized ? (float)p[k] / 255.0f : (float)p[k];
    default: { int v; memcpy(&v, p + 4 * k, 4); return (float)v / 65536.0f; }   /* GL_FIXED */
    }
}
static int type_size(GLenum type) { return type == GL_FLOAT || type == GL_FIXED ? 4 : type == GL_SHORT || type == GL_UNSIGNED_SHORT ? 2 : 1; }

static unsigned int clip_outcode(const float *c)
{
    unsigned int code = 0;
    if (c[0] < -c[3]) code |= 1; if (c[0] > c[3]) code |= 2;
    if (c[1] < -c[3]) code |= 4; if (c[1] > c[3]) code |= 8;
    if (c[2] < -c[3]) code |= 16; if (c[2] > c[3]) code |= 32;
    return code;
}

static float *shade_vertex(unsigned int index);
static void fetch_attributes(program *p, unsigned int index)
{
    int i, k;
    for (i = 0; i < p->attrib_count; i++) {
        const attrib_info *ai = &p->attribs[i];
        const attrib_array *a = &gl.arrays[ai->location];
        float *dst = p->vregs[ai->reg];
        if (a->enabled) {
            const unsigned char *base;
            size_t stride = a->stride ? (size_t)a->stride : (size_t)(a->size * type_size(a->type));
            if (a->buffer) {
                buffer *b = table_get(&gl.buffers, a->buffer);
                base = b && b->data ? b->data + (size_t)a->pointer : 0;
            } else base = a->pointer;
            if (base) {
                const unsigned char *src = base + stride * index;
                dst[0] = 0.0f; dst[1] = 0.0f; dst[2] = 0.0f; dst[3] = 1.0f;
                for (k = 0; k < a->size; k++) dst[k] = fetch_component(a->type, a->normalized, src, k);
                continue;
            }
        }
        for (k = 0; k < 4; k++) dst[k] = gl.generic[ai->location][k];
    }
}

/* A vertex's clip position and outcode, from the position slice of the
 * program when that pays (vpos_split), else from the whole program. */
static float *vertex_position(unsigned int index)
{
    float *out = draw.cache + (size_t)index * (size_t)draw.stride;
    program *p = draw.p;
    int k;
    if (draw.stamp[index] == draw.generation) return out;
    draw.stamp[index] = draw.generation;
    if (!p->vpos_split) return shade_vertex(index);
    fetch_attributes(p, index);
    fast_exec(&p->vpos, draw.vtex);
    for (k = 0; k < 4; k++) out[k] = p->vregs[p->vs_out][k];
    draw.outcode[index] = clip_outcode(out);
    return out;
}

/* Every output of a vertex. The position the whole program computes is
 * the one the slice computed: the same operations on the same inputs. */
static float *shade_vertex(unsigned int index)
{
    float *out = draw.cache + (size_t)index * (size_t)draw.stride;
    program *p = draw.p;
    int i, k;
    if (draw.full_stamp[index] == draw.generation) return out;
    draw.full_stamp[index] = draw.stamp[index] = draw.generation;
    fetch_attributes(p, index);
#ifdef DE_GL_SLOW_VM
    vm_exec(p->vregs, p->vs->code, p->vs->instruction_count, p->vs->entry_pc, draw.vtex);
#else
    fast_exec(&p->vfast, draw.vtex);
#endif
    for (k = 0; k < 4; k++) out[k] = p->vregs[p->vs_out][k];
    for (k = 0; k < draw.nv; k++) out[4 + k] = p->vregs[p->vary_vreg[k]][p->vary_comp[k]];
    draw.outcode[index] = clip_outcode(out);
#ifdef DE_GL_TRACE
    {
        static int traced;
        if (gl.stats.draws <= 2 && traced < 6) {
            traced++;
            printf("[gltrace] draw %lu vertex %u:", gl.stats.draws, index);
            for (i = 0; i < p->attrib_count; i++)
                printf(" %s(%g %g %g %g)", p->attribs[i].name, (double)p->vregs[p->attribs[i].reg][0], (double)p->vregs[p->attribs[i].reg][1],
                       (double)p->vregs[p->attribs[i].reg][2], (double)p->vregs[p->attribs[i].reg][3]);
            printf(" -> clip(%g %g %g %g) code %u\n", (double)out[0], (double)out[1], (double)out[2], (double)out[3], draw.outcode[index]);
            if (traced == 1) {
                int r, c;
                for (r = 0; r < p->vs->symbol_count; r++)
                    if (p->vs->symbols[r].kind == TG_GLSL_UNIFORM && p->vs->symbols[r].width == 16) {
                        printf("[gltrace] %s:", p->vs->symbols[r].name);
                        for (c = 0; c < 16; c++) printf(" %g", (double)p->vregs[p->vs->symbols[r].slot + c / 4][c % 4]);
                        printf("\n");
                    }
            }
        }
    }
#endif
    gl.stats.vertices++;
    return out;
}

static unsigned char to_byte(float c) { if (!(c > 0.0f)) return 0; if (c >= 1.0f) return 255; return (unsigned char)(int)(c * 255.0f + 0.5f); }
/* to_byte(clampf01(c)), inline for the unblended write */
#define TO_BYTE(c) ((unsigned char)(!((c) > 0.0f) ? 0 : (c) >= 1.0f ? 255 : (int)((c) * 255.0f + 0.5f)))

static float blend_term(GLenum f, const float *s, const float *d, int k)
{
    switch (f) {
    case GL_ZERO: return 0.0f;
    case GL_ONE: return 1.0f;
    case GL_SRC_COLOR: return s[k];
    case GL_ONE_MINUS_SRC_COLOR: return 1.0f - s[k];
    case GL_DST_COLOR: return d[k];
    case GL_ONE_MINUS_DST_COLOR: return 1.0f - d[k];
    case GL_SRC_ALPHA: return s[3];
    case GL_ONE_MINUS_SRC_ALPHA: return 1.0f - s[3];
    case GL_DST_ALPHA: return d[3];
    case GL_ONE_MINUS_DST_ALPHA: return 1.0f - d[3];
    case GL_CONSTANT_COLOR: return gl.blend_color[k];
    case GL_ONE_MINUS_CONSTANT_COLOR: return 1.0f - gl.blend_color[k];
    case GL_CONSTANT_ALPHA: return gl.blend_color[3];
    case GL_ONE_MINUS_CONSTANT_ALPHA: return 1.0f - gl.blend_color[3];
    default: { float f1 = s[3], f2 = 1.0f - d[3]; return k == 3 ? 1.0f : f1 < f2 ? f1 : f2; }   /* SRC_ALPHA_SATURATE */
    }
}

static void write_pixel(unsigned char *dst, const float *color)
{
    float s[4], d[4], o[4];
    int k;
    for (k = 0; k < 4; k++) s[k] = clampf01(color[k]);
    if (gl.blend) {
        for (k = 0; k < 4; k++) d[k] = byte_to_float[dst[k]];
        for (k = 0; k < 4; k++) {
            GLenum sf = k < 3 ? gl.blend_src_rgb : gl.blend_src_alpha, df = k < 3 ? gl.blend_dst_rgb : gl.blend_dst_alpha;
            GLenum eq = k < 3 ? gl.blend_eq_rgb : gl.blend_eq_alpha;
            float a = s[k] * blend_term(sf, s, d, k), b = d[k] * blend_term(df, s, d, k);
            o[k] = eq == GL_FUNC_ADD ? a + b : eq == GL_FUNC_SUBTRACT ? a - b : b - a;
        }
    } else for (k = 0; k < 4; k++) o[k] = s[k];
    for (k = 0; k < 4; k++) if (gl.color_mask[k]) dst[k] = to_byte(o[k]);
}

static int depth_pass(float z, float stored)
{
    switch (gl.depth_func) {
    case GL_LESS: return z < stored;
    case GL_LEQUAL: return z <= stored;
    case GL_GREATER: return z > stored;
    case GL_GEQUAL: return z >= stored;
    case GL_EQUAL: return z == stored;
    case GL_NOTEQUAL: return z != stored;
    case GL_ALWAYS: return 1;
    default: return 0;
    }
}

static int ceil_int(float v) { int i = (int)v; return (float)i < v ? i + 1 : i; }

static void raster_triangle(const window_vertex *a0, const window_vertex *b0, const window_vertex *c0)
{
    const window_vertex *v0 = a0, *v1 = b0, *v2 = c0, *tmp;
    float area, inv, dx1, dy1, dx2, dy2, s02, s01, s12;
    float zx, zy, wx, wy, vx[MAX_VARYING_FLOATS], vy[MAX_VARYING_FLOATS];
    int nv = draw.nv, k, j, jmin, jmax, front;
    program *p = draw.p;
    int plain_write = !gl.blend && gl.color_mask[0] && gl.color_mask[1] && gl.color_mask[2] && gl.color_mask[3];
    int depth_on = gl.depth_test && draw.t.depth != 0, depth_less = gl.depth_func == GL_LESS;
    dx1 = b0->x - a0->x; dy1 = b0->y - a0->y; dx2 = c0->x - a0->x; dy2 = c0->y - a0->y;
    area = dx1 * dy2 - dx2 * dy1;
    if (!(area > 0.0f) && !(area < 0.0f)) return;
    front = (area > 0.0f) == (gl.front_face == GL_CCW);
    if (gl.cull && (gl.cull_face == GL_FRONT_AND_BACK || (gl.cull_face == GL_FRONT) == front)) { gl.stats.culled++; return; }
    gl.stats.triangles++;
    /* Plane gradients, relative to a0. */
    inv = 1.0f / area;
    zx = ((b0->z - a0->z) * dy2 - (c0->z - a0->z) * dy1) * inv;
    zy = ((c0->z - a0->z) * dx1 - (b0->z - a0->z) * dx2) * inv;
    wx = ((b0->iw - a0->iw) * dy2 - (c0->iw - a0->iw) * dy1) * inv;
    wy = ((c0->iw - a0->iw) * dx1 - (b0->iw - a0->iw) * dx2) * inv;
    for (k = 0; k < nv; k++) {
        float d1 = b0->v[k] - a0->v[k], d2 = c0->v[k] - a0->v[k];
        vx[k] = (d1 * dy2 - d2 * dy1) * inv;
        vy[k] = (d2 * dx1 - d1 * dx2) * inv;
    }
    /* Edges, each computed from its lower end so neighbours agree. */
    if (v1->y < v0->y) { tmp = v0; v0 = v1; v1 = tmp; }
    if (v2->y < v1->y) { tmp = v1; v1 = v2; v2 = tmp; }
    if (v1->y < v0->y) { tmp = v0; v0 = v1; v1 = tmp; }
    if (!(v2->y > v0->y)) return;
    s02 = (v2->x - v0->x) / (v2->y - v0->y);
    s01 = v1->y > v0->y ? (v1->x - v0->x) / (v1->y - v0->y) : 0.0f;
    s12 = v2->y > v1->y ? (v2->x - v1->x) / (v2->y - v1->y) : 0.0f;
    jmin = ceil_int(v0->y - 0.5f); jmax = ceil_int(v2->y - 0.5f);
    if (jmin < draw.clip_y0) jmin = draw.clip_y0;
    if (jmax > draw.clip_y1) jmax = draw.clip_y1;
    for (j = jmin; j < jmax; j++) {
        float yc = (float)j + 0.5f, xa, xb, left, right, xc, ddx, ddy, z, iw, q[MAX_VARYING_FLOATS];
        int i, i0, i1;
        unsigned char *color;
        float *depth;
        if (draw.t.owned && !draw.t.owned[j]) continue;
        xa = v0->x + (yc - v0->y) * s02;
        xb = yc < v1->y ? v0->x + (yc - v0->y) * s01 : v1->x + (yc - v1->y) * s12;
        left = xa < xb ? xa : xb; right = xa < xb ? xb : xa;
        i0 = ceil_int(left - 0.5f); i1 = ceil_int(right - 0.5f);
        if (i0 < draw.clip_x0) i0 = draw.clip_x0;
        if (i1 > draw.clip_x1) i1 = draw.clip_x1;
        if (i0 >= i1) continue;
        xc = (float)i0 + 0.5f; ddx = xc - a0->x; ddy = yc - a0->y;
        z = a0->z + zx * ddx + zy * ddy;
        iw = a0->iw + wx * ddx + wy * ddy;
        for (k = 0; k < nv; k++) q[k] = a0->v[k] + vx[k] * ddx + vy[k] * ddy;
        color = draw.t.color ? draw.t.color + ((size_t)j * (size_t)draw.t.width + (size_t)i0) * 4u : 0;
        depth = draw.t.depth ? draw.t.depth + (size_t)j * (size_t)draw.t.width + (size_t)i0 : 0;
        for (i = i0; i < i1; i++) {
            float zc = z < 0.0f ? 0.0f : z > 1.0f ? 1.0f : z;
            gl.stats.fragments++;
            if (!depth_on || (depth_less ? zc < *depth : depth_pass(zc, *depth))) {
                float w = 1.0f / iw, *out;
                if (depth_on && gl.depth_mask) *depth = zc;
                for (k = 0; k < nv; k++) *draw.vary_dst[k] = q[k] * w;
#ifdef DE_GL_SLOW_VM
                vm_exec(p->fregs, p->fs->code, p->fs->instruction_count, p->fs->entry_pc, draw.ftex);
#else
                fast_exec(&p->ffast, draw.ftex);
#endif
                out = p->fregs[p->fs_out];
                if (color) {
                    if (plain_write) { color[0] = TO_BYTE(out[0]); color[1] = TO_BYTE(out[1]); color[2] = TO_BYTE(out[2]); color[3] = TO_BYTE(out[3]); }
                    else write_pixel(color, out);
                }
                gl.stats.shaded++;
            } else gl.stats.depth_rejected++;
            z += zx; iw += wx;
            for (k = 0; k < nv; k++) q[k] += vx[k];
            if (color) color += 4;
            if (depth) depth++;
        }
    }
}

static void to_window(const float *c, window_vertex *o)
{
    float iw = 1.0f / c[3];
    int k;
    o->x = draw.vx + (c[0] * iw + 1.0f) * draw.vw;
    o->y = draw.vy + (c[1] * iw + 1.0f) * draw.vh;
    o->z = gl.depth_near + (gl.depth_far - gl.depth_near) * ((c[2] * iw + 1.0f) * 0.5f);
    o->iw = iw;
    for (k = 0; k < draw.nv; k++) o->v[k] = c[4 + k] * iw;
}

static float plane_distance(const float *c, int plane)
{
    switch (plane) {
    case 0: return c[3] + c[0];
    case 1: return c[3] - c[0];
    case 2: return c[3] + c[1];
    case 3: return c[3] - c[1];
    case 4: return c[3] + c[2];
    default: return c[3] - c[2];
    }
}

#define CLIP_MAX 12
static void clip_and_draw(const float *a, const float *b, const float *c, unsigned int codes_or)
{
    float store[2][CLIP_MAX][4 + MAX_VARYING_FLOATS];
    int count = 3, n = 4 + draw.nv, plane, i, k, from = 0;
    window_vertex w[CLIP_MAX];
    memcpy(store[0][0], a, sizeof(float) * (size_t)n);
    memcpy(store[0][1], b, sizeof(float) * (size_t)n);
    memcpy(store[0][2], c, sizeof(float) * (size_t)n);
    gl.stats.clipped++;
    for (plane = 0; plane < 6; plane++) {
        int out = 0, to = from ^ 1;
        if (!(codes_or & (1u << plane))) continue;
        for (i = 0; i < count; i++) {
            const float *p = store[from][i], *q = store[from][(i + 1) % count];
            float dp = plane_distance(p, plane), dq = plane_distance(q, plane);
            if (dp >= 0.0f) { if (out < CLIP_MAX) memcpy(store[to][out++], p, sizeof(float) * (size_t)n); }
            if ((dp >= 0.0f) != (dq >= 0.0f) && out < CLIP_MAX) {
                /* Interpolate from the inside end, so a shared edge clips to
                 * the same point whichever triangle it belongs to. */
                const float *in = dp >= 0.0f ? p : q, *ex = dp >= 0.0f ? q : p;
                float di = dp >= 0.0f ? dp : dq, de = dp >= 0.0f ? dq : dp, t = di / (di - de);
                for (k = 0; k < n; k++) store[to][out][k] = in[k] + t * (ex[k] - in[k]);
                out++;
            }
        }
        count = out; from = to;
        if (count < 3) return;
    }
    for (i = 0; i < count; i++) {
        if (!(store[from][i][3] > 0.0f)) return;
        to_window(store[from][i], &w[i]);
    }
    for (i = 1; i + 1 < count; i++) raster_triangle(&w[0], &w[i], &w[i + 1]);
}

static float window_x(const float *c) { float iw = 1.0f / c[3]; return draw.vx + (c[0] * iw + 1.0f) * draw.vw; }
static float window_y(const float *c) { float iw = 1.0f / c[3]; return draw.vy + (c[1] * iw + 1.0f) * draw.vh; }

static void draw_triangle(unsigned int ia, unsigned int ib, unsigned int ic)
{
    float *a = vertex_position(ia), *b = vertex_position(ib), *c = vertex_position(ic);
    unsigned int oa = draw.outcode[ia], ob = draw.outcode[ib], oc = draw.outcode[ic];
    if (oa & ob & oc) return;
    if (!(oa | ob | oc)) {
        /* Before the rest of the vertex program runs: a triangle that is
         * culled, or covers none of this context's rows, draws nothing.
         * The tests repeat raster_triangle's own arithmetic on the same
         * values (to_window's x and y), so they decide as it would. */
        float ax = window_x(a), ay = window_y(a), bx = window_x(b), by = window_y(b), cx = window_x(c), cy = window_y(c);
        float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
        int front, jmin, jmax;
        if (!(area > 0.0f) && !(area < 0.0f)) return;
        front = (area > 0.0f) == (gl.front_face == GL_CCW);
        if (gl.cull && (gl.cull_face == GL_FRONT_AND_BACK || (gl.cull_face == GL_FRONT) == front)) { gl.stats.culled++; return; }
        if (draw.t.owned) {
            float ymin = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy);
            float ymax = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);
            jmin = ceil_int(ymin - 0.5f); jmax = ceil_int(ymax - 0.5f);
            if (jmin < draw.clip_y0) jmin = draw.clip_y0;
            if (jmax > draw.clip_y1) jmax = draw.clip_y1;
            if (jmin >= jmax || draw.prefix[jmax] == draw.prefix[jmin]) return;
        }
    }
    a = shade_vertex(ia); b = shade_vertex(ib); c = shade_vertex(ic);
    if (!(oa | ob | oc)) {
        window_vertex w[3];
        to_window(a, &w[0]); to_window(b, &w[1]); to_window(c, &w[2]);
        raster_triangle(&w[0], &w[1], &w[2]);
    } else clip_and_draw(a, b, c, oa | ob | oc);
}

static int begin_draw(GLenum mode, unsigned int vertices)
{
    program *p;
    int i;
    if (mode > GL_TRIANGLE_FAN) { set_error(GL_INVALID_ENUM); return 0; }
    p = table_get(&gl.programs, gl.program);
    if (!p || !p->linked) { set_error(GL_INVALID_OPERATION); return 0; }
    if (!current_target(&draw.t)) return 0;
    if (mode < GL_TRIANGLES) return 0;   /* points and lines are not drawn */
    draw.p = p; draw.nv = p->varying_floats; draw.stride = 4 + draw.nv;
    for (i = 0; i < draw.nv; i++) draw.vary_dst[i] = &p->fregs[p->vary_freg[i]][p->vary_comp[i]];
    for (i = 0; i < TG_VM_TEXTURES; i++) draw.vtex[i] = draw.ftex[i] = &black_texture_object.s;
    for (i = 0; i < p->uniform_count; i++) {
        const uniform_info *u = &p->uniforms[i];
        if (u->vsampler >= 0) draw.vtex[u->vsampler] = unit_sampler(u->unit);
        if (u->fsampler >= 0) draw.ftex[u->fsampler] = unit_sampler(u->unit);
    }
    draw.clip_x0 = 0; draw.clip_y0 = 0; draw.clip_x1 = draw.t.width; draw.clip_y1 = draw.t.height;
    if (gl.scissor_test) {
        if (gl.scissor[0] > draw.clip_x0) draw.clip_x0 = gl.scissor[0];
        if (gl.scissor[1] > draw.clip_y0) draw.clip_y0 = gl.scissor[1];
        if (gl.scissor[0] + gl.scissor[2] < draw.clip_x1) draw.clip_x1 = gl.scissor[0] + gl.scissor[2];
        if (gl.scissor[1] + gl.scissor[3] < draw.clip_y1) draw.clip_y1 = gl.scissor[1] + gl.scissor[3];
    }
    draw.vw = (float)gl.viewport[2] * 0.5f; draw.vh = (float)gl.viewport[3] * 0.5f;
    draw.vx = (float)gl.viewport[0]; draw.vy = (float)gl.viewport[1];
    if (vertices > draw.cache_cap) {
        unsigned int cap = vertices + 256u;
        float *cache = realloc(draw.cache, (size_t)cap * (4u + MAX_VARYING_FLOATS) * sizeof(float));
        unsigned int *stamp, *outcode;
        if (!cache) { set_error(GL_OUT_OF_MEMORY); return 0; }
        draw.cache = cache;
        stamp = realloc(draw.stamp, (size_t)cap * sizeof(unsigned int));
        if (!stamp) { set_error(GL_OUT_OF_MEMORY); return 0; }
        draw.stamp = stamp;
        outcode = realloc(draw.outcode, (size_t)cap * sizeof(unsigned int));
        if (!outcode) { set_error(GL_OUT_OF_MEMORY); return 0; }
        draw.outcode = outcode;
        stamp = realloc(draw.full_stamp, (size_t)cap * sizeof(unsigned int));
        if (!stamp) { set_error(GL_OUT_OF_MEMORY); return 0; }
        draw.full_stamp = stamp;
        memset(draw.stamp + draw.cache_cap, 0, (size_t)(cap - draw.cache_cap) * sizeof(unsigned int));
        memset(draw.full_stamp + draw.cache_cap, 0, (size_t)(cap - draw.cache_cap) * sizeof(unsigned int));
        draw.cache_cap = cap;
    }
    if (++draw.generation == 0) {
        memset(draw.stamp, 0, (size_t)draw.cache_cap * sizeof(unsigned int));
        memset(draw.full_stamp, 0, (size_t)draw.cache_cap * sizeof(unsigned int));
        draw.generation = 1;
    }
    if (draw.t.owned && (draw.prefix_mask != draw.t.owned || draw.prefix_rows != draw.t.height)) {
        int y, *prefix = realloc(draw.prefix, (size_t)(draw.t.height + 1) * sizeof(int));
        if (!prefix) { set_error(GL_OUT_OF_MEMORY); return 0; }
        draw.prefix = prefix;
        prefix[0] = 0;
        for (y = 0; y < draw.t.height; y++) prefix[y + 1] = prefix[y] + (draw.t.owned[y] != 0);
        draw.prefix_mask = draw.t.owned; draw.prefix_rows = draw.t.height;
    }
    gl.stats.draws++;
    return 1;
}

static void assemble(GLenum mode, unsigned int count, unsigned int (*index)(const void *, unsigned int), const void *src)
{
    unsigned int i;
    if (mode == GL_TRIANGLES) for (i = 0; i + 2 < count; i += 3) draw_triangle(index(src, i), index(src, i + 1), index(src, i + 2));
    else if (mode == GL_TRIANGLE_STRIP) {
        for (i = 0; i + 2 < count; i++) {
            if (i & 1) draw_triangle(index(src, i + 1), index(src, i), index(src, i + 2));
            else draw_triangle(index(src, i), index(src, i + 1), index(src, i + 2));
        }
    } else for (i = 1; i + 1 < count; i++) draw_triangle(index(src, 0), index(src, i), index(src, i + 1));
}
static unsigned int index_u8(const void *s, unsigned int i) { return ((const unsigned char *)s)[i]; }
static unsigned int index_u16(const void *s, unsigned int i) { return ((const unsigned short *)s)[i]; }
static unsigned int index_u32(const void *s, unsigned int i) { return ((const unsigned int *)s)[i]; }
static unsigned int first_vertex;
static unsigned int index_linear(const void *s, unsigned int i) { (void)s; return first_vertex + i; }

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (first < 0 || count < 0) { set_error(GL_INVALID_VALUE); return; }
    if (!begin_draw(mode, (unsigned)(first + count))) return;
    first_vertex = (unsigned)first;
    assemble(mode, (unsigned)count, index_linear, 0);
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    unsigned int (*index)(const void *, unsigned int);
    unsigned int i, max = 0;
    const unsigned char *src = indices;
    if (count < 0) { set_error(GL_INVALID_VALUE); return; }
    if (type == GL_UNSIGNED_BYTE) index = index_u8;
    else if (type == GL_UNSIGNED_SHORT) index = index_u16;
    else if (type == GL_UNSIGNED_INT) index = index_u32;
    else { set_error(GL_INVALID_ENUM); return; }
    if (gl.element_buffer) {
        buffer *b = table_get(&gl.buffers, gl.element_buffer);
        if (!b || !b->data) { set_error(GL_INVALID_OPERATION); return; }
        src = b->data + (size_t)indices;
    }
    if (!src) return;
    for (i = 0; i < (unsigned)count; i++) { unsigned int v = index(src, i); if (v > max) max = v; }
    if (!begin_draw(mode, max + 1u)) return;
    assemble(mode, (unsigned)count, index, src);
}

/* ------------------------------------------------------- clear, read, copy */
void glClear(GLbitfield mask)
{
    target t;
    int x0 = 0, y0 = 0, x1, y1, x, y;
    unsigned char c[4];
    int k;
    if (mask & ~(GLbitfield)(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) { set_error(GL_INVALID_VALUE); return; }
    if (!current_target(&t)) return;
    x1 = t.width; y1 = t.height;
    if (gl.scissor_test) {
        if (gl.scissor[0] > x0) x0 = gl.scissor[0];
        if (gl.scissor[1] > y0) y0 = gl.scissor[1];
        if (gl.scissor[0] + gl.scissor[2] < x1) x1 = gl.scissor[0] + gl.scissor[2];
        if (gl.scissor[1] + gl.scissor[3] < y1) y1 = gl.scissor[1] + gl.scissor[3];
    }
    for (k = 0; k < 4; k++) c[k] = to_byte(gl.clear_color[k]);
    for (y = y0; y < y1; y++) {
        if (t.owned && !t.owned[y]) continue;
        if ((mask & GL_COLOR_BUFFER_BIT) && t.color) {
            unsigned char *p = t.color + ((size_t)y * (size_t)t.width + (size_t)x0) * 4u;
            if (gl.color_mask[0] && gl.color_mask[1] && gl.color_mask[2] && gl.color_mask[3]) {
                unsigned int word;
                memcpy(&word, c, 4);
                for (x = x0; x < x1; x++, p += 4) memcpy(p, &word, 4);
            } else for (x = x0; x < x1; x++, p += 4) for (k = 0; k < 4; k++) if (gl.color_mask[k]) p[k] = c[k];
        }
        if ((mask & GL_DEPTH_BUFFER_BIT) && t.depth && gl.depth_mask) {
            float *d = t.depth + (size_t)y * (size_t)t.width + (size_t)x0;
            for (x = x0; x < x1; x++) *d++ = gl.clear_depth;
        }
    }
}

void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid *pixels)
{
    target t;
    int i, j;
    size_t row;
    unsigned char *out = pixels;
    if (w < 0 || h < 0) { set_error(GL_INVALID_VALUE); return; }
    if (format != GL_RGBA || type != GL_UNSIGNED_BYTE) { set_error(GL_INVALID_OPERATION); return; }
    if (!current_target(&t) || !t.color) return;
    row = ((size_t)w * 4u + (size_t)gl.pack_alignment - 1u) & ~(size_t)(gl.pack_alignment - 1);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            int sx = x + i, sy = y + j;
            unsigned char *o = out + (size_t)j * row + (size_t)i * 4u;
            if (sx < 0 || sy < 0 || sx >= t.width || sy >= t.height) continue;   /* undefined outside */
            memcpy(o, t.color + ((size_t)sy * (size_t)t.width + (size_t)sx) * 4u, 4);
        }
}

void glCopyTexSubImage2D(GLenum target_enum, GLint level, GLint xo, GLint yo, GLint x, GLint y, GLsizei w, GLsizei h)
{
    target t;
    texture *tex;
    int i, j;
    if (!texture_target_level(target_enum, level)) return;
    if (!(tex = bound_texture(target_enum))) return;
    if (level > 0) return;
    if (xo < 0 || yo < 0 || w < 0 || h < 0 || xo + w > tex->width || yo + h > tex->height) { set_error(GL_INVALID_VALUE); return; }
    if (!current_target(&t) || !t.color) return;
    if (t.color == tex->rgba) { set_error(GL_INVALID_OPERATION); return; }   /* feedback loop */
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            int sx = x + i, sy = y + j;
            unsigned char *o = tex->rgba + ((size_t)(yo + j) * (size_t)tex->width + (size_t)(xo + i)) * 4u;
            if (sx < 0 || sy < 0 || sx >= t.width || sy >= t.height) continue;
            memcpy(o, t.color + ((size_t)sy * (size_t)t.width + (size_t)sx) * 4u, 4);
            if (tex->format == GL_RGB || tex->format == GL_LUMINANCE) o[3] = 255;
        }
}
void glCopyTexImage2D(GLenum target_enum, GLint level, GLenum format, GLint x, GLint y, GLsizei w, GLsizei h, GLint border)
{
    if (format != GL_RGBA && format != GL_RGB && format != GL_LUMINANCE && format != GL_LUMINANCE_ALPHA && format != GL_ALPHA) { set_error(GL_INVALID_ENUM); return; }
    glTexImage2D(target_enum, level, (GLint)format, w, h, border, format, GL_UNSIGNED_BYTE, 0);
    if (gl.error == GL_NO_ERROR) glCopyTexSubImage2D(target_enum, level, 0, 0, x, y, w, h);
}

/* ---------------------------------------------------------------- queries */
static int get_values(GLenum pname, float *v, int *integer)
{
    *integer = 1;
    switch (pname) {
    case GL_VIEWPORT: v[0] = (float)gl.viewport[0]; v[1] = (float)gl.viewport[1]; v[2] = (float)gl.viewport[2]; v[3] = (float)gl.viewport[3]; return 4;
    case GL_SCISSOR_BOX: v[0] = (float)gl.scissor[0]; v[1] = (float)gl.scissor[1]; v[2] = (float)gl.scissor[2]; v[3] = (float)gl.scissor[3]; return 4;
    case GL_COLOR_CLEAR_VALUE: *integer = 0; memcpy(v, gl.clear_color, sizeof(gl.clear_color)); return 4;
    case GL_BLEND_COLOR: *integer = 0; memcpy(v, gl.blend_color, sizeof(gl.blend_color)); return 4;
    case GL_COLOR_WRITEMASK: v[0] = gl.color_mask[0]; v[1] = gl.color_mask[1]; v[2] = gl.color_mask[2]; v[3] = gl.color_mask[3]; return 4;
    case GL_DEPTH_RANGE: *integer = 0; v[0] = gl.depth_near; v[1] = gl.depth_far; return 2;
    case GL_DEPTH_CLEAR_VALUE: *integer = 0; v[0] = gl.clear_depth; return 1;
    case GL_DEPTH_WRITEMASK: v[0] = gl.depth_mask; return 1;
    case GL_DEPTH_FUNC: v[0] = (float)gl.depth_func; return 1;
    case GL_CULL_FACE_MODE: v[0] = (float)gl.cull_face; return 1;
    case GL_FRONT_FACE: v[0] = (float)gl.front_face; return 1;
    case GL_BLEND_SRC_RGB: v[0] = (float)gl.blend_src_rgb; return 1;
    case GL_BLEND_DST_RGB: v[0] = (float)gl.blend_dst_rgb; return 1;
    case GL_BLEND_SRC_ALPHA: v[0] = (float)gl.blend_src_alpha; return 1;
    case GL_BLEND_DST_ALPHA: v[0] = (float)gl.blend_dst_alpha; return 1;
    case GL_BLEND_EQUATION_RGB: v[0] = (float)gl.blend_eq_rgb; return 1;
    case GL_BLEND_EQUATION_ALPHA: v[0] = (float)gl.blend_eq_alpha; return 1;
    case GL_ACTIVE_TEXTURE: v[0] = (float)(GL_TEXTURE0 + gl.active_unit); return 1;
    case GL_TEXTURE_BINDING_2D: v[0] = (float)gl.unit_texture[gl.active_unit]; return 1;
    case GL_ARRAY_BUFFER_BINDING: v[0] = (float)gl.array_buffer; return 1;
    case GL_ELEMENT_ARRAY_BUFFER_BINDING: v[0] = (float)gl.element_buffer; return 1;
    case GL_FRAMEBUFFER_BINDING: v[0] = (float)gl.framebuffer_binding; return 1;
    case GL_RENDERBUFFER_BINDING: v[0] = (float)gl.renderbuffer_binding; return 1;
    case GL_CURRENT_PROGRAM: v[0] = (float)gl.program; return 1;
    case GL_UNPACK_ALIGNMENT: v[0] = (float)gl.unpack_alignment; return 1;
    case GL_PACK_ALIGNMENT: v[0] = (float)gl.pack_alignment; return 1;
    case GL_LINE_WIDTH: *integer = 0; v[0] = gl.line_width; return 1;
    case GL_ALIASED_LINE_WIDTH_RANGE: case GL_ALIASED_POINT_SIZE_RANGE: *integer = 0; v[0] = 1.0f; v[1] = 1.0f; return 2;
    case GL_POLYGON_OFFSET_FACTOR: *integer = 0; v[0] = gl.offset_factor; return 1;
    case GL_POLYGON_OFFSET_UNITS: *integer = 0; v[0] = gl.offset_units; return 1;
    case GL_GENERATE_MIPMAP_HINT: v[0] = (float)gl.mipmap_hint; return 1;
    case GL_STENCIL_CLEAR_VALUE: v[0] = (float)gl.clear_stencil; return 1;
    case GL_MAX_TEXTURE_SIZE: case GL_MAX_RENDERBUFFER_SIZE: case GL_MAX_CUBE_MAP_TEXTURE_SIZE: v[0] = (float)MAX_TEXTURE_SIZE; return 1;
    case GL_MAX_VIEWPORT_DIMS: v[0] = v[1] = (float)MAX_TEXTURE_SIZE; return 2;
    case GL_MAX_VERTEX_ATTRIBS: v[0] = MAX_ATTRIBS; return 1;
    case GL_MAX_VARYING_VECTORS: v[0] = MAX_VARYING_FLOATS / 4; return 1;
    case GL_MAX_VERTEX_UNIFORM_VECTORS: case GL_MAX_FRAGMENT_UNIFORM_VECTORS: v[0] = 32; return 1;
    case GL_MAX_TEXTURE_IMAGE_UNITS: case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS: v[0] = MAX_UNITS; return 1;
    case GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS: v[0] = TG_VM_TEXTURES; return 1;
    case GL_SUBPIXEL_BITS: v[0] = 4; return 1;
    case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS: v[0] = 8; return 1;
    case GL_DEPTH_BITS: v[0] = 24; return 1;
    case GL_STENCIL_BITS: case GL_SAMPLE_BUFFERS: case GL_SAMPLES: v[0] = 0; return 1;
    case GL_NUM_COMPRESSED_TEXTURE_FORMATS: v[0] = 4; return 1;
    case GL_COMPRESSED_TEXTURE_FORMATS: v[0] = GL_COMPRESSED_RGB_PVRTC_4BPPV1_IMG; v[1] = GL_COMPRESSED_RGB_PVRTC_2BPPV1_IMG; v[2] = GL_COMPRESSED_RGBA_PVRTC_4BPPV1_IMG; v[3] = GL_COMPRESSED_RGBA_PVRTC_2BPPV1_IMG; return 4;
    case GL_IMPLEMENTATION_COLOR_READ_FORMAT: v[0] = GL_RGBA; return 1;
    case GL_IMPLEMENTATION_COLOR_READ_TYPE: v[0] = GL_UNSIGNED_BYTE; return 1;
    case GL_SHADER_COMPILER: v[0] = 1; return 1;
    case GL_NUM_SHADER_BINARY_FORMATS: v[0] = 0; return 1;
    default: {
        GLboolean *cap = capability(pname);
        if (cap) { v[0] = *cap; return 1; }
        return 0;
    }
    }
}
void glGetIntegerv(GLenum pname, GLint *params)
{
    float v[4];
    int integer, n = get_values(pname, v, &integer), i;
    if (!n) { set_error(GL_INVALID_ENUM); return; }
    for (i = 0; i < n; i++) params[i] = integer ? (GLint)v[i] : (GLint)(v[i] * 2147483647.0f);
}
void glGetFloatv(GLenum pname, GLfloat *params)
{
    float v[4];
    int integer, n = get_values(pname, v, &integer), i;
    if (!n) { set_error(GL_INVALID_ENUM); return; }
    for (i = 0; i < n; i++) params[i] = v[i];
}
void glGetBooleanv(GLenum pname, GLboolean *params)
{
    float v[4];
    int integer, n = get_values(pname, v, &integer), i;
    if (!n) { set_error(GL_INVALID_ENUM); return; }
    for (i = 0; i < n; i++) params[i] = v[i] != 0.0f;
}
