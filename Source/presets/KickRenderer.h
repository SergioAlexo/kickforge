#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>
#include <vector>
#include "core/Curve.h"

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

/** Per-layer result of LayerScratch::render. All buffers are mono, start at the note-on, and have the same length. */
struct LayerBuffers
{
    double sampleRate = 44100.0;
    std::array<std::vector<float>, 5> layer; // 0 = sub (incl. harmonics / drive), 1..4 = click layers; before pan / FX
    std::vector<float> preFx;                // master-L sum of all buses before the FX chains (== sum of layers at centre pan)
    std::vector<float> mix;                  // post-FX master output (limiter latency compensated)
};

/** A persistent private processor used to render the kick of a live processor again and again (live waveform behind the
    curve editor). sync() copies parameters, sub curves and click layers from the live processor (cheap when nothing
    changed; samples are shared by pointer, not reloaded). render() plays one note and captures every layer.
    One thread at a time (the owner's worker); the live voice is never touched. */
class LayerScratch
{
public:
    LayerScratch();
    ~LayerScratch();
    void sync (KickforgeProcessor& live);
    /** Renders `seconds` of audio for `note` (-1 = sub_root). */
    LayerBuffers render (double seconds, double sampleRate = 44100.0, int note = -1);
    /** Length in ms the live state needs: max of the sub length and the click layer lengths. */
    static double neededMs (KickforgeProcessor& live);
    KickforgeProcessor& processor() noexcept { return *proc; }

private:
    std::unique_ptr<KickforgeProcessor> proc;
    std::array<std::shared_ptr<const std::vector<float>>, 4> sampleSeen;
    std::array<Curve, 4> clickEnvSeen;
    std::array<Curve, 5> subSeen;
    bool primed = false;
};

/** 24-bit WAV, replaces an existing file. Returns false on failure. */
bool writeWav24 (const juce::File&, const juce::AudioBuffer<float>&, double sampleRate);
}
