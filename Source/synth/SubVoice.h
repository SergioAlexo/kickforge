#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace kf
{
/** Mono sub/body voice. process() ADDS into channel 0 of the buffer. velocity is 0..1. */
class SubVoice
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void noteOn (int note, float velocity);
    void noteOff();
    void process (juce::AudioBuffer<float>& buffer, int start, int num);

private:
    double sr = 44100.0;
    double phase = 0.0, time = 0.0;
    float vel = 0.0f;
    bool active = false;
};
}
