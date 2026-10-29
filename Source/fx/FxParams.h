#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace kf
{
/** Registers fx_* parameters. Owned by the fx module. */
void addFxParams (juce::AudioProcessorValueTreeState::ParameterLayout&);
}
