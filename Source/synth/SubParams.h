#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace kf
{
/** Registers sub_* parameters. Owned by the synth module. */
void addSubParams (juce::AudioProcessorValueTreeState::ParameterLayout&);

enum class SubWave { Sine = 0, Triangle, Saw, Square, SoftSine, Count };
enum class PitchMode { Semitones = 0, Hz };
enum class GateMode { OneShot = 0, Gate };

/** Plain snapshot of the sub parameters (audio-thread friendly). */
struct SubSettings
{
    float gainDb = 0.0f;
    bool mute = false;
    SubWave wave = SubWave::Sine;
    float startPhaseDeg = 0.0f;     // phase at note-on (0..360)
    PitchMode pitchMode = PitchMode::Semitones;
    float rootNote = 33.0f;         // MIDI note the curves are written against (A1 = 55 Hz)
    float tuneSemis = 0.0f;
    float fineCents = 0.0f;
    bool keyTrack = true;
    float lengthMs = 500.0f;        // duration the normalized curves are stretched over
    GateMode gate = GateMode::OneShot;
    float releaseMs = 50.0f;
};

/** Cached raw-parameter pointers; read() fills a SubSettings without allocation. */
struct SubParamPtrs
{
    void bind (juce::AudioProcessorValueTreeState&);
    void read (SubSettings&) const noexcept;
    bool bound = false;
    std::atomic<float> *gain = nullptr, *mute = nullptr, *wave = nullptr, *phase = nullptr,
                       *pitchMode = nullptr, *root = nullptr, *tune = nullptr, *fine = nullptr,
                       *keyTrack = nullptr, *length = nullptr, *gate = nullptr, *release = nullptr;
};
}
