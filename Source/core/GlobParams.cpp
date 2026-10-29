#include "GlobParams.h"

namespace kf
{
void addGlobParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "glob_gain", 1 }, "Master Gain",
        juce::NormalisableRange<float> (-24.0f, 6.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
}
}
