#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace kf
{
/** Registers click_* parameters. Owned by the click module. */
void addClickParams (juce::AudioProcessorValueTreeState::ParameterLayout&);
}
