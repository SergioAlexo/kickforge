#include "KickRenderer.h"
#include "PluginProcessor.h"
#include <cmath>

namespace kf
{
juce::AudioBuffer<float> renderKickFromState (const juce::MemoryBlock& state, const RenderOptions& o)
{
    constexpr int block = 512;
    KickforgeProcessor proc;
    proc.setPlayConfigDetails (0, 2, o.sampleRate, block);
    proc.prepareToPlay (o.sampleRate, block);
    if (state.getSize() > 0)
    {
        proc.setStateInformation (state.getData(), (int) state.getSize());
        proc.getClickLayer().waitForLoads (15000);
    }

    int note = o.note;
    if (note < 0)
    {
        auto* r = proc.apvts.getRawParameterValue ("sub_root");
        note = r != nullptr ? juce::jlimit (0, 127, juce::roundToInt (r->load())) : 36;
    }
    const int latency = proc.getLatencySamples();
    const long maxN = (long) (o.maxSeconds * o.sampleRate) + latency;
    const long minN = (long) (o.minSeconds * o.sampleRate) + latency;
    const long offAt = (long) (o.noteOffSeconds * o.sampleRate);
    const int quietBlocksNeeded = std::max (2, (int) (0.1 * o.sampleRate / block));

    std::vector<float> out;
    out.reserve ((size_t) maxN);
    juce::AudioBuffer<float> buf (2, block);
    int quiet = 0;
    for (long pos = 0; pos < maxN; pos += block)
    {
        juce::MidiBuffer midi;
        if (pos == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, o.velocity), 0);
        if (offAt >= pos && offAt < pos + block)
            midi.addEvent (juce::MidiMessage::noteOff (1, note), (int) (offAt - pos));
        proc.processBlock (buf, midi);
        float pk = 0.0f;
        const float* d = buf.getReadPointer (0);
        for (int i = 0; i < block; ++i)
        {
            const float s = std::isfinite (d[i]) ? d[i] : 0.0f;
            pk = std::max (pk, std::abs (s));
            out.push_back (s);
        }
        quiet = pk < 1.0e-4f ? quiet + 1 : 0;
        if (pos > minN && quiet >= quietBlocksNeeded && pos > offAt) break;
    }

    // drop limiter latency, trim trailing silence
    const size_t skip = std::min ((size_t) latency, out.size());
    size_t end = out.size();
    while (end > skip + 1 && std::abs (out[end - 1]) < 1.0e-4f) --end;
    end = std::min (out.size(), end + (size_t) (0.005 * o.sampleRate));
    const size_t minLen = (size_t) (o.minSeconds * o.sampleRate);
    end = std::max (end, std::min (out.size(), skip + minLen));
    const int len = (int) (end - skip);

    juce::AudioBuffer<float> result (1, std::max (1, len));
    result.clear();
    if (len > 0)
        result.copyFrom (0, 0, out.data() + skip, len);
    return result;
}

juce::AudioBuffer<float> renderKick (KickforgeProcessor& live, const RenderOptions& o)
{
    juce::MemoryBlock state;
    live.getStateInformation (state);
    return renderKickFromState (state, o);
}

bool writeWav24 (const juce::File& file, const juce::AudioBuffer<float>& b, double sampleRate)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    auto stream = file.createOutputStream();
    if (stream == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (
        wav.createWriterFor (stream.get(), sampleRate, (unsigned) b.getNumChannels(), 24, {}, 0));
    if (w == nullptr) return false;
    stream.release(); // owned by the writer now
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}
}
