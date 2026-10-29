#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace kf
{
/** Registers glob_* parameters (global/master). Extended by whoever needs globals. */
void addGlobParams (juce::AudioProcessorValueTreeState::ParameterLayout&);

/** Plain snapshot of the global MIDI/voice parameters. */
struct GlobSettings
{
    float a4 = 440.0f;          // tuning reference
    float bendRange = 2.0f;     // semitones for full pitch-wheel deflection
    float glideMs = 0.0f;       // portamento time between notes
    bool legato = false;        // note-on while sounding glides instead of retriggering
    float velSens = 0.8f;       // 0 = fixed level, 1 = level follows velocity fully
};

struct GlobParamPtrs
{
    void bind (juce::AudioProcessorValueTreeState&);
    void read (GlobSettings&) const noexcept;
    bool bound = false;
    std::atomic<float> *a4 = nullptr, *bend = nullptr, *glide = nullptr, *legato = nullptr, *vel = nullptr;
};
}
