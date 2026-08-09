#ifndef SXWNL_GUI_GLES_GL_COMPAT_H
#define SXWNL_GUI_GLES_GL_COMPAT_H

#if defined(SXWNL_USE_GLES)
#include <GLES3/gl3.h>
#define SXWNL_GLSL_VERSION \
    "#version 300 es\n" \
    "precision highp float;\n" \
    "precision highp int;\n"
#else
#include <glad/glad.h>
#define SXWNL_GLSL_VERSION "#version 330 core\n"
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
