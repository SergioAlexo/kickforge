#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace kf
{
/** Master FX chain (stub: passthrough). process() works in place on channel 0 of the range. */
class FxChain
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void process (juce::AudioBuffer<float>& buffer, int start, int num);
};
}
