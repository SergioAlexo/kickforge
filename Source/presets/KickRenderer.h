#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

class KickforgeProcessor;

namespace kf
{
struct RenderOptions
{
    int note = -1; // -1 = the preset's sub root note (so key-tracked pitch is the base pitch)
    float velocity = 1.0f;
    double sampleRate = 48000.0;
    double maxSeconds = 8.0;
    double minSeconds = 0.05;
    double noteOffSeconds = 0.25; // only matters for Gate mode
};

/** Offline render of one kick (P-05). Uses a private second processor instance seeded with the
    given state, so the live voice is never disturbed. Result is mono; limiter latency is
    compensated and trailing silence trimmed. Message thread only. */
juce::AudioBuffer<float> renderKickFromState (const juce::MemoryBlock& state, const RenderOptions& = {});
juce::AudioBuffer<float> renderKick (KickforgeProcessor& live, const RenderOptions& = {});

/** 24-bit WAV, replaces an existing file. Returns false on failure. */
bool writeWav24 (const juce::File&, const juce::AudioBuffer<float>&, double sampleRate);
}
