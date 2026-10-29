#pragma once
#include <string>

namespace kf::tuning
{
constexpr double defaultA4 = 440.0;

double midiToHz (double midiNote, double a4 = defaultA4) noexcept;
double hzToMidi (double hz, double a4 = defaultA4) noexcept; // fractional note; 0 for hz <= 0

struct NoteInfo
{
    std::string name;   // e.g. "A#1"
    int midi = 0;       // nearest MIDI note
    double cents = 0.0; // -50..+50 offset from that note
};

/** Nearest note name (sharps, C4 = MIDI 60) and cents offset. */
NoteInfo hzToNote (double hz, double a4 = defaultA4);
std::string midiToName (int midiNote);
} // namespace kf::tuning
