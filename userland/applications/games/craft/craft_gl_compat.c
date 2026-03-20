#include <GL/glew.h>

static GLuint g_next_gl_id = 1u;

int glewInit(void) { return GLEW_OK; }
void glActiveTexture(GLenum texture) { (void)texture; }
void glAttachShader(GLuint program, GLuint shader) { (void)program; (void)shader; }
void glBindBuffer(GLenum target, GLuint buffer) { (void)target; (void)buffer; }
void glBindTexture(GLenum target, GLuint texture) { (void)target; (void)texture; }
void glBlendFunc(GLenum sfactor, GLenum dfactor) { (void)sfactor; (void)dfactor; }
void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) { (void)target; (void)size; (void)data; (void)usage; }
void glClear(GLbitfield mask) { (void)mask; }
void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) { (void)red; (void)green; (void)blue; (void)alpha; }
void glCompileShader(GLuint shader) { (void)shader; }
GLuint glCreateProgram(void) { return g_next_gl_id++; }
GLuint glCreateShader(GLenum type) { (void)type; return g_next_gl_id++; }
void glDeleteBuffers(GLsizei n, const GLuint *buffers) { (void)n; (void)buffers; }
void glDeleteShader(GLuint shader) { (void)shader; }
void glDetachShader(GLuint program, GLuint shader) { (void)program; (void)shader; }
void glDisable(GLenum cap) { (void)cap; }
void glDisableVertexAttribArray(GLuint index) { (void)index; }
void glDrawArrays(GLenum mode, GLint first, GLsizei count) { (void)mode; (void)first; (void)count; }
void glEnable(GLenum cap) { (void)cap; }
void glEnableVertexAttribArray(GLuint index) { (void)index; }
void glGenBuffers(GLsizei n, GLuint *buffers) { for (GLsizei i = 0; i < n; ++i) buffers[i] = g_next_gl_id++; }
void glGenTextures(GLsizei n, GLuint *textures) { for (GLsizei i = 0; i < n; ++i) textures[i] = g_next_gl_id++; }
GLint glGetAttribLocation(GLuint program, const GLchar *name) { (void)program; (void)name; return 0; }
void glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei *length, GLchar *infoLog) { (void)program; if (length) *length = 0; if (maxLength > 0 && infoLog) infoLog[0] = '\0'; }
void glGetProgramiv(GLuint program, GLenum pname, GLint *params) { (void)program; (void)pname; if (params) *params = 1; }
void glGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog) { (void)shader; if (length) *length = 0; if (maxLength > 0 && infoLog) infoLog[0] = '\0'; }
void glGetShaderiv(GLuint shader, GLenum pname, GLint *params) { (void)shader; (void)pname; if (params) *params = 1; }
GLint glGetUniformLocation(GLuint program, const GLchar *name) { (void)program; (void)name; return 0; }
void glLineWidth(GLfloat width) { (void)width; }
void glLinkProgram(GLuint program) { (void)program; }
void glLogicOp(GLenum opcode) { (void)opcode; }
void glPolygonOffset(GLfloat factor, GLfloat units) { (void)factor; (void)units; }
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) { (void)x; (void)y; (void)width; (void)height; }
void glShaderSource(GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length) { (void)shader; (void)count; (void)string; (void)length; }
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels) {
    (void)target; (void)level; (void)internalformat; (void)width; (void)height; (void)border; (void)format; (void)type; (void)pixels;
}
void glTexParameteri(GLenum target, GLenum pname, GLint param) { (void)target; (void)pname; (void)param; }
void glUniform1f(GLint location, GLfloat v0) { (void)location; (void)v0; }
void glUniform1i(GLint location, GLint v0) { (void)location; (void)v0; }
void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { (void)location; (void)v0; (void)v1; (void)v2; }
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) { (void)location; (void)count; (void)transpose; (void)value; }
void glUseProgram(GLuint program) { (void)program; }
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) {
    (void)index; (void)size; (void)type; (void)normalized; (void)stride; (void)pointer;
}
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) { (void)x; (void)y; (void)width; (void)height; }
