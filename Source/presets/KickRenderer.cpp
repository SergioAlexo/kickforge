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

LayerScratch::LayerScratch() : proc (std::make_unique<KickforgeProcessor>())
{
    proc->setPlayConfigDetails (0, 2, 44100.0, 512);
}

LayerScratch::~LayerScratch() = default;

double LayerScratch::neededMs (KickforgeProcessor& live)
{
    double ms = live.apvts.getRawParameterValue ("sub_length")->load();
    for (int l = 0; l < kNumClickLayers; ++l)
        if (live.apvts.getRawParameterValue (clickParamId (l, CP::source))->load() > 0.5f) // source not Off
            ms = std::max (ms, (double) live.apvts.getRawParameterValue (clickParamId (l, CP::length))->load());
    return ms;
}

void LayerScratch::sync (KickforgeProcessor& live)
{
    auto& lp = live.getParameters();
    auto& sp = proc->getParameters();
    for (int i = 0; i < lp.size() && i < sp.size(); ++i)
        if (sp[i]->getValue() != lp[i]->getValue())
            sp[i]->setValueNotifyingHost (lp[i]->getValue()); // notifies the scratch APVTS adapters (raw values)
    for (int c = 0; c < 5; ++c)
    {
        auto cur = live.getSubCurves().getCurve ((SubCurveId) c);
        if (! primed || ! (cur == subSeen[(size_t) c]))
        {
            proc->getSubCurves().setCurve ((SubCurveId) c, cur);
            subSeen[(size_t) c] = cur;
        }
    }
    for (int l = 0; l < kNumClickLayers; ++l)
    {
        double rate = 0.0;
        auto smp = live.getClickLayer().getSourceSample (l, rate);
        if (smp != sampleSeen[(size_t) l])
        {
            if (smp != nullptr && rate > 0.0) proc->getClickLayer().loadSampleFromMemory (l, *smp, rate, "scratch");
            else proc->getClickLayer().clearSample (l);
            sampleSeen[(size_t) l] = smp;
        }
        auto env = live.getClickLayer().getEnvelope (l);
        if (! primed || ! (env == clickEnvSeen[(size_t) l]))
        {
            proc->getClickLayer().setEnvelope (l, env);
            clickEnvSeen[(size_t) l] = env;
        }
    }
    primed = true;
}

LayerBuffers LayerScratch::render (double seconds, double sr, int note)
{
    constexpr int block = 512;
    proc->prepareToPlay (sr, block); // also clears voices and FX tails
    if (note < 0)
        note = juce::jlimit (0, 127, juce::roundToInt (proc->apvts.getRawParameterValue ("sub_root")->load()));
    const int latency = proc->getLatencySamples();
    const int len = std::max (block, (int) std::ceil (seconds * sr));
    const int total = ((len + latency + block - 1) / block) * block;

    LayerTap tap;
    for (auto& l : tap.layer) l.assign ((size_t) total, 0.0f);
    tap.preFx.assign ((size_t) total, 0.0f);
    proc->setLayerTap (&tap);

    LayerBuffers r;
    r.sampleRate = sr;
    std::vector<float> out;
    out.reserve ((size_t) total);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += block)
    {
        midi.clear();
        if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
        proc->processBlock (buf, midi);
        const float* d = buf.getReadPointer (0);
        for (int i = 0; i < block; ++i) out.push_back (std::isfinite (d[i]) ? d[i] : 0.0f);
    }
    proc->setLayerTap (nullptr);

    for (size_t k = 0; k < r.layer.size(); ++k)
        r.layer[k].assign (tap.layer[k].begin(), tap.layer[k].begin() + len);
    r.preFx.assign (tap.preFx.begin(), tap.preFx.begin() + len);
    r.mix.assign (out.begin() + std::min (latency, total - len), out.begin() + std::min (latency, total - len) + len);
    return r;
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
