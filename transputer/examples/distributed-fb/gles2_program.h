/* Experimental, deliberately limited GLSL ES shader/program support. */
#ifndef DISTFB_GLES2_PROGRAM_H
#define DISTFB_GLES2_PROGRAM_H
#include "gles2_draw.h"
typedef char GLchar;
#define GL_VERTEX_SHADER 0x8b31
#define GL_FRAGMENT_SHADER 0x8b30
#define GL_COMPILE_STATUS 0x8b81
#define GL_LINK_STATUS 0x8b82
#define GL_INFO_LOG_LENGTH 0x8b84
#define GL_SHADER_TYPE 0x8b4f
#define GL_SHADER_SOURCE_LENGTH 0x8b88
#define GL_DELETE_STATUS 0x8b80
#define TG_GLES_MAX_SHADERS 4
#define TG_GLES_MAX_PROGRAMS 2
#define TG_GLES_SHADER_SOURCE_MAX 8192
typedef struct {
    GLenum type;
    GLboolean used, compiled, overflow, deleted;
    char source[TG_GLES_SHADER_SOURCE_MAX];
    char attribute[32], matrix[32], color[32];
    const char *log;
} tg_gles_shader;
typedef struct {
    GLboolean used, linked, deleted;
    GLuint vertex_shader, fragment_shader;
    GLuint attribute_index;
    char bound_attribute[32], attribute[32], matrix_name[32], color_name[32];
    GLfloat matrix[16], color[4];
    unsigned int texture[64];
    const char *log;
} tg_gles_program;
typedef struct tg_gles_program_state {
    tg_gles_shader shaders[TG_GLES_MAX_SHADERS];
    tg_gles_program programs[TG_GLES_MAX_PROGRAMS];
    GLuint current_program;
} tg_gles_program_state;
/* Explicit embedding hook. Shader source, programs and linked executables are
 * retained in caller-owned storage; this is not an EGL context manager. */
void tgGlesInitPrograms(tg_gles_state *context, tg_gles_program_state *state);
GLuint glCreateShader(GLenum type);
void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *strings, const GLint *lengths);
void glCompileShader(GLuint shader);
void glGetShaderiv(GLuint shader, GLenum pname, GLint *params);
void glGetShaderInfoLog(GLuint shader, GLsizei size, GLsizei *length, GLchar *log);
void glDeleteShader(GLuint shader);
GLuint glCreateProgram(void);
void glAttachShader(GLuint program, GLuint shader);
void glDetachShader(GLuint program, GLuint shader);
void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name);
void glLinkProgram(GLuint program);
void glGetProgramiv(GLuint program, GLenum pname, GLint *params);
void glGetProgramInfoLog(GLuint program, GLsizei size, GLsizei *length, GLchar *log);
void glUseProgram(GLuint program);
void glDeleteProgram(GLuint program);
GLint glGetAttribLocation(GLuint program, const GLchar *name);
GLint glGetUniformLocation(GLuint program, const GLchar *name);
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void glUniform4f(GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
#endif
