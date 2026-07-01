/*
 * Pre-generated GLAD loader for OpenGL 3.3 Core Profile.
 * SPDX-License-Identifier: MIT
 */
#define GLAD_GL_IMPL_
#include "glad/glad.h"

#include <string.h>

/* ====================== Platform proc-address lookup ====================== */

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>

static HMODULE s_opengl32 = NULL;

static void* glad_win32_get_proc(const char* name) {
    void* p = (void*)wglGetProcAddress(name);
    if (!p || p == (void*)0x1 || p == (void*)0x2 ||
              p == (void*)0x3 || p == (void*)-1) {
        if (!s_opengl32)
            s_opengl32 = LoadLibraryA("opengl32.dll");
        if (s_opengl32)
            p = (void*)GetProcAddress(s_opengl32, name);
    }
    return p;
}
#define GLAD_GET_PROC(name) glad_win32_get_proc(name)

#elif defined(__APPLE__)
#  include <dlfcn.h>
static void* s_libgl = NULL;
static void* glad_apple_get_proc(const char* name) {
    if (!s_libgl)
        s_libgl = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL", RTLD_LAZY);
    return s_libgl ? dlsym(s_libgl, name) : NULL;
}
#define GLAD_GET_PROC(name) glad_apple_get_proc(name)

#else /* Linux / other POSIX */
#  include <dlfcn.h>
/* Try glXGetProcAddressARB first (most portable on Linux) */
typedef void* (*PFNGLXGETPROCADDRESSARBPROC)(const unsigned char*);
static PFNGLXGETPROCADDRESSARBPROC s_glx_get = NULL;
static void* s_libgl = NULL;

static void* glad_linux_get_proc(const char* name) {
    if (!s_glx_get) {
        if (!s_libgl) s_libgl = dlopen("libGL.so.1", RTLD_LAZY | RTLD_GLOBAL);
        if (!s_libgl) s_libgl = dlopen("libGL.so",   RTLD_LAZY | RTLD_GLOBAL);
        if (s_libgl)
            s_glx_get = (PFNGLXGETPROCADDRESSARBPROC)dlsym(s_libgl, "glXGetProcAddressARB");
    }
    void* p = s_glx_get ? s_glx_get((const unsigned char*)name) : NULL;
    if (!p && s_libgl) p = dlsym(s_libgl, name);
    return p;
}
#define GLAD_GET_PROC(name) glad_linux_get_proc(name)
#endif

/* ====================== Function pointer definitions ====================== */

PFNGLBINDTEXTUREPROC          glad_glBindTexture          = NULL;
PFNGLBLENDFUNCPROC            glad_glBlendFunc             = NULL;
PFNGLCLEARPROC                glad_glClear                 = NULL;
PFNGLCLEARCOLORPROC           glad_glClearColor            = NULL;
PFNGLCLEARDEPTHPROC           glad_glClearDepth            = NULL;
PFNGLDELETETEXTURESPROC       glad_glDeleteTextures        = NULL;
PFNGLDEPTHMASKPROC            glad_glDepthMask             = NULL;
PFNGLDEPTHFUNCPROC            glad_glDepthFunc             = NULL;
PFNGLDISABLEPROC              glad_glDisable               = NULL;
PFNGLDRAWARRAYSPROC           glad_glDrawArrays            = NULL;
PFNGLDRAWELEMENTSPROC         glad_glDrawElements          = NULL;
PFNGLENABLEPROC               glad_glEnable                = NULL;
PFNGLFINISHPROC               glad_glFinish                = NULL;
PFNGLFLUSHPROC                glad_glFlush                 = NULL;
PFNGLGENTEXTURESPROC          glad_glGenTextures           = NULL;
PFNGLGETERRORPROC             glad_glGetError              = NULL;
PFNGLGETINTEGERVPROC          glad_glGetIntegerv           = NULL;
PFNGLGETSTRINGPROC            glad_glGetString             = NULL;
PFNGLLINEWIDTHPROC            glad_glLineWidth             = NULL;
PFNGLSCISSORPROC              glad_glScissor               = NULL;
PFNGLTEXIMAGE2DPROC           glad_glTexImage2D            = NULL;
PFNGLTEXPARAMETERIPROC        glad_glTexParameteri         = NULL;
PFNGLVIEWPORTPROC             glad_glViewport              = NULL;

PFNGLACTIVETEXTUREPROC        glad_glActiveTexture         = NULL;

PFNGLBINDBUFFERPROC           glad_glBindBuffer            = NULL;
PFNGLBUFFERDATAPROC           glad_glBufferData            = NULL;
PFNGLBUFFERSUBDATAPROC        glad_glBufferSubData         = NULL;
PFNGLDELETEBUFFERSPROC        glad_glDeleteBuffers         = NULL;
PFNGLGENBUFFERSPROC           glad_glGenBuffers            = NULL;

PFNGLATTACHSHADERPROC         glad_glAttachShader          = NULL;
PFNGLBLENDEQUATIONPROC        glad_glBlendEquation         = NULL;
PFNGLBLENDEQUATIONSEPARATEPROC glad_glBlendEquationSeparate = NULL;
PFNGLBLENDFUNCSEPARATEPROC    glad_glBlendFuncSeparate     = NULL;
PFNGLCOMPILESHADERPROC        glad_glCompileShader         = NULL;
PFNGLCREATEPROGRAMPROC        glad_glCreateProgram         = NULL;
PFNGLCREATESHADERPROC         glad_glCreateShader          = NULL;
PFNGLDELETEPROGRAMPROC        glad_glDeleteProgram         = NULL;
PFNGLDELETESHADERPROC         glad_glDeleteShader          = NULL;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glad_glDisableVertexAttribArray = NULL;
PFNGLENABLEVERTEXATTRIBARRAYPROC  glad_glEnableVertexAttribArray  = NULL;
PFNGLGETPROGRAMINFOLOGPROC    glad_glGetProgramInfoLog     = NULL;
PFNGLGETPROGRAMIVPROC         glad_glGetProgramiv          = NULL;
PFNGLGETSHADERINFOLOGPROC     glad_glGetShaderInfoLog      = NULL;
PFNGLGETSHADERIVPROC          glad_glGetShaderiv           = NULL;
PFNGLGETUNIFORMLOCATIONPROC   glad_glGetUniformLocation    = NULL;
PFNGLLINKPROGRAMPROC          glad_glLinkProgram           = NULL;
PFNGLSHADERSOURCEPROC         glad_glShaderSource          = NULL;
PFNGLUSEPROGRAMPROC           glad_glUseProgram            = NULL;
PFNGLUNIFORM1FPROC            glad_glUniform1f             = NULL;
PFNGLUNIFORM1IPROC            glad_glUniform1i             = NULL;
PFNGLUNIFORM2FPROC            glad_glUniform2f             = NULL;
PFNGLUNIFORM3FPROC            glad_glUniform3f             = NULL;
PFNGLUNIFORM4FPROC            glad_glUniform4f             = NULL;
PFNGLUNIFORMMATRIX4FVPROC     glad_glUniformMatrix4fv      = NULL;
PFNGLVERTEXATTRIBPOINTERPROC  glad_glVertexAttribPointer   = NULL;

PFNGLBINDFRAMEBUFFERPROC      glad_glBindFramebuffer       = NULL;
PFNGLBINDRENDERBUFFERPROC     glad_glBindRenderbuffer      = NULL;
PFNGLBINDVERTEXARRAYPROC      glad_glBindVertexArray       = NULL;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glad_glCheckFramebufferStatus = NULL;
PFNGLDELETEFRAMEBUFFERSPROC   glad_glDeleteFramebuffers    = NULL;
PFNGLDELETERENDERBUFFERSPROC  glad_glDeleteRenderbuffers   = NULL;
PFNGLDELETEVERTEXARRAYSPROC   glad_glDeleteVertexArrays    = NULL;
PFNGLFRAMEBUFFERRENDERBUFFERPROC glad_glFramebufferRenderbuffer = NULL;
PFNGLFRAMEBUFFERTEXTURE2DPROC glad_glFramebufferTexture2D  = NULL;
PFNGLGENFRAMEBUFFERSPROC      glad_glGenFramebuffers       = NULL;
PFNGLGENRENDERBUFFERSPROC     glad_glGenRenderbuffers      = NULL;
PFNGLGENVERTEXARRAYSPROC      glad_glGenVertexArrays       = NULL;
PFNGLGETSTRINGIPROC           glad_glGetStringi            = NULL;
PFNGLRENDERBUFFERSTORAGEPROC  glad_glRenderbufferStorage   = NULL;
PFNGLGENERATEMIPMAPPROC       glad_glGenerateMipmap        = NULL;

/* ====================== Loader ====================== */

static void* load(GLADloadproc get, const char* name) {
    return get ? get(name) : GLAD_GET_PROC(name);
}

int gladLoadGLLoader(GLADloadproc get) {
#define LOAD(fn, sym) glad_gl##fn = (PFNGL##sym##PROC) load(get, "gl" #fn)
    LOAD(BindTexture,             BINDTEXTURE);
    LOAD(BlendFunc,               BLENDFUNC);
    LOAD(Clear,                   CLEAR);
    LOAD(ClearColor,              CLEARCOLOR);
    LOAD(ClearDepth,              CLEARDEPTH);
    LOAD(DeleteTextures,          DELETETEXTURES);
    LOAD(DepthMask,               DEPTHMASK);
    LOAD(DepthFunc,               DEPTHFUNC);
    LOAD(Disable,                 DISABLE);
    LOAD(DrawArrays,              DRAWARRAYS);
    LOAD(DrawElements,            DRAWELEMENTS);
    LOAD(Enable,                  ENABLE);
    LOAD(Finish,                  FINISH);
    LOAD(Flush,                   FLUSH);
    LOAD(GenTextures,             GENTEXTURES);
    LOAD(GetError,                GETERROR);
    LOAD(GetIntegerv,             GETINTEGERV);
    LOAD(GetString,               GETSTRING);
    LOAD(LineWidth,               LINEWIDTH);
    LOAD(Scissor,                 SCISSOR);
    LOAD(TexImage2D,              TEXIMAGE2D);
    LOAD(TexParameteri,           TEXPARAMETERI);
    LOAD(Viewport,                VIEWPORT);

    LOAD(ActiveTexture,           ACTIVETEXTURE);

    LOAD(BindBuffer,              BINDBUFFER);
    LOAD(BufferData,              BUFFERDATA);
    LOAD(BufferSubData,           BUFFERSUBDATA);
    LOAD(DeleteBuffers,           DELETEBUFFERS);
    LOAD(GenBuffers,              GENBUFFERS);

    LOAD(AttachShader,            ATTACHSHADER);
    LOAD(BlendEquation,           BLENDEQUATION);
    LOAD(BlendEquationSeparate,   BLENDEQUATIONSEPARATE);
    LOAD(BlendFuncSeparate,       BLENDFUNCSEPARATE);
    LOAD(CompileShader,           COMPILESHADER);
    LOAD(CreateProgram,           CREATEPROGRAM);
    LOAD(CreateShader,            CREATESHADER);
    LOAD(DeleteProgram,           DELETEPROGRAM);
    LOAD(DeleteShader,            DELETESHADER);
    LOAD(DisableVertexAttribArray, DISABLEVERTEXATTRIBARRAY);
    LOAD(EnableVertexAttribArray, ENABLEVERTEXATTRIBARRAY);
    LOAD(GetProgramInfoLog,       GETPROGRAMINFOLOG);
    LOAD(GetProgramiv,            GETPROGRAMIV);
    LOAD(GetShaderInfoLog,        GETSHADERINFOLOG);
    LOAD(GetShaderiv,             GETSHADERIV);
    LOAD(GetUniformLocation,      GETUNIFORMLOCATION);
    LOAD(LinkProgram,             LINKPROGRAM);
    LOAD(ShaderSource,            SHADERSOURCE);
    LOAD(UseProgram,              USEPROGRAM);
    LOAD(Uniform1f,               UNIFORM1F);
    LOAD(Uniform1i,               UNIFORM1I);
    LOAD(Uniform2f,               UNIFORM2F);
    LOAD(Uniform3f,               UNIFORM3F);
    LOAD(Uniform4f,               UNIFORM4F);
    LOAD(UniformMatrix4fv,        UNIFORMMATRIX4FV);
    LOAD(VertexAttribPointer,     VERTEXATTRIBPOINTER);

    LOAD(BindFramebuffer,         BINDFRAMEBUFFER);
    LOAD(BindRenderbuffer,        BINDRENDERBUFFER);
    LOAD(BindVertexArray,         BINDVERTEXARRAY);
    LOAD(CheckFramebufferStatus,  CHECKFRAMEBUFFERSTATUS);
    LOAD(DeleteFramebuffers,      DELETEFRAMEBUFFERS);
    LOAD(DeleteRenderbuffers,     DELETERENDERBUFFERS);
    LOAD(DeleteVertexArrays,      DELETEVERTEXARRAYS);
    LOAD(FramebufferRenderbuffer, FRAMEBUFFERRENDERBUFFER);
    LOAD(FramebufferTexture2D,    FRAMEBUFFERTEXTURE2D);
    LOAD(GenFramebuffers,         GENFRAMEBUFFERS);
    LOAD(GenRenderbuffers,        GENRENDERBUFFERS);
    LOAD(GenVertexArrays,         GENVERTEXARRAYS);
    LOAD(GetStringi,              GETSTRINGI);
    LOAD(RenderbufferStorage,     RENDERBUFFERSTORAGE);
    LOAD(GenerateMipmap,          GENERATEMIPMAP);
#undef LOAD

    /* Minimal sanity check: if these core 2.0 symbols are null, loading failed */
    return (glad_glCreateProgram != NULL && glad_glGenVertexArrays != NULL) ? 1 : 0;
}
