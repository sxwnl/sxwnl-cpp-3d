/*
 * Pre-generated GLAD loader for OpenGL 3.3 Core Profile.
 * Generated from the Khronos OpenGL Registry.
 * SPDX-License-Identifier: MIT
 *
 * Usage:
 *   1. Add glad.c to your build.
 *   2. Include this header BEFORE any window-system headers (GLFW, SDL…).
 *   3. After creating a GL context, call:
 *        gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
 */
#ifndef __glad_h_
#define __glad_h_

#ifdef __gl_h_
#error OpenGL header already included; glad.h must be included first.
#endif
#define __gl_h_

#include <KHR/khrplatform.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------- GL typedefs ----------------------------- */
typedef unsigned int   GLenum;
typedef unsigned char  GLboolean;
typedef unsigned int   GLbitfield;
typedef void           GLvoid;
typedef signed char    GLbyte;
typedef short          GLshort;
typedef int            GLint;
typedef unsigned char  GLubyte;
typedef unsigned short GLushort;
typedef unsigned int   GLuint;
typedef int            GLsizei;
typedef float          GLfloat;
typedef float          GLclampf;
typedef double         GLdouble;
typedef double         GLclampd;
typedef char           GLchar;
typedef khronos_ptrdiff_t GLintptr;
typedef khronos_ptrdiff_t GLsizeiptr;
typedef khronos_int64_t   GLint64;
typedef khronos_uint64_t  GLuint64;

#ifdef _WIN32
#  ifndef APIENTRY
#    define APIENTRY __stdcall
#  endif
#  ifndef WINGDIAPI
#    define WINGDIAPI __declspec(dllimport)
#  endif
#else
#  define APIENTRY
#  define WINGDIAPI
#endif
#define GLAPI WINGDIAPI

/* ========================= GL CONSTANTS ========================= */

/* Booleans */
#define GL_FALSE                          0
#define GL_TRUE                           1

/* Primitive types */
#define GL_POINTS                         0x0000
#define GL_LINES                          0x0001
#define GL_LINE_LOOP                      0x0002
#define GL_LINE_STRIP                     0x0003
#define GL_TRIANGLES                      0x0004
#define GL_TRIANGLE_STRIP                 0x0005
#define GL_TRIANGLE_FAN                   0x0006

/* Depth / stencil / test */
#define GL_NEVER                          0x0200
#define GL_LESS                           0x0201
#define GL_EQUAL                          0x0202
#define GL_LEQUAL                         0x0203
#define GL_GREATER                        0x0204
#define GL_NOTEQUAL                       0x0205
#define GL_GEQUAL                         0x0206
#define GL_ALWAYS                         0x0207

/* Blend factors */
#define GL_ZERO                           0
#define GL_ONE                            1
#define GL_SRC_COLOR                      0x0300
#define GL_ONE_MINUS_SRC_COLOR            0x0301
#define GL_SRC_ALPHA                      0x0302
#define GL_ONE_MINUS_SRC_ALPHA            0x0303
#define GL_DST_ALPHA                      0x0304
#define GL_ONE_MINUS_DST_ALPHA            0x0305
#define GL_DST_COLOR                      0x0306
#define GL_ONE_MINUS_DST_COLOR            0x0307
#define GL_SRC_ALPHA_SATURATE             0x0308
#define GL_CONSTANT_COLOR                 0x8001
#define GL_ONE_MINUS_CONSTANT_COLOR       0x8002
#define GL_CONSTANT_ALPHA                 0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA       0x8004

/* Blend equation */
#define GL_FUNC_ADD                       0x8006
#define GL_MIN                            0x8007
#define GL_MAX                            0x8008
#define GL_FUNC_SUBTRACT                  0x800A
#define GL_FUNC_REVERSE_SUBTRACT          0x800B

/* Enable bits */
#define GL_CULL_FACE                      0x0B44
#define GL_DEPTH_TEST                     0x0B71
#define GL_BLEND                          0x0BE2
#define GL_LINE_SMOOTH                    0x0B20
#define GL_SCISSOR_TEST                   0x0C11
#define GL_PROGRAM_POINT_SIZE             0x8642

/* Clear bits */
#define GL_COLOR_BUFFER_BIT               0x00004000
#define GL_DEPTH_BUFFER_BIT               0x00000100
#define GL_STENCIL_BUFFER_BIT             0x00000400

/* Data types */
#define GL_BYTE                           0x1400
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_SHORT                          0x1402
#define GL_UNSIGNED_SHORT                 0x1403
#define GL_INT                            0x1404
#define GL_UNSIGNED_INT                   0x1405
#define GL_FLOAT                          0x1406
#define GL_DOUBLE                         0x140A

/* Texture */
#define GL_TEXTURE_2D                     0x0DE1
#define GL_TEXTURE_3D                     0x806F
#define GL_TEXTURE_CUBE_MAP               0x8513
#define GL_TEXTURE_MIN_FILTER             0x2801
#define GL_TEXTURE_MAG_FILTER             0x2800
#define GL_TEXTURE_WRAP_S                 0x2802
#define GL_TEXTURE_WRAP_T                 0x2803
#define GL_TEXTURE_WRAP_R                 0x8072
#define GL_NEAREST                        0x2600
#define GL_LINEAR                         0x2601
#define GL_NEAREST_MIPMAP_NEAREST         0x2700
#define GL_LINEAR_MIPMAP_NEAREST          0x2701
#define GL_NEAREST_MIPMAP_LINEAR          0x2702
#define GL_LINEAR_MIPMAP_LINEAR           0x2703
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_REPEAT                         0x2901
#define GL_MIRRORED_REPEAT                0x8370

/* Pixel formats */
#define GL_RED                            0x1903
#define GL_RG                             0x8227
#define GL_RGB                            0x1907
#define GL_RGBA                           0x1908
#define GL_DEPTH_COMPONENT                0x1902
/* Internal formats */
#define GL_R8                             0x8229
#define GL_RG8                            0x822B
#define GL_RGB8                           0x8051
#define GL_RGBA8                          0x8058
#define GL_RGB16F                         0x881B
#define GL_RGBA16F                        0x881A
#define GL_DEPTH_COMPONENT16              0x81A5
#define GL_DEPTH_COMPONENT24              0x81A6
#define GL_DEPTH_COMPONENT32              0x81A7
#define GL_DEPTH_COMPONENT32F             0x8CAC

/* Buffer objects (GL 1.5) */
#define GL_ARRAY_BUFFER                   0x8892
#define GL_ELEMENT_ARRAY_BUFFER           0x8893
#define GL_UNIFORM_BUFFER                 0x8A11
#define GL_STREAM_DRAW                    0x88E0
#define GL_STREAM_READ                    0x88E1
#define GL_STREAM_COPY                    0x88E2
#define GL_STATIC_DRAW                    0x88B4
#define GL_STATIC_READ                    0x88B5
#define GL_STATIC_COPY                    0x88B6
#define GL_DYNAMIC_DRAW                   0x88E8
#define GL_DYNAMIC_READ                   0x88E9
#define GL_DYNAMIC_COPY                   0x88EA

/* Shaders (GL 2.0) */
#define GL_FRAGMENT_SHADER                0x8B30
#define GL_VERTEX_SHADER                  0x8B31
#define GL_GEOMETRY_SHADER                0x8DD9
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_VALIDATE_STATUS                0x8B83
#define GL_INFO_LOG_LENGTH                0x8B84
#define GL_ATTACHED_SHADERS               0x8B85
#define GL_ACTIVE_UNIFORMS                0x8B86
#define GL_ACTIVE_ATTRIBUTES              0x8B89
#define GL_DELETE_STATUS                  0x8B80
#define GL_SHADING_LANGUAGE_VERSION       0x8B8C

/* Framebuffer objects (GL 3.0) */
#define GL_FRAMEBUFFER                    0x8D40
#define GL_READ_FRAMEBUFFER               0x8CA8
#define GL_DRAW_FRAMEBUFFER               0x8CA9
#define GL_RENDERBUFFER                   0x8D41
#define GL_COLOR_ATTACHMENT0              0x8CE0
#define GL_COLOR_ATTACHMENT1              0x8CE1
#define GL_DEPTH_ATTACHMENT               0x8D00
#define GL_STENCIL_ATTACHMENT             0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT       0x821A
#define GL_FRAMEBUFFER_COMPLETE           0x8CD5
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT         0x8CD6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD7
#define GL_FRAMEBUFFER_UNSUPPORTED        0x8CDD
#define GL_FRAMEBUFFER_BINDING            0x8CA6
#define GL_RENDERBUFFER_BINDING           0x8CA7

/* Vertex Array Objects (GL 3.0) */
#define GL_VERTEX_ARRAY_BINDING           0x85B5

/* Texture units (GL 1.3+, always present in GL 3.3 core) */
#define GL_TEXTURE0   0x84C0
#define GL_TEXTURE1   0x84C1
#define GL_TEXTURE2   0x84C2
#define GL_TEXTURE3   0x84C3
#define GL_TEXTURE4   0x84C4
#define GL_TEXTURE5   0x84C5
#define GL_TEXTURE6   0x84C6
#define GL_TEXTURE7   0x84C7

/* Sampler / shader uniforms */
#define GL_SAMPLER_2D 0x8B5E

/* Misc */
#define GL_NO_ERROR                       0
#define GL_INVALID_ENUM                   0x0500
#define GL_INVALID_VALUE                  0x0501
#define GL_INVALID_OPERATION              0x0502
#define GL_OUT_OF_MEMORY                  0x0505
#define GL_VENDOR                         0x1F00
#define GL_RENDERER                       0x1F01
#define GL_VERSION                        0x1F02
#define GL_EXTENSIONS                     0x1F03
#define GL_MAJOR_VERSION                  0x821B
#define GL_MINOR_VERSION                  0x821C

/* ========================= GL 1.0/1.1 functions (in opengl32.dll) ========================= */
/* These are directly exported from opengl32 — no function pointer needed on Windows,
   but we declare them via pointer anyway for portability. */

/* ========================= FUNCTION POINTER TYPES ========================= */

/* GL 1.1 (some may be direct-linked, but we load all via the loader for portability) */
typedef void    (APIENTRY *PFNGLBINDTEXTUREPROC)         (GLenum, GLuint);
typedef void    (APIENTRY *PFNGLBLENDFUNCPROC)           (GLenum, GLenum);
typedef void    (APIENTRY *PFNGLCLEARPROC)               (GLbitfield);
typedef void    (APIENTRY *PFNGLCLEARCOLORPROC)          (GLfloat, GLfloat, GLfloat, GLfloat);
typedef void    (APIENTRY *PFNGLCLEARDEPTHPROC)          (GLdouble);
typedef void    (APIENTRY *PFNGLDELETETEXTURESPROC)      (GLsizei, const GLuint *);
typedef void    (APIENTRY *PFNGLDEPTHMASKPROC)           (GLboolean);
typedef void    (APIENTRY *PFNGLDEPTHFUNCPROC)           (GLenum);
typedef void    (APIENTRY *PFNGLDISABLEPROC)             (GLenum);
typedef void    (APIENTRY *PFNGLDRAWARRAYSPROC)          (GLenum, GLint, GLsizei);
typedef void    (APIENTRY *PFNGLDRAWELEMENTSPROC)        (GLenum, GLsizei, GLenum, const void *);
typedef void    (APIENTRY *PFNGLENABLEPROC)              (GLenum);
typedef void    (APIENTRY *PFNGLFINISHPROC)              (void);
typedef void    (APIENTRY *PFNGLFLUSHPROC)               (void);
typedef void    (APIENTRY *PFNGLGENTEXTURESPROC)         (GLsizei, GLuint *);
typedef GLenum  (APIENTRY *PFNGLGETERRORPROC)            (void);
typedef void    (APIENTRY *PFNGLGETINTEGERVPROC)         (GLenum, GLint *);
typedef const GLubyte *(APIENTRY *PFNGLGETSTRINGPROC)    (GLenum);
typedef void    (APIENTRY *PFNGLLINEWIDTHPROC)            (GLfloat);
typedef void    (APIENTRY *PFNGLPIXELSTOREI)             (GLenum, GLint);
typedef void    (APIENTRY *PFNGLPOLYGONOFFSETPROC)       (GLfloat, GLfloat);
typedef void    (APIENTRY *PFNGLSCISSORPROC)             (GLint, GLint, GLsizei, GLsizei);
typedef void    (APIENTRY *PFNGLTEXIMAGE2DPROC)          (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
typedef void    (APIENTRY *PFNGLTEXPARAMETERIPROC)       (GLenum, GLenum, GLint);
typedef void    (APIENTRY *PFNGLTEXPARAMETERF)           (GLenum, GLenum, GLfloat);
typedef void    (APIENTRY *PFNGLVIEWPORTPROC)            (GLint, GLint, GLsizei, GLsizei);

/* GL 1.3 */
typedef void    (APIENTRY *PFNGLACTIVETEXTUREPROC)       (GLenum);

/* GL 1.5 (VBO) */
typedef void    (APIENTRY *PFNGLBINDBUFFERPROC)          (GLenum, GLuint);
typedef void    (APIENTRY *PFNGLBUFFERDATAPROC)          (GLenum, GLsizeiptr, const void *, GLenum);
typedef void    (APIENTRY *PFNGLBUFFERSUBDATAPROC)       (GLenum, GLintptr, GLsizeiptr, const void *);
typedef void    (APIENTRY *PFNGLDELETEBUFFERSPROC)       (GLsizei, const GLuint *);
typedef void    (APIENTRY *PFNGLGENBUFFERSPROC)          (GLsizei, GLuint *);

/* GL 2.0 (shaders + attribs + blend eq) */
typedef void    (APIENTRY *PFNGLATTACHSHADERPROC)        (GLuint, GLuint);
typedef void    (APIENTRY *PFNGLBLENDEQUATIONPROC)       (GLenum);
typedef void    (APIENTRY *PFNGLBLENDEQUATIONSEPARATEPROC)(GLenum,GLenum);
typedef void    (APIENTRY *PFNGLBLENDFUNCSEPARATEPROC)   (GLenum,GLenum,GLenum,GLenum);
typedef void    (APIENTRY *PFNGLCOMPILESHADERPROC)       (GLuint);
typedef GLuint  (APIENTRY *PFNGLCREATEPROGRAMPROC)       (void);
typedef GLuint  (APIENTRY *PFNGLCREATESHADERPROC)        (GLenum);
typedef void    (APIENTRY *PFNGLDELETEPROGRAMPROC)       (GLuint);
typedef void    (APIENTRY *PFNGLDELETESHADERPROC)        (GLuint);
typedef void    (APIENTRY *PFNGLDETACHSHADERPROC)        (GLuint, GLuint);
typedef void    (APIENTRY *PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void    (APIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void    (APIENTRY *PFNGLGETPROGRAMINFOLOGPROC)   (GLuint, GLsizei, GLsizei *, GLchar *);
typedef void    (APIENTRY *PFNGLGETPROGRAMIVPROC)        (GLuint, GLenum, GLint *);
typedef void    (APIENTRY *PFNGLGETSHADERINFOLOGPROC)    (GLuint, GLsizei, GLsizei *, GLchar *);
typedef void    (APIENTRY *PFNGLGETSHADERIVPROC)         (GLuint, GLenum, GLint *);
typedef GLint   (APIENTRY *PFNGLGETUNIFORMLOCATIONPROC)  (GLuint, const GLchar *);
typedef void    (APIENTRY *PFNGLLINKPROGRAMPROC)         (GLuint);
typedef void    (APIENTRY *PFNGLSHADERSOURCEPROC)        (GLuint, GLsizei, const GLchar *const*, const GLint *);
typedef void    (APIENTRY *PFNGLUSEPROGRAMPROC)          (GLuint);
typedef void    (APIENTRY *PFNGLUNIFORM1FPROC)           (GLint, GLfloat);
typedef void    (APIENTRY *PFNGLUNIFORM1IPROC)           (GLint, GLint);
typedef void    (APIENTRY *PFNGLUNIFORM2FPROC)           (GLint, GLfloat, GLfloat);
typedef void    (APIENTRY *PFNGLUNIFORM3FPROC)           (GLint, GLfloat, GLfloat, GLfloat);
typedef void    (APIENTRY *PFNGLUNIFORM4FPROC)           (GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void    (APIENTRY *PFNGLUNIFORMMATRIX4FVPROC)    (GLint, GLsizei, GLboolean, const GLfloat *);
typedef void    (APIENTRY *PFNGLVERTEXATTRIBPOINTERPROC) (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);

/* GL 3.0 (VAO + FBO + RBO + getStringi) */
typedef void    (APIENTRY *PFNGLBINDFRAMEBUFFERPROC)     (GLenum, GLuint);
typedef void    (APIENTRY *PFNGLBINDRENDERBUFFERPROC)    (GLenum, GLuint);
typedef void    (APIENTRY *PFNGLBINDVERTEXARRAYPROC)     (GLuint);
typedef GLenum  (APIENTRY *PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum);
typedef void    (APIENTRY *PFNGLDELETEFRAMEBUFFERSPROC)  (GLsizei, const GLuint *);
typedef void    (APIENTRY *PFNGLDELETERENDERBUFFERSPROC) (GLsizei, const GLuint *);
typedef void    (APIENTRY *PFNGLDELETEVERTEXARRAYSPROC)  (GLsizei, const GLuint *);
typedef void    (APIENTRY *PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum,GLenum,GLenum,GLuint);
typedef void    (APIENTRY *PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum,GLenum,GLenum,GLuint,GLint);
typedef void    (APIENTRY *PFNGLGENFRAMEBUFFERSPROC)     (GLsizei, GLuint *);
typedef void    (APIENTRY *PFNGLGENRENDERBUFFERSPROC)    (GLsizei, GLuint *);
typedef void    (APIENTRY *PFNGLGENVERTEXARRAYSPROC)     (GLsizei, GLuint *);
typedef const GLubyte *(APIENTRY *PFNGLGETSTRINGIPROC)   (GLenum, GLuint);
typedef void    (APIENTRY *PFNGLRENDERBUFFERSTORAGEPROC) (GLenum, GLenum, GLsizei, GLsizei);
typedef void    (APIENTRY *PFNGLGENERATEMIPMAPPROC)      (GLenum);

/* GL 3.1 */
typedef void    (APIENTRY *PFNGLDRAWARRAYSINSTANCEDPROC) (GLenum, GLint, GLsizei, GLsizei);
typedef void    (APIENTRY *PFNGLDRAWELEMENTSINSTANCEDPROC)(GLenum,GLsizei,GLenum,const void*,GLsizei);

/* ========================= EXTERN FUNCTION POINTERS ========================= */

#ifndef GLAD_GL_IMPL_
#define GLAD_GL_EXTERN_ extern
#else
#define GLAD_GL_EXTERN_
#endif

/* GL 1.x */
GLAD_GL_EXTERN_ PFNGLBINDTEXTUREPROC          glad_glBindTexture;
GLAD_GL_EXTERN_ PFNGLBLENDFUNCPROC            glad_glBlendFunc;
GLAD_GL_EXTERN_ PFNGLCLEARPROC                glad_glClear;
GLAD_GL_EXTERN_ PFNGLCLEARCOLORPROC           glad_glClearColor;
GLAD_GL_EXTERN_ PFNGLCLEARDEPTHPROC           glad_glClearDepth;
GLAD_GL_EXTERN_ PFNGLDELETETEXTURESPROC       glad_glDeleteTextures;
GLAD_GL_EXTERN_ PFNGLDEPTHMASKPROC            glad_glDepthMask;
GLAD_GL_EXTERN_ PFNGLDEPTHFUNCPROC            glad_glDepthFunc;
GLAD_GL_EXTERN_ PFNGLDISABLEPROC              glad_glDisable;
GLAD_GL_EXTERN_ PFNGLDRAWARRAYSPROC           glad_glDrawArrays;
GLAD_GL_EXTERN_ PFNGLDRAWELEMENTSPROC         glad_glDrawElements;
GLAD_GL_EXTERN_ PFNGLENABLEPROC               glad_glEnable;
GLAD_GL_EXTERN_ PFNGLFINISHPROC               glad_glFinish;
GLAD_GL_EXTERN_ PFNGLFLUSHPROC                glad_glFlush;
GLAD_GL_EXTERN_ PFNGLGENTEXTURESPROC          glad_glGenTextures;
GLAD_GL_EXTERN_ PFNGLGETERRORPROC             glad_glGetError;
GLAD_GL_EXTERN_ PFNGLGETINTEGERVPROC          glad_glGetIntegerv;
GLAD_GL_EXTERN_ PFNGLGETSTRINGPROC            glad_glGetString;
GLAD_GL_EXTERN_ PFNGLLINEWIDTHPROC             glad_glLineWidth;
GLAD_GL_EXTERN_ PFNGLSCISSORPROC              glad_glScissor;
GLAD_GL_EXTERN_ PFNGLTEXIMAGE2DPROC           glad_glTexImage2D;
GLAD_GL_EXTERN_ PFNGLTEXPARAMETERIPROC        glad_glTexParameteri;
GLAD_GL_EXTERN_ PFNGLVIEWPORTPROC             glad_glViewport;
/* GL 1.3 */
GLAD_GL_EXTERN_ PFNGLACTIVETEXTUREPROC        glad_glActiveTexture;
/* GL 1.5 */
GLAD_GL_EXTERN_ PFNGLBINDBUFFERPROC           glad_glBindBuffer;
GLAD_GL_EXTERN_ PFNGLBUFFERDATAPROC           glad_glBufferData;
GLAD_GL_EXTERN_ PFNGLBUFFERSUBDATAPROC        glad_glBufferSubData;
GLAD_GL_EXTERN_ PFNGLDELETEBUFFERSPROC        glad_glDeleteBuffers;
GLAD_GL_EXTERN_ PFNGLGENBUFFERSPROC           glad_glGenBuffers;
/* GL 2.0 */
GLAD_GL_EXTERN_ PFNGLATTACHSHADERPROC         glad_glAttachShader;
GLAD_GL_EXTERN_ PFNGLBLENDEQUATIONPROC        glad_glBlendEquation;
GLAD_GL_EXTERN_ PFNGLBLENDEQUATIONSEPARATEPROC glad_glBlendEquationSeparate;
GLAD_GL_EXTERN_ PFNGLBLENDFUNCSEPARATEPROC    glad_glBlendFuncSeparate;
GLAD_GL_EXTERN_ PFNGLCOMPILESHADERPROC        glad_glCompileShader;
GLAD_GL_EXTERN_ PFNGLCREATEPROGRAMPROC        glad_glCreateProgram;
GLAD_GL_EXTERN_ PFNGLCREATESHADERPROC         glad_glCreateShader;
GLAD_GL_EXTERN_ PFNGLDELETEPROGRAMPROC        glad_glDeleteProgram;
GLAD_GL_EXTERN_ PFNGLDELETESHADERPROC         glad_glDeleteShader;
GLAD_GL_EXTERN_ PFNGLDISABLEVERTEXATTRIBARRAYPROC glad_glDisableVertexAttribArray;
GLAD_GL_EXTERN_ PFNGLENABLEVERTEXATTRIBARRAYPROC  glad_glEnableVertexAttribArray;
GLAD_GL_EXTERN_ PFNGLGETPROGRAMINFOLOGPROC    glad_glGetProgramInfoLog;
GLAD_GL_EXTERN_ PFNGLGETPROGRAMIVPROC         glad_glGetProgramiv;
GLAD_GL_EXTERN_ PFNGLGETSHADERINFOLOGPROC     glad_glGetShaderInfoLog;
GLAD_GL_EXTERN_ PFNGLGETSHADERIVPROC          glad_glGetShaderiv;
GLAD_GL_EXTERN_ PFNGLGETUNIFORMLOCATIONPROC   glad_glGetUniformLocation;
GLAD_GL_EXTERN_ PFNGLLINKPROGRAMPROC          glad_glLinkProgram;
GLAD_GL_EXTERN_ PFNGLSHADERSOURCEPROC         glad_glShaderSource;
GLAD_GL_EXTERN_ PFNGLUSEPROGRAMPROC           glad_glUseProgram;
GLAD_GL_EXTERN_ PFNGLUNIFORM1FPROC            glad_glUniform1f;
GLAD_GL_EXTERN_ PFNGLUNIFORM1IPROC            glad_glUniform1i;
GLAD_GL_EXTERN_ PFNGLUNIFORM2FPROC            glad_glUniform2f;
GLAD_GL_EXTERN_ PFNGLUNIFORM3FPROC            glad_glUniform3f;
GLAD_GL_EXTERN_ PFNGLUNIFORM4FPROC            glad_glUniform4f;
GLAD_GL_EXTERN_ PFNGLUNIFORMMATRIX4FVPROC     glad_glUniformMatrix4fv;
GLAD_GL_EXTERN_ PFNGLVERTEXATTRIBPOINTERPROC  glad_glVertexAttribPointer;
/* GL 3.0 */
GLAD_GL_EXTERN_ PFNGLBINDFRAMEBUFFERPROC      glad_glBindFramebuffer;
GLAD_GL_EXTERN_ PFNGLBINDRENDERBUFFERPROC     glad_glBindRenderbuffer;
GLAD_GL_EXTERN_ PFNGLBINDVERTEXARRAYPROC      glad_glBindVertexArray;
GLAD_GL_EXTERN_ PFNGLCHECKFRAMEBUFFERSTATUSPROC glad_glCheckFramebufferStatus;
GLAD_GL_EXTERN_ PFNGLDELETEFRAMEBUFFERSPROC   glad_glDeleteFramebuffers;
GLAD_GL_EXTERN_ PFNGLDELETERENDERBUFFERSPROC  glad_glDeleteRenderbuffers;
GLAD_GL_EXTERN_ PFNGLDELETEVERTEXARRAYSPROC   glad_glDeleteVertexArrays;
GLAD_GL_EXTERN_ PFNGLFRAMEBUFFERRENDERBUFFERPROC glad_glFramebufferRenderbuffer;
GLAD_GL_EXTERN_ PFNGLFRAMEBUFFERTEXTURE2DPROC glad_glFramebufferTexture2D;
GLAD_GL_EXTERN_ PFNGLGENFRAMEBUFFERSPROC      glad_glGenFramebuffers;
GLAD_GL_EXTERN_ PFNGLGENRENDERBUFFERSPROC     glad_glGenRenderbuffers;
GLAD_GL_EXTERN_ PFNGLGENVERTEXARRAYSPROC      glad_glGenVertexArrays;
GLAD_GL_EXTERN_ PFNGLGETSTRINGIPROC           glad_glGetStringi;
GLAD_GL_EXTERN_ PFNGLRENDERBUFFERSTORAGEPROC  glad_glRenderbufferStorage;
GLAD_GL_EXTERN_ PFNGLGENERATEMIPMAPPROC       glad_glGenerateMipmap;

/* ========================= MACRO ALIASES ========================= */

#define glBindTexture            glad_glBindTexture
#define glBlendFunc              glad_glBlendFunc
#define glClear                  glad_glClear
#define glClearColor             glad_glClearColor
#define glClearDepth             glad_glClearDepth
#define glDeleteTextures         glad_glDeleteTextures
#define glDepthMask              glad_glDepthMask
#define glDepthFunc              glad_glDepthFunc
#define glDisable                glad_glDisable
#define glDrawArrays             glad_glDrawArrays
#define glDrawElements           glad_glDrawElements
#define glEnable                 glad_glEnable
#define glFinish                 glad_glFinish
#define glFlush                  glad_glFlush
#define glGenTextures            glad_glGenTextures
#define glGetError               glad_glGetError
#define glGetIntegerv            glad_glGetIntegerv
#define glGetString              glad_glGetString
#define glLineWidth              glad_glLineWidth
#define glScissor                glad_glScissor
#define glTexImage2D             glad_glTexImage2D
#define glTexParameteri          glad_glTexParameteri
#define glViewport               glad_glViewport
#define glActiveTexture          glad_glActiveTexture
#define glBindBuffer             glad_glBindBuffer
#define glBufferData             glad_glBufferData
#define glBufferSubData          glad_glBufferSubData
#define glDeleteBuffers          glad_glDeleteBuffers
#define glGenBuffers             glad_glGenBuffers
#define glAttachShader           glad_glAttachShader
#define glBlendEquation          glad_glBlendEquation
#define glBlendEquationSeparate  glad_glBlendEquationSeparate
#define glBlendFuncSeparate      glad_glBlendFuncSeparate
#define glCompileShader          glad_glCompileShader
#define glCreateProgram          glad_glCreateProgram
#define glCreateShader           glad_glCreateShader
#define glDeleteProgram          glad_glDeleteProgram
#define glDeleteShader           glad_glDeleteShader
#define glDisableVertexAttribArray glad_glDisableVertexAttribArray
#define glEnableVertexAttribArray glad_glEnableVertexAttribArray
#define glGetProgramInfoLog      glad_glGetProgramInfoLog
#define glGetProgramiv           glad_glGetProgramiv
#define glGetShaderInfoLog       glad_glGetShaderInfoLog
#define glGetShaderiv            glad_glGetShaderiv
#define glGetUniformLocation     glad_glGetUniformLocation
#define glLinkProgram            glad_glLinkProgram
#define glShaderSource           glad_glShaderSource
#define glUseProgram             glad_glUseProgram
#define glUniform1f              glad_glUniform1f
#define glUniform1i              glad_glUniform1i
#define glUniform2f              glad_glUniform2f
#define glUniform3f              glad_glUniform3f
#define glUniform4f              glad_glUniform4f
#define glUniformMatrix4fv       glad_glUniformMatrix4fv
#define glVertexAttribPointer    glad_glVertexAttribPointer
#define glBindFramebuffer        glad_glBindFramebuffer
#define glBindRenderbuffer       glad_glBindRenderbuffer
#define glBindVertexArray        glad_glBindVertexArray
#define glCheckFramebufferStatus glad_glCheckFramebufferStatus
#define glDeleteFramebuffers     glad_glDeleteFramebuffers
#define glDeleteRenderbuffers    glad_glDeleteRenderbuffers
#define glDeleteVertexArrays     glad_glDeleteVertexArrays
#define glFramebufferRenderbuffer glad_glFramebufferRenderbuffer
#define glFramebufferTexture2D   glad_glFramebufferTexture2D
#define glGenFramebuffers        glad_glGenFramebuffers
#define glGenRenderbuffers       glad_glGenRenderbuffers
#define glGenVertexArrays        glad_glGenVertexArrays
#define glGetStringi             glad_glGetStringi
#define glRenderbufferStorage    glad_glRenderbufferStorage
#define glGenerateMipmap         glad_glGenerateMipmap

/* ========================= GLAD API ========================= */

typedef void* (*GLADloadproc)(const char* name);

/** Load GL 3.3 core functions using the given function loader.
 *  Returns 1 on success, 0 on failure.
 *  Typical usage:  gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
 */
int gladLoadGLLoader(GLADloadproc load);

#ifdef __cplusplus
}
#endif

#endif /* __glad_h_ */
