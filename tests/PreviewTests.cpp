#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "PluginProcessor.h"

using Catch::Matchers::WithinAbs;

namespace
{
std::vector<float> dcClip (int n, float v = 0.5f) { return std::vector<float> ((size_t) n, v); }

float peakOf (const juce::AudioBuffer<float>& b)
{
    float p = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c) p = std::max (p, b.getMagnitude (c, 0, b.getNumSamples()));
    return p;
}
}

TEST_CASE ("PreviewPlayer plays a clip into every channel and finishes", "[preview]")
{
    kf::PreviewPlayer p;
    p.prepare (48000.0);
    p.play (dcClip (4800), 48000.0); // 100 ms
    REQUIRE (p.isPlaying());

    juce::AudioBuffer<float> b (2, 512);
    b.clear();
    p.process (b, 1.0f);
    // past the 4 ms fade-in the clip plays at full level on both channels
    REQUIRE_THAT (b.getSample (0, 400), WithinAbs (0.5, 1e-4));
    REQUIRE_THAT (b.getSample (1, 400), WithinAbs (0.5, 1e-4));
    REQUIRE (b.getSample (0, 0) < 0.01f); // fade-in, no click

    for (int i = 0; i < 20; ++i) { b.clear(); p.process (b, 1.0f); }
    REQUIRE_FALSE (p.isPlaying());
    b.clear();
    p.process (b, 1.0f);
    REQUIRE (peakOf (b) == 0.0f);
}

TEST_CASE ("PreviewPlayer resamples to the host rate", "[preview]")
{
    kf::PreviewPlayer p;
    p.prepare (96000.0);
    p.play (dcClip (4410), 44100.0); // 100 ms at 44.1k -> ~9600 host samples
    int played = 0;
    juce::AudioBuffer<float> b (1, 256);
    for (int blk = 0; blk < 100 && (blk == 0 || p.isPlaying()); ++blk)
    {
        b.clear();
        p.process (b, 1.0f);
        for (int i = 0; i < b.getNumSamples(); ++i) played += b.getSample (0, i) != 0.0f ? 1 : 0;
    }
    REQUIRE (std::abs (played - 9600) < 30);
}

TEST_CASE ("PreviewPlayer stop fades out and replacing a clip restarts it", "[preview]")
{
    kf::PreviewPlayer p;
    p.prepare (48000.0);
    p.play (dcClip (48000), 48000.0);
    juce::AudioBuffer<float> b (1, 1024);
    b.clear();
    p.process (b, 1.0f);
    p.stop();
    b.clear();
    p.process (b, 1.0f);
    REQUIRE (b.getSample (0, 0) > 0.4f);   // starts from where it was ...
    REQUIRE (b.getSample (0, 300) == 0.0f); // ... and is silent after the 4 ms fade-out
    REQUIRE_FALSE (p.isPlaying());

    p.play (dcClip (48000, 0.25f), 48000.0);
    p.play (dcClip (48000, 0.75f), 48000.0); // replaced before the audio thread saw it
    b.clear();
    p.process (b, 1.0f);
    REQUIRE_THAT (b.getSample (0, 1000), WithinAbs (0.75, 1e-4));
    p.play (dcClip (48000, 0.25f), 48000.0); // replaced while playing
    b.clear();
    p.process (b, 1.0f);
    REQUIRE_THAT (b.getSample (0, 1000), WithinAbs (0.25, 1e-4));
}

TEST_CASE ("Processor mixes the preview into the main output", "[preview]")
{
    KickforgeProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> b (proc.getTotalNumOutputChannels(), 512);
    juce::MidiBuffer midi;
    b.clear();
    proc.processBlock (b, midi);
    REQUIRE (peakOf (b) == 0.0f); // no note, no preview: silence

    proc.preview.play (dcClip (48000, 0.5f), 48000.0);
    b.clear();
    proc.processBlock (b, midi);
    REQUIRE (b.getMagnitude (0, 256, 256) > 0.3f);
    REQUIRE (b.getMagnitude (1, 256, 256) > 0.3f);
}
