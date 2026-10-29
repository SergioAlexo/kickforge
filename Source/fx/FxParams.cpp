#include "FxParams.h"

namespace kf
{
void addFxParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    // Placeholder - replace with the real fx parameter set.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "fx_drive", 1 }, "Drive",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
}
}
