#pragma once
#include "Analyser.h"
#include "KfLookAndFeel.h"

namespace kf::ui
{
/** U-09: waveform of the last rendered kick. */
class WaveformView : public juce::Component
{
public:
    explicit WaveformView (Analyser& a) : analyser (a) {}
    void paint (juce::Graphics&) override;
private:
    Analyser& analyser;
};

/** U-10 / F-21: live spectrum analyser, optionally with the master-EQ response overlaid. */
class SpectrumView : public juce::Component
{
public:
    SpectrumView (Analyser& a, juce::AudioProcessorValueTreeState* eqState = nullptr)
        : analyser (a), eq (eqState) {}
    void paint (juce::Graphics&) override;

    static float freqToX (double hz, float x, float w) noexcept
    {
        return x + w * (float) (std::log (hz / 20.0) / std::log (1000.0));
    }
    static double xToFreq (float px, float x, float w) noexcept
    {
        return 20.0 * std::pow (1000.0, (px - x) / w);
    }
private:
    Analyser& analyser;
    juce::AudioProcessorValueTreeState* eq;
};
} // namespace kf::ui
