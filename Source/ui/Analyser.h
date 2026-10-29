#pragma once
#include "AudioFifo.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

namespace kf::ui
{
/** Message-thread consumer of the audio FIFO: keeps a rolling window for the spectrum
    analyser and captures the most recent kick (onset after silence) for the waveform view. */
class Analyser
{
public:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int numBins = fftSize / 2;
    static constexpr float floorDb = -100.0f;

    Analyser();

    void setSampleRate (double sr);
    double getSampleRate() const noexcept { return sampleRate; }

    /** Drains the FIFO and refreshes the spectrum (message thread). */
    void pump (AudioFifo&);
    /** Same, for samples you already have (tests / offline). */
    void feed (const float* samples, int n);

    /** Last captured kick (mono). Grows while a capture is in progress. */
    const std::vector<float>& getKick() const noexcept { return kick; }
    int getKickVersion() const noexcept { return kickVersion; }
    bool isCapturing() const noexcept { return capturing; }

    /** Smoothed magnitude spectrum in dBFS (0 dB = full-scale sine), numBins entries. */
    const std::vector<float>& getSpectrumDb() const noexcept { return spectrum; }
    double binToHz (double bin) const noexcept { return bin * sampleRate / fftSize; }

private:
    void ingest (const float* s, int n);
    void updateSpectrum();

    double sampleRate = 48000.0;
    std::array<float, fftSize> ring {};
    int writePos = 0;
    std::vector<float> spectrum, fftData, scratch, kick;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };

    long silentRun = 1 << 30, armRun = 1 << 30;
    bool capturing = false;
    int kickVersion = 0;
    size_t maxKick = 72000;
};
} // namespace kf::ui
