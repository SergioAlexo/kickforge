#pragma once
#include <array>
#include <string>
#include <vector>

/** Pure, headless kick measurements + reference-vs-synth comparison (used by kickforge-cli).
    No JUCE types, no message-thread requirements. */
namespace kf::analysis
{
constexpr int kNumTimes = 15;
constexpr int kNumBands = 10;
/** Fixed sample times (ms after the aligned onset). */
extern const std::array<double, kNumTimes> kTimesMs;
/** Octave band centres (Hz); band k spans centre/sqrt2 .. centre*sqrt2. */
extern const std::array<double, kNumBands> kBandCentersHz;

struct KickFeatures
{
    bool ok = false;
    double sr = 0.0;
    int start = 0;                 // onset sample in the analysed buffer
    double lengthMs = 0.0;         // until the envelope falls 50 dB below its peak
    double peakDb = -120.0;        // sample peak, dBFS
    double rmsDb = -120.0;         // RMS over the first 400 ms
    double envPeakDb = -120.0;     // loudest 10 ms RMS window
    double tailHz = 0.0;           // fundamental of the tail (0 = none found)
    double transientPct = 0.0;     // share of the first-400-ms energy that lies in the first 10 ms
    std::array<double, kNumTimes> pitchHz {};     // <= 0 : not measurable
    std::array<double, kNumTimes> envDb {};       // windowed RMS, dBFS
    std::array<double, kNumBands> bandEarlyDb {}; // 0..50 ms
    std::array<double, kNumBands> bandLateDb {};  // 50..350 ms
};

/** First sample whose magnitude exceeds 3 % of the peak (0 if silent). */
int findOnset (const float* x, int n);

/** Fine alignment: lag (samples, +-maxMs) to ADD to synStart so that the first winMs of syn best matches ref. */
int alignLag (const float* ref, int nref, int refStart, const float* syn, int nsyn, int synStart, double sr,
              double maxMs = 3.0, double winMs = 30.0);

/** Features of x[start..n). */
KickFeatures analyseKick (const float* x, int n, double sr, int start);

/** Dense measurement helper (also used for plots). */
class Tracker
{
public:
    Tracker (const float* x, int n, double sr, int start);
    double hzAt (double tMs, double* confidence = nullptr) const; // <= 0 if unreliable
    double rmsDbAt (double tMs) const;                            // windowed RMS, dBFS
    double durationMs() const { return (double) raw.size() * 1000.0 / sr; }
    double sampleRate() const { return sr; }
private:
    double sr;
    std::vector<float> raw;
    std::vector<float> lp;   // low-passed + decimated
    int decim = 1;
    double lpPeakRms = 0.0;
};

/** Magnitude spectrum of x[start .. start+lenMs) on a log frequency axis, dB (relative scale, comparable between signals). */
std::vector<double> logSpectrumDb (const float* x, int n, double sr, int start, double lenMs, int points,
                                   double fMin, double fMax);

/** "A1 +12ct" style name (A4 = 440, MIDI 33 = A1). */
std::string noteName (double hz);

struct HintContext
{
    double subLengthMs = 500.0; // maps ms -> normalized curve time in hints
};

struct Comparison
{
    double score = 0.0;  // 0..100, weighted mean of the part scores
    double loss = 0.0;   // smooth non-negative error (0 = identical), used by `fit`
    double pitchScore = 0, envScore = 0, spectrumScore = 0, tailScore = 0, lengthScore = 0, transientScore = 0;
    std::array<double, kNumTimes> pitchErrSt {}; // synth - ref, NaN where not comparable
    std::array<double, kNumTimes> envErrDb {};   // synth - ref
    std::array<double, kNumBands> bandErrEarlyDb {}, bandErrLateDb {}; // NaN where both are silent
    double tailCents = 0.0;      // synth - ref (NaN if unknown)
    double lengthRatio = 1.0;    // synth / ref
    double transientDiffPct = 0.0;
    std::vector<std::string> hints;
};

/** Weights: pitch 0.30, envelope 0.25, spectrum 0.20, tail 0.10, transient 0.10, length 0.05.
    Each part score = 100 * exp(-meanError / scale); see `kickforge-cli help compare`. */
Comparison compareKicks (const KickFeatures& ref, const KickFeatures& syn, const HintContext& = {});
} // namespace kf::analysis
