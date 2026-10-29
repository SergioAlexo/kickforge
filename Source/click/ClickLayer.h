#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace kf
{
/** Click/transient layer (stub: silence). process() ADDS into channel 0. velocity is 0..1. */
class ClickLayer
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void noteOn (int note, float velocity);
    void noteOff();
    void process (juce::AudioBuffer<float>& buffer, int start, int num);
};
}
