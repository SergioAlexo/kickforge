#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

/** Placeholder editor: generic parameter list. Replaced by the UI agent (Source/ui). */
class KickforgeEditor : public juce::AudioProcessorEditor
{
public:
    explicit KickforgeEditor (KickforgeProcessor&);
    void resized() override;

private:
    juce::GenericAudioProcessorEditor generic;
};
