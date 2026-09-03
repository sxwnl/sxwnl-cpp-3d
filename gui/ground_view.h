// 地面视角: the selected eclipse as it looks from a place on the ground.
//
// Drawn into the 3-D viewport rather than into a page of its own. The solar
// system view and this one are the same subject seen from two distances - the
// shadow cone from outside, and standing in it - so switching between them is a
// change of viewpoint, not a change of page.
#ifndef SXWNL_GUI_GROUND_VIEW_H
#define SXWNL_GUI_GROUND_VIEW_H

#include "panels.h"

namespace sx {

// Fills the viewport rectangle at `origin` with the sky over the observer.
// Returns true when it has taken the pointer, so the caller leaves the orbit
// camera alone.
bool DrawGroundEclipseView(Scene& scene, PanelState& ps, const EclipseEvent& e,
                           ImVec2 origin, float w, float h, bool hovered);

} // namespace sx

#endif
