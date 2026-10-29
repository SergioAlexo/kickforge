#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include "PluginProcessor.h"

TEST_CASE ("Processor renders a non-silent, finite kick offline")
{
    juce::ScopedJuceInitialiser_GUI init;
    KickforgeProcessor proc;
    constexpr int block = 256;
    proc.setPlayConfigDetails (0, 2, 48000.0, block);
    proc.prepareToPlay (48000.0, block);

    float peak = 0.0f, preNotePeak = 0.0f, lateTail = 0.0f;
    for (int b = 0; b < 200; ++b)
    {
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 100);
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float s = buf.getSample (ch, i);
                REQUIRE (std::isfinite (s));
                peak = std::max (peak, std::abs (s));
                if (b == 0 && i < 100) preNotePeak = std::max (preNotePeak, std::abs (s));
                if (b > 150) lateTail = std::max (lateTail, std::abs (s));
            }
    }
    REQUIRE (peak > 0.1f);
    REQUIRE (peak < 1.5f);
    REQUIRE (preNotePeak == 0.0f); // sample-accurate: silent before the note-on offset
    REQUIRE (lateTail < 0.01f);
}
