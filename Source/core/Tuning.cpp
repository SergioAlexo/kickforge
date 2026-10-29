#include "Tuning.h"
#include <cmath>

namespace kf::tuning
{
double midiToHz (double n, double a4) noexcept { return a4 * std::pow (2.0, (n - 69.0) / 12.0); }

double hzToMidi (double hz, double a4) noexcept
{
    return hz > 0.0 ? 69.0 + 12.0 * std::log2 (hz / a4) : 0.0;
}

std::string midiToName (int n)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int pc = ((n % 12) + 12) % 12;
    const int oct = (int) std::floor (n / 12.0) - 1;
    return std::string (names[pc]) + std::to_string (oct);
}

NoteInfo hzToNote (double hz, double a4)
{
    NoteInfo info;
    if (hz <= 0.0) { info.name = "-"; return info; }
    const double m = hzToMidi (hz, a4);
    info.midi = (int) std::lround (m);
    info.cents = (m - info.midi) * 100.0;
    info.name = midiToName (info.midi);
    return info;
}
} // namespace kf::tuning
