#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace kf
{
/** Registers sub_* parameters. Owned by the synth module. */
void addSubParams (juce::AudioProcessorValueTreeState::ParameterLayout&);
}
