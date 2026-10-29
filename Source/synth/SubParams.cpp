#include "SubParams.h"

namespace kf
{
void addSubParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    // Placeholder - replace with the real synth parameter set.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "sub_level", 1 }, "Sub Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f));
}
}
