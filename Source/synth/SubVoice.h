#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "SubCurves.h"
#include "SubParams.h"
#include "core/GlobParams.h"
#include <array>
#include <optional>
#include <vector>

namespace kf
{
/** Mono sub/body voice. process() ADDS into channel 0 of the buffer. velocity is 0..1.

    - Phase-accurate oscillator (phase integrated from instantaneous frequency, start phase on note-on).
    - Pitch + amp envelopes come from immutable Curve snapshots (CurveSlot), acquired at note-on;
      curve time is normalized over sub_length.
    - Mono: a new note-on retriggers with a short crossfade (declick) or, in legato mode, glides.
    - Settings are read from bound APVTS parameters at the start of each block / note, or can be
      injected with setSettings() (tests). */
class SubVoice
{
public:
    SubVoice() = default;
    ~SubVoice();
    SubVoice (const SubVoice&) = delete;
    SubVoice& operator= (const SubVoice&) = delete;

    void prepare (double sampleRate, int maxBlockSize);

    /** Message thread, before audio starts: reads parameters from the tree and curves from SubCurves. */
    void bind (juce::AudioProcessorValueTreeState&, SubCurves&);
    /** Alternative to bind(): inject curve slots / settings directly (tests). */
    void setCurveSlots (CurveSlot* pitch, CurveSlot* amp, CurveSlot* drive = nullptr,
                        CurveSlot* harm = nullptr, CurveSlot* keyTrack = nullptr) noexcept
    {
        slotPtr = { pitch, amp, drive, harm, keyTrack }; // null drive/harm/keytrack curves count as 1.0
    }
    void setSettings (const SubSettings& s, const GlobSettings& g) noexcept { sub = s; glob = g; }

    void noteOn (int note, float velocity);
    void noteOff();           // all notes off / release
    void noteOff (int note);  // releases only if this is the sounding note (Gate mode)
    void setPitchBend (float bend01); // -1..1

    void process (juce::AudioBuffer<float>& buffer, int start, int num);

    bool isActive() const noexcept { return cur.active || old.active; }
    /** Instantaneous fundamental (Hz) of the sounding layer at the last rendered sample, 0 if idle. */
    double getLastFrequency() const noexcept { return lastFreq; }
    /** Routing of the sub layer (0 master, 1 bus A, 2 bus B) and pan -1..1, as of the last block. */
    int getBus() const noexcept;
    float getPan() const noexcept;

private:
    static constexpr int kNumCurves = (int) SubCurveId::Count;
    static constexpr int kNumOsc = 1 + SubSettings::kNumHarmonics;
    struct Layer
    {
        bool active = false, releasing = false;
        int note = 0;
        double phase = 0.0, time = 0.0, length = 0.5;
        float velGain = 1.0f;
        float fade = 1.0f, fadeStep = 0.0f; // fade in (+) / out (-)
        float rel = 1.0f, relCoef = 1.0f;
        std::array<const CurveSlot::Snapshot*, kNumCurves> snap {};
        std::array<std::optional<Curve::Walker>, kNumCurves> walker;
        // S-04: oscillator 0 = fundamental, 1..9 = harmonics
        std::array<double, kNumOsc> ph {}, corr {};   // phases; pending phase-lock corrections (per sample)
        std::array<float, SubSettings::kNumHarmonics> hGain {}, hDecayCoef {}, hDec {};
        double delayLeft = 0.0;                        // S-22
        int corrLeft = 0;
        bool lockDone = false;
    };

    void refresh() noexcept;
    void stopLayer (Layer&) noexcept;
    void startLayer (Layer&, int note, float vel) noexcept;
    void renderLayer (Layer&, float* out, int n) noexcept;
    float waveform (double p, double dp) const noexcept;

    double sr = 44100.0;
    int maxBlock = 512;
    SubParamPtrs subPtrs;
    GlobParamPtrs globPtrs;
    SubSettings sub;
    GlobSettings glob;
    std::array<CurveSlot*, kNumCurves> slotPtr {};

    Layer cur, old;
    std::vector<double> stScratch, ntScratch; // user pitch offset / note-delta (semitones) per sample
    std::vector<float> tmp;

    // shared per-sample state
    double noteNow = 33.0, noteTarget = 33.0, noteStep = 0.0; // MIDI note incl. glide
    bool haveNote = false;
    double userSt = 0.0, userStTarget = 0.0;  // tune + fine + bend, smoothed (semitones)
    float bend = 0.0f;
    float gainSm = 1.0f;
    double smoothCoef = 0.0, declickSamples = 64.0;
    bool smoothInit = false;
    double lastFreq = 0.0;
};
}
