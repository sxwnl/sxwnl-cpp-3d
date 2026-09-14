// Mesh-to-body-frame rotations for the bundled OBJ models.
//
// Header-only and free of any GL dependency on purpose: these are measured
// constants, and a measured constant nobody can check is how the Moon came to
// face 48 degrees the wrong way. moonphase_test pins them against published
// selenography, and it cannot link the renderer.
#ifndef SXWNL_GUI_MESH_FRAMES_H
#define SXWNL_GUI_MESH_FRAMES_H

#include <string>
#include "mathx.h"

namespace sx {

// ============================================================================
//  Per-mesh axis calibration.
//
//  Each bundled mesh sits in its own arbitrary local frame, so before any
//  astronomical spin/tilt is applied the mesh has to be carried into the
//  canonical geographic frame: +Y = north pole, +Z = prime meridian,
//  +X = 90 deg east. These matrices were measured from the meshes' own
//  vertex/UV data (pole axis from where the texture's latitude parameter is
//  stationary, meridian phase from a least-squares fit over every vertex).
//
//  Spinning a body about the wrong axis makes its texture wobble once per
//  rotation instead of turning in place, which is what these correct. The
//  offenders are Earth (54 deg out), the Moon (79 deg) and Saturn (26.5 deg);
//  Jupiter is 4.5 deg out, enough to make its bands visibly drift up and down.
//  The remaining meshes already have their pole on local +Y and only need the
//  meridian phase.
//
//  Jupiter and the Moon do not use an equirectangular UV layout -- the Moon's
//  is a cube-net atlas -- so a whole-vertex least-squares fit leaves a 7-10 deg
//  median residual and only its pole is trustworthy. For the Moon the meridian
//  was measured a second way instead, off the map itself: sample the texture
//  through the mesh's own UVs, cluster the mare-dark points on the sphere, and
//  match the isolated ones against selenography. Mare Crisium (17.0N 59.1E) and
//  Mare Moscoviense (27.3N 147.9E) both landed 48.3 deg east of where they
//  belong, and Mare Smythii (1.3N 87.5E) 43.8 deg -- it sits on the limb, where
//  the sampling foreshortens it. The 48.3 deg is taken out below; the same
//  clusters then come back within 3 deg in latitude, which is the pole
//  confirming itself. Before that correction the Moon faced the camera 48 deg
//  off in the solar-system view, and the moon-phase panel, which applied no fix
//  at all, showed the far side.
// ============================================================================
inline gx::Mat4 meshAxisFixFor(const std::string& pinyin) {
    if (pinyin == "earth")
        return gx::fromColumns({-0.237068f,-0.809406f,-0.537272f},
                               {+0.342213f,-0.587165f,+0.733571f},
                               {-0.909225f,-0.009956f,+0.416187f});
    if (pinyin == "jupiter")
        return gx::fromColumns({-0.048441f,-0.078187f,-0.995761f},
                               {+0.002369f,+0.996920f,-0.078393f},
                               {+0.998823f,-0.006156f,-0.048106f});
    if (pinyin == "saturn")
        return gx::fromColumns({+0.108912f,+0.217127f,+0.970049f},
                               {-0.419315f,+0.894819f,-0.153209f},
                               {-0.901284f,-0.390070f,+0.188501f});
    if (pinyin == "moon")
        return gx::fromColumns({+0.367435f,+0.485555f,-0.793238f},
                               {+0.839843f,+0.193201f,+0.507284f},
                               {+0.399569f,-0.852590f,-0.336801f});
    // Mercury, Venus, Mars, Uranus, Neptune and the Sun share one frame:
    // pole already on +Y, prime meridian rotated 65.9 deg away.
    if (pinyin == "mercury" || pinyin == "venus" || pinyin == "mars" ||
        pinyin == "uranus"  || pinyin == "neptune" || pinyin == "sun")
        return gx::fromColumns({+0.408181f, 0.0f, +0.912901f},
                               { 0.0f,      1.0f,  0.0f},
                               {-0.912901f, 0.0f, +0.408181f});
    return gx::Mat4::identity();
}

} // namespace sx

#endif // SXWNL_GUI_MESH_FRAMES_H
