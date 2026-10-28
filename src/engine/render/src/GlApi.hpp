#pragma once

#include <cstddef>

#include <fa/platform/GlContext.hpp>

namespace fa::render::detail {

using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLbyte = signed char;
using GLubyte = unsigned char;
using GLshort = short;
using GLushort = unsigned short;
using GLint = int;
using GLuint = unsigned int;
using GLsizei = int;
using GLfloat = float;
using GLchar = char;
using GLintptr = std::ptrdiff_t;
using GLsizeiptr = std::ptrdiff_t;

inline constexpr GLenum GL_NO_ERROR = 0;
inline constexpr GLenum GL_TRIANGLES = 0x0004;
inline constexpr GLenum GL_UNSIGNED_BYTE = 0x1401;
inline constexpr GLenum GL_UNSIGNED_INT = 0x1405;
inline constexpr GLenum GL_FLOAT = 0x1406;
inline constexpr GLenum GL_RED = 0x1903;
inline constexpr GLenum GL_RGBA = 0x1908;
inline constexpr GLenum GL_VERSION = 0x1F02;
inline constexpr GLenum GL_RENDERER = 0x1F01;
inline constexpr GLenum GL_VENDOR = 0x1F00;
inline constexpr GLenum GL_NEAREST = 0x2600;
inline constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800;
inline constexpr GLenum GL_TEXTURE_MIN_FILTER = 0x2801;
inline constexpr GLenum GL_TEXTURE_WRAP_S = 0x2802;
inline constexpr GLenum GL_TEXTURE_WRAP_T = 0x2803;
inline constexpr GLenum GL_BLEND = 0x0BE2;
inline constexpr GLenum GL_DEPTH_TEST = 0x0B71;
inline constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5;
inline constexpr GLenum GL_UNPACK_ROW_LENGTH = 0x0CF2;
inline constexpr GLenum GL_SRC_ALPHA = 0x0302;
inline constexpr GLenum GL_ONE = 1;
inline constexpr GLenum GL_ONE_MINUS_SRC_ALPHA = 0x0303;
inline constexpr GLenum GL_COLOR_BUFFER_BIT = 0x00004000;
inline constexpr GLenum GL_TEXTURE_2D = 0x0DE1;
inline constexpr GLenum GL_RGBA8 = 0x8058;
inline constexpr GLenum GL_R8 = 0x8229;
inline constexpr GLenum GL_CLAMP_TO_EDGE = 0x812F;
inline constexpr GLenum GL_TEXTURE0 = 0x84C0;
inline constexpr GLenum GL_ARRAY_BUFFER = 0x8892;
inline constexpr GLenum GL_ELEMENT_ARRAY_BUFFER = 0x8893;
inline constexpr GLenum GL_DYNAMIC_DRAW = 0x88E8;
inline constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30;
inline constexpr GLenum GL_VERTEX_SHADER = 0x8B31;
inline constexpr GLenum GL_COMPILE_STATUS = 0x8B81;
inline constexpr GLenum GL_LINK_STATUS = 0x8B82;
inline constexpr GLenum GL_INFO_LOG_LENGTH = 0x8B84;

/// \brief The entry points the sprite pipeline needs, resolved at startup.
///
/// Declared as a table instead of one free variable per function so resolution,
/// reporting and shutdown stay one loop. `glGetString` and friends are here even
/// though they live in opengl32.dll - IGlContext::procAddress hides that.
#define FA_GL_FUNCTION_LIST(X)                                                                                         \
    X(const GLubyte*, GetString, (GLenum name))                                                                        \
    X(GLenum, GetError, (void))                                                                                        \
    X(void, Viewport, (GLint x, GLint y, GLsizei width, GLsizei height))                                               \
    X(void, ClearColor, (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha))                                     \
    X(void, Clear, (GLbitfield mask))                                                                                  \
    X(void, Enable, (GLenum capability))                                                                               \
    X(void, Disable, (GLenum capability))                                                                              \
    X(void, BlendFuncSeparate, (GLenum srcRgb, GLenum dstRgb, GLenum srcAlpha, GLenum dstAlpha))                       \
    X(void, PixelStorei, (GLenum target, GLint value))                                                                 \
    X(void, GenTextures, (GLsizei count, GLuint * textures))                                                           \
    X(void, DeleteTextures, (GLsizei count, const GLuint* textures))                                                   \
    X(void, BindTexture, (GLenum target, GLuint texture))                                                              \
    X(void, ActiveTexture, (GLenum texture))                                                                           \
    X(void,                                                                                                            \
      TexImage2D,                                                                                                      \
      (GLenum target,                                                                                                  \
       GLint level,                                                                                                    \
       GLint internalFormat,                                                                                           \
       GLsizei width,                                                                                                  \
       GLsizei height,                                                                                                 \
       GLint border,                                                                                                   \
       GLenum format,                                                                                                  \
       GLenum type,                                                                                                    \
       const void* pixels))                                                                                            \
    X(void, TexParameteri, (GLenum target, GLenum name, GLint value))                                                  \
    X(void, GenVertexArrays, (GLsizei count, GLuint * arrays))                                                         \
    X(void, DeleteVertexArrays, (GLsizei count, const GLuint* arrays))                                                 \
    X(void, BindVertexArray, (GLuint array))                                                                           \
    X(void, GenBuffers, (GLsizei count, GLuint * buffers))                                                             \
    X(void, DeleteBuffers, (GLsizei count, const GLuint* buffers))                                                     \
    X(void, BindBuffer, (GLenum target, GLuint buffer))                                                                \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))                              \
    X(void, BufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data))                        \
    X(GLuint, CreateShader, (GLenum type))                                                                             \
    X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths))          \
    X(void, CompileShader, (GLuint shader))                                                                            \
    X(void, GetShaderiv, (GLuint shader, GLenum name, GLint * value))                                                  \
    X(void, GetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei * length, GLchar * infoLog))                    \
    X(void, DeleteShader, (GLuint shader))                                                                             \
    X(GLuint, CreateProgram, (void))                                                                                   \
    X(void, AttachShader, (GLuint program, GLuint shader))                                                             \
    X(void, LinkProgram, (GLuint program))                                                                             \
    X(void, GetProgramiv, (GLuint program, GLenum name, GLint * value))                                                \
    X(void, GetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei * length, GLchar * infoLog))                  \
    X(void, DeleteProgram, (GLuint program))                                                                           \
    X(void, UseProgram, (GLuint program))                                                                              \
    X(GLint, GetUniformLocation, (GLuint program, const GLchar* name))                                                 \
    X(void, Uniform1i, (GLint location, GLint value))                                                                  \
    X(void, UniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value))              \
    X(void, EnableVertexAttribArray, (GLuint index))                                                                   \
    X(void,                                                                                                            \
      VertexAttribPointer,                                                                                             \
      (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer))              \
    X(void, DrawElements, (GLenum mode, GLsizei count, GLenum type, const void* indices))

struct GlFunctions {
    bool loaded = false;

#define FA_GL_DECLARE_FIELD(returnType, name, args) returnType(*gl##name) args = nullptr;
    FA_GL_FUNCTION_LIST(FA_GL_DECLARE_FIELD)
#undef FA_GL_DECLARE_FIELD
};

/// \brief Resolves every entry point against \p context.
///
/// \param outMissing Receives the first name that could not be resolved.
/// \return false when at least one function is missing.
bool loadGlFunctions(GlFunctions& functions, platform::IGlContext& context, const char** outMissing);

/// \brief Drains the GL error queue and returns the first error, or GL_NO_ERROR.
GLenum takeGlError(const GlFunctions& functions);

}  // namespace fa::render::detail
