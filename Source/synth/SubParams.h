#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <cmath>

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

    // S-04..S-06 harmonics: oscillator k runs at fundamental * 2^(semis/12); gain <= -59.9 dB = off.
    // decayMs = time to -60 dB of the extra per-harmonic decay (>= 4999 = no extra decay).
    struct Harmonic
    {
        float semis = 12.0f, gainDb = -60.0f, decayMs = 5000.0f;
    };
    static constexpr int kNumHarmonics = 9;
    std::array<Harmonic, kNumHarmonics> harm;
    SubSettings()
    {
        for (int i = 0; i < kNumHarmonics; ++i)
            harm[(size_t) i].semis = (float) (12.0 * std::log2 ((double) (i + 2)));
    }

    float drive = 0.0f;             // S-19 drive amount 0..1, multiplied by the Drive curve
    bool phaseLock = false;         // S-17
    float lockPos = 0.3f;           // S-17 normalized curve time at which the phase is locked
    float delayMs = 0.0f;           // S-22 start offset
    int bus = 0;                    // routing: 0 master, 1 bus A, 2 bus B (BusChoice)
    float pan = 0.0f;               // -1..1
};

/** Cached raw-parameter pointers; read() fills a SubSettings without allocation. */
struct SubParamPtrs
{
    void bind (juce::AudioProcessorValueTreeState&);
    void read (SubSettings&) const noexcept;
    bool bound = false;
    std::atomic<float> *gain = nullptr, *mute = nullptr, *wave = nullptr, *phase = nullptr,
                       *pitchMode = nullptr, *root = nullptr, *tune = nullptr, *fine = nullptr,
                       *keyTrack = nullptr, *length = nullptr, *gate = nullptr, *release = nullptr,
                       *drive = nullptr, *lock = nullptr, *lockPos = nullptr, *delay = nullptr,
                       *bus = nullptr, *pan = nullptr;
    std::array<std::atomic<float>*, SubSettings::kNumHarmonics> hSemi {}, hGain {}, hDecay {};
};
}
