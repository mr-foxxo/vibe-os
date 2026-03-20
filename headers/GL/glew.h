#ifndef VIBEOS_GLEW_H
#define VIBEOS_GLEW_H

#include <stddef.h>
#include <stdint.h>

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;
typedef char GLchar;
typedef void GLvoid;

#define GL_FALSE 0
#define GL_TRUE 1

#define GLEW_OK 0

#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_TRIANGLES 0x0004
#define GL_LINES 0x0001
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_COLOR_LOGIC_OP 0x0BF2
#define GL_INVERT 0x150A
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_SCISSOR_TEST 0x0C11

int glewInit(void);
void glActiveTexture(GLenum texture);
void glAttachShader(GLuint program, GLuint shader);
void glBindBuffer(GLenum target, GLuint buffer);
void glBindTexture(GLenum target, GLuint texture);
void glBlendFunc(GLenum sfactor, GLenum dfactor);
void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
void glClear(GLbitfield mask);
void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void glCompileShader(GLuint shader);
GLuint glCreateProgram(void);
GLuint glCreateShader(GLenum type);
void glDeleteBuffers(GLsizei n, const GLuint *buffers);
void glDeleteShader(GLuint shader);
void glDetachShader(GLuint program, GLuint shader);
void glDisable(GLenum cap);
void glDisableVertexAttribArray(GLuint index);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glEnable(GLenum cap);
void glEnableVertexAttribArray(GLuint index);
void glGenBuffers(GLsizei n, GLuint *buffers);
void glGenTextures(GLsizei n, GLuint *textures);
GLint glGetAttribLocation(GLuint program, const GLchar *name);
void glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
void glGetProgramiv(GLuint program, GLenum pname, GLint *params);
void glGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
void glGetShaderiv(GLuint shader, GLenum pname, GLint *params);
GLint glGetUniformLocation(GLuint program, const GLchar *name);
void glLineWidth(GLfloat width);
void glLinkProgram(GLuint program);
void glLogicOp(GLenum opcode);
void glPolygonOffset(GLfloat factor, GLfloat units);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void glShaderSource(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const void *pixels);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glUniform1f(GLint location, GLfloat v0);
void glUniform1i(GLint location, GLint v0);
void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
void glUseProgram(GLuint program);
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const void *pointer);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);

#endif
