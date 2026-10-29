#include "ClickParams.h"

namespace kf
{
void addClickParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    // Placeholder - replace with the real click parameter set.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "click_level", 1 }, "Click Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f));
}
}
