// Upcoming astronomical events for one body, for the 3-D view's info card.
//
// Engine-level and platform independent, like eclipse.h next to it: the GUI
// asks "what is about to happen to this body" and gets a time-ordered list
// back. Times use the same J2000-relative TD convention as EclipseEvent, so
// eclipseTdToUtcJD() converts them for display.
#ifndef EPH_ASTRO_EVENTS_H
#define EPH_ASTRO_EVENTS_H

#include <string>
#include <vector>

enum AstroEventKind
{
    AE_GreatestElongEast = 0, // 东大距
    AE_GreatestElongWest,     // 西大距
    AE_Conjunction,           // 合(外行星) / 上合(内行星)
    AE_InferiorConjunction,   // 下合(内行星)
    AE_Opposition,            // 冲(外行星)
    AE_Transit,               // 凌日(水星/金星下合且黄纬差很小)
    AE_StationDirect,         // 顺留
    AE_StationRetrograde,     // 逆留
    AE_ConjunctionMoon,       // 合月
    AE_NewMoon,               // 朔
    AE_FirstQuarter,          // 上弦
    AE_FullMoon,              // 望
    AE_LastQuarter,           // 下弦
    AE_SolarEclipse,          // 日食
    AE_LunarEclipse,          // 月食
    AE_Perihelion,            // 地球过近日点
    AE_Aphelion,              // 地球过远日点
    AE_MarchEquinox,          // 春分
    AE_JuneSolstice,          // 夏至
    AE_SeptemberEquinox,      // 秋分
    AE_DecemberSolstice,      // 冬至
    AE_KindCount
};

struct AstroEvent
{
    double jdTd = 0.0;   // J2000-relative TD (as EclipseEvent::maximumTd)
    int kind = 0;        // AstroEventKind
    std::string detail;  // short qualifier, e.g. "27.4°" or "全环食"; may be empty
};

// Next `maxCount` events for a body, soonest first.
//
// `xt` is the engine's own body index, matching BodyInfo::xt:
//   -1 Sun, 0 Earth, 1..7 Mercury..Neptune, 8 Pluto, 10 Moon.
// `jdTdNow` is the search origin (J2000-relative TD).
//
// Every entry is strictly after jdTdNow. The search is deliberate rather than
// cheap - a handful of full-precision VSOP evaluations per event type - so
// callers should cache the result rather than call it per frame.
std::vector<AstroEvent> upcomingAstroEvents(int xt, double jdTdNow, int maxCount);

// Display name for a kind. `chinese` false yields the English label.
const char* astroEventName(int kind, bool chinese);

#endif
