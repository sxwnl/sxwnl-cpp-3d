// 地面视角: what one spot on Earth sees of the Sun and the Moon at one instant,
// and - when the two discs meet, or the Moon enters Earth's shadow - of the
// eclipse in progress there.
//
// Engine-level and platform independent like eclipse.h and astro_events.h next
// to it: the GUI says "stand here at this moment and look up" and gets back
// apparent positions, apparent sizes, and how much of one disc the other
// covers. Times use the same J2000-relative TD convention as EclipseEvent.
#ifndef EPH_SKY_VIEW_H
#define EPH_SKY_VIEW_H

struct SkyBodyView
{
    bool   valid = false;
    double azimuthDeg = 0.0;   // from north, increasing eastward
    double altitudeDeg = 0.0;  // above the horizon, atmospheric refraction included
    double radiusDeg = 0.0;    // apparent radius as seen from the observer
};

// Offsets below are gnomonic (tangent-plane) coordinates around whichever body
// the view is centred on, in degrees: +x to the observer's right when facing
// that body, +y towards the zenith. They carry the eclipse geometry, so they
// are built from the unrefracted topocentric places - refraction shifts both
// discs together and would otherwise leak a spurious wobble into the overlap.
struct SkyView
{
    bool   valid = false;
    double jdTd = 0.0;

    SkyBodyView sun;
    SkyBodyView moon;

    // ---- Solar eclipse, as seen from this spot ----------------------------
    double moonDx = 0.0, moonDy = 0.0; // Moon's centre relative to the Sun's
    double separationDeg = 0.0;        // between the two centres
    double magnitude = 0.0;            // covered fraction of the Sun's diameter
    double obscuration = 0.0;          // covered fraction of the Sun's area
    bool   total = false;              // Moon's disc wholly covers the Sun's
    bool   annular = false;            // Sun's disc wholly surrounds the Moon's

    // ---- Lunar eclipse: Earth's shadow at the Moon's distance -------------
    // Offsets are relative to the Moon's centre. Both the shadow and the Moon
    // are taken geocentrically here, which is what a lunar eclipse is: every
    // observer who can see the Moon sees the same shadow on it.
    double shadowDx = 0.0, shadowDy = 0.0;
    double umbraRadiusDeg = 0.0;
    double penumbraRadiusDeg = 0.0;
    double umbralMagnitude = 0.0;    // Moon's diameter covered by the umbra
    double penumbralMagnitude = 0.0; // ... by the penumbra
    double moonIllum = 0.0;          // lit fraction of the Moon's disc (phase)

    // ---- Sky ---------------------------------------------------------------
    // Daylight left, 1 = the Sun is its normal self, 0 = totality. A perceptual
    // curve rather than a photometric one: the sky holds up until the last few
    // percent of the Sun is gone, and then goes fast.
    double daylight = 1.0;
};

// One instant, one place. `altitudeKm` is the observer's height above sea level.
SkyView computeSkyView(double jdTd, double lonDeg, double latDeg,
                       double altitudeKm = 0.0);

// The place on Earth with the Moon straight overhead at that instant.
//
// For a lunar eclipse this is the best seat in the house: the eclipse is the
// same everywhere it can be seen at all, so the only thing left to choose is
// somewhere the Moon is high rather than under the horizon. A solar eclipse has
// a centre line to stand on instead; this is the lunar answer to it.
struct SubPoint
{
    bool   valid = false;
    double lonDeg = 0.0;
    double latDeg = 0.0;
};
SubPoint subLunarPoint(double jdTd);

#endif
