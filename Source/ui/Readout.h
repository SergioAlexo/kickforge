#pragma once
#include "core/Tuning.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace kf::ui
{
/** Converts a sub pitch-curve value into Hz. Semitone mode: relative to root+tune+fine.
    Hz mode: the curve is absolute Hz, tune/fine scale it. */
inline double curveValueToHz (double v, bool hzMode, double rootNote, double tuneSemis, double fineCents, double a4)
{
    const double shift = tuneSemis + fineCents / 100.0;
    if (hzMode) return v * std::pow (2.0, shift / 12.0);
    return tuning::midiToHz (rootNote + shift + v, a4);
}

/** "A1  55.0 Hz  +0c" */
inline juce::String formatPitch (double hz, double a4)
{
    if (! (hz > 0.0)) return "-";
    const auto n = tuning::hzToNote (hz, a4);
    const int c = (int) std::lround (n.cents);
    return juce::String (n.name) + "  " + juce::String (hz, hz < 100.0 ? 1 : 0) + " Hz  " + (c >= 0 ? "+" : "") + juce::String (c) + "c";
}

/** Snaps a pitch-curve value onto the nearest musical note (S-12). */
inline double snapPitchValue (double v, bool hzMode, double rootNote, double tuneSemis, double fineCents, double a4)
{
    const double shift = tuneSemis + fineCents / 100.0;
    if (hzMode)
    {
        const double hz = v * std::pow (2.0, shift / 12.0);
        if (hz <= 0.0) return v;
        const double snapped = tuning::midiToHz (std::round (tuning::hzToMidi (hz, a4)), a4);
        return snapped / std::pow (2.0, shift / 12.0);
    }
    const double base = rootNote + shift;
    return std::round (base + v) - base;
}
} // namespace kf::ui
