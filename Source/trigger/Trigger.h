#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "fx/FxDsp.h"

namespace kf
{
/** Registers trig_* (sidechain drum-replacement trigger, G-06..G-10) and midiout_* (G-11) parameters. */
void addTriggerParams (juce::AudioProcessorValueTreeState::ParameterLayout&);

struct TriggerSettings
{
    bool enable = false;
    float hpHz = 40.0f, lpHz = 8000.0f;   // G-07 detection input band
    float threshDb = -24.0f;              // G-08 level that fires a trigger
    float offReturnDb = 6.0f;             // G-09 note-off when the level falls this far below threshold
    float retrigReturnDb = 3.0f;          // G-10 detector re-arms when the level falls this far below threshold
    int note = 36;                        // note sent to the engine
    float velocity = 1.0f;                // fixed velocity unless dynamic
    bool dynamic = false;                 // velocity from the peak of the next ~1 ms of input
};

struct TriggerParamPtrs
{
    void bind (juce::AudioProcessorValueTreeState&);
    void read (TriggerSettings&) const noexcept;
    std::atomic<float> *enable = nullptr, *hp = nullptr, *lp = nullptr, *thresh = nullptr, *off = nullptr,
                       *retrig = nullptr, *note = nullptr, *vel = nullptr, *dyn = nullptr;
};

/** Level-triggered detector with hysteresis. Feed it the sidechain; it emits note events. */
class TriggerDetector
{
public:
    struct Event { int pos = 0; bool on = true; int note = 36; float vel = 1.0f; };
    static constexpr int kMaxEvents = 64;

    void prepare (double sampleRate);
    void reset();
    /** r may be null (mono). Returns the number of events written to out (sorted by pos). */
    int process (const TriggerSettings&, const float* l, const float* r, int n, Event* out, int maxEvents) noexcept;

private:
    double sr = 48000.0;
    fxdsp::Biquad hp, lp;
    float hpKey = -1, lpKey = -1;
    float env = 0.0f;
    bool armed = true, held = false;
    int heldNote = 36;
};
}
