// Phone/tablet UI shell.
//
// The desktop shell (panels.cpp: DrawSidebar / DrawViewportPanel /
// DrawToolsPanel) packs three resizable columns side by side, which stops
// working the moment the UI is scaled up for a high-DPI phone: nothing fits,
// the splitters are too thin to hit with a finger, and the tools column
// overlaps the system navigation bar.
//
// This shell renders the SAME content functions from panels.h, but one page at
// a time behind a touch-sized navigation rail, with drag-to-scroll, swipe
// paging and system-inset padding. No astronomy code is duplicated or forked;
// only the arrangement differs.
#ifndef SXWNL_GUI_UI_MOBILE_H
#define SXWNL_GUI_UI_MOBILE_H

#include "camera.h"
#include "panels.h"
#include "renderer.h"
#include "scene.h"

namespace sx {

// System window insets in physical pixels (status bar, navigation bar, display
// cutout). Android feeds these in from the Java side; anything left at 0 simply
// means "no reserved area on that edge".
void SetSafeAreaInsets(float left, float top, float right, float bottom);

// Draws the complete mobile UI for one frame. Replaces the DrawMainMenuBar +
// DrawSidebar + DrawViewportPanel + DrawToolsPanel + DrawPanelSplitters
// sequence used by the desktop shell.
void DrawMobileUI(Renderer& renderer, Scene& scene, gx::OrbitCamera& cam,
                  RenderOptions& ropt, PanelState& ps);

} // namespace sx
#endif
