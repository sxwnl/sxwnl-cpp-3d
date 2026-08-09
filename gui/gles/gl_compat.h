#ifndef SXWNL_GUI_GLES_GL_COMPAT_H
#define SXWNL_GUI_GLES_GL_COMPAT_H

#if defined(SXWNL_USE_GLES)
#include <GLES3/gl3.h>
#define SXWNL_GLSL_VERSION_DIRECTIVE "#version 300 es"
#define SXWNL_GLSL_VERSION \
    SXWNL_GLSL_VERSION_DIRECTIVE "\n" \
    "precision highp float;\n" \
    "precision highp int;\n"
#else
#include <glad/glad.h>
#define SXWNL_GLSL_VERSION_DIRECTIVE "#version 330 core"
#define SXWNL_GLSL_VERSION SXWNL_GLSL_VERSION_DIRECTIVE "\n"
#endif

inline void sxwnlSetLineSmoothing(bool enabled) {
#if !defined(SXWNL_USE_GLES)
    if (enabled) glEnable(GL_LINE_SMOOTH);
    else glDisable(GL_LINE_SMOOTH);
#else
    (void)enabled;
#endif
}

#endif
