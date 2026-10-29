#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace kf
{
/** Registers fx_* parameters. Owned by the fx module. */
void addFxParams (juce::AudioProcessorValueTreeState::ParameterLayout&);

/** Slot name suffixes: fx_<name>_type/_a/_b/_c/_mix/_byp for name in { a1, a2, b1, b2 }. */
extern const char* const kFxSlotNames[4];
juce::StringArray fxTypeNames();
}
