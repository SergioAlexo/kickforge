#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include "core/Curve.h"
#include "synth/SubParams.h"

namespace kf::analysis
{
/** A single drum hit inside a (possibly longer) recording. Sample indices. */
struct Hit
{
    int start = 0, end = 0;
    float strength = 0.0f; // peak of the low-band (<200 Hz) RMS envelope, linear
};

/** Time-ordered kick-hit candidates (low-band onset detection). A single-shot sample yields one hit
    covering the whole file. Empty if the input is silent. */
std::vector<Hit> detectHits (const float* x, int n, double sampleRate);
/** Index of the strongest hit (earliest wins ties within 3 %), -1 if empty. */
int strongestHit (const std::vector<Hit>&);

struct Options
{
    bool harmonics = true;   // also estimate sub_hN_* (harmonics 2..10)
    double a4 = 440.0;       // glob_a4 the sub will be played against
    double minHz = 25.0, maxHz = 400.0;
};

/** Everything the importer needs to recreate a hit with the sub generator + a top-end sample. */
struct Result
{
    bool ok = false;
    juce::String error;
    double sampleRate = 0.0;
    int regionStart = 0, regionEnd = 0;   // analysed region of the input (samples)

    // sub layer
    Curve pitch;        // semitones relative to root+tune, normalized time 0..1 over sub.lengthMs
    Curve amp;          // linear 0..1, normalized time
    Curve harmAmount;   // flat 1 (the harmonic gains/decays carry the shape)
    SubSettings sub;    // gain, root, tune, length, start phase, harmonics, pitch mode = semitones
    double startHz = 0.0, tailHz = 0.0;
    double peakAmp = 0.0;                 // linear peak of the extracted fundamental
    int numHarmonicsUsed = 0;

    // top end: original minus the extracted sub, high-passed, trimmed. Same rate as the input.
    std::vector<float> residual;
    double residualHpHz = 0.0;

    // fit quality (diagnostics)
    double pitchFitRmsSemis = 0.0;   // fitted curve vs tracked pitch, frames with amp > 10 % of peak
    double ampFitCorrelation = 0.0;  // fitted amp curve vs extracted fundamental envelope
};

/** Analyse one hit. [regionStart, regionEnd) selects it; regionStart < 0 = strongest detected hit.
    Pure function (no JUCE message-thread requirements), safe on a background thread. */
Result analyse (const float* x, int n, double sampleRate, int regionStart = -1, int regionEnd = -1,
                const Options& = {});

/** Renders the sub part of a Result with the real SubVoice (offline, mono). Length = numSamples. */
std::vector<float> renderSub (const Result&, double sampleRate, int numSamples, double a4 = 440.0);

/** Generic breakpoint simplification with per-segment tension (used for both curves; exposed for tests).
    t strictly increasing. relFloor > 0: error is |d| / (relFloor + |y|), else absolute. */
Curve fitBreakpoints (const std::vector<double>& t, const std::vector<double>& y, double tol, int maxPoints,
                      double relFloor);
} // namespace kf::analysis
