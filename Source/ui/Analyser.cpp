#include "Analyser.h"
#include <cmath>

namespace kf::ui
{
Analyser::Analyser()
    : spectrum ((size_t) numBins, floorDb), fftData ((size_t) fftSize * 2, 0.0f), scratch (4096)
{
    setSampleRate (48000.0);
}

void Analyser::setSampleRate (double sr)
{
    sampleRate = sr > 0 ? sr : 48000.0;
    maxKick = (size_t) (sampleRate * 1.5);
    kick.reserve (maxKick);
}

void Analyser::pump (AudioFifo& f)
{
    bool any = false;
    for (;;)
    {
        const int n = f.pull (scratch.data(), (int) scratch.size());
        if (n <= 0) break;
        ingest (scratch.data(), n);
        any = true;
    }
    if (any || spectrum.front() > floorDb) updateSpectrum();
}

void Analyser::feed (const float* s, int n)
{
    ingest (s, n);
    updateSpectrum();
}

void Analyser::ingest (const float* s, int n)
{
    const long holdoff = (long) (sampleRate * 0.03);
    for (int i = 0; i < n; ++i)
    {
        const float x = s[i];
        const float a = std::abs (x);
        ring[(size_t) writePos] = x;
        writePos = (writePos + 1) & (fftSize - 1);

        if (! capturing && a > 0.02f && armRun > holdoff)
        {
            capturing = true;
            kick.clear();
            ++kickVersion;
        }
        if (a < 0.02f) ++armRun; else armRun = 0;
        if (a < 0.002f) ++silentRun; else silentRun = 0;

        if (capturing)
        {
            kick.push_back (x);
            if (kick.size() >= maxKick) { capturing = false; ++kickVersion; }
            else if ((kick.size() & 1023) == 0) ++kickVersion;
        }
    }
    if (capturing && silentRun > (long) (sampleRate * 0.1))
    {
        capturing = false;
        ++kickVersion;
    }
}

void Analyser::updateSpectrum()
{
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = ring[(size_t) ((writePos + i) & (fftSize - 1))];
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const float norm = 4.0f / (float) fftSize;
    for (int i = 0; i < numBins; ++i)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[(size_t) i] * norm, floorDb);
        auto& s = spectrum[(size_t) i];
        s = std::max (db, s - 2.0f); // fast attack, ~60 dB/s fall at 30 Hz updates
    }
}
} // namespace kf::ui
