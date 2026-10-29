#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace kf
{
/** Registers glob_* parameters (global/master). Extended by whoever needs globals. */
void addGlobParams (juce::AudioProcessorValueTreeState::ParameterLayout&);
}
