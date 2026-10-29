#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"

KickforgeProcessor::KickforgeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", kf::createLayout())
{
    gainParam = apvts.getRawParameterValue ("glob_gain");
    click.bindParams (apvts);
    sub.bind (apvts, subCurves);
    subCurves.resetAll();
    for (auto& f : fx) f.bind (apvts);
}

void KickforgeProcessor::prepareToPlay (double sr, int block)
{
    sub.prepare (sr, block);
    click.prepare (sr, block);
    for (auto& f : fx) f.prepare (sr, block);
    maxChunk = std::max (16, block);
    mixBuf.setSize (6, maxChunk);
    monoBuf.setSize (1, maxChunk);
    gainSm.reset (sr, 0.01);
    gainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainParam != nullptr ? gainParam->load() : 0.0f));
    setLatencySamples (fx[0].getLatencySamples());
}

bool KickforgeProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void KickforgeProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        sub.noteOn (m.getNoteNumber(), m.getFloatVelocity());
        click.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    }
    else if (m.isPitchWheel())
    {
        sub.setPitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    }
    else if (m.isNoteOff())
    {
        sub.noteOff (m.getNoteNumber());
        click.noteOff();
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        sub.noteOff();
        click.noteOff();
    }
}

// Adds monoBuf[0..n) to the routing mix of `bus` (0 master, 1 A, 2 B) with a constant-power pan
// (0 dB at centre, +3 dB at the extremes).
void KickforgeProcessor::addRouted (int bus, float pan, int n)
{
    const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * (juce::MathConstants<float>::pi * 0.25f);
    const float gl = std::cos (a) * juce::MathConstants<float>::sqrt2, gr = std::sin (a) * juce::MathConstants<float>::sqrt2;
    const float* m = monoBuf.getReadPointer (0);
    float* l = mixBuf.getWritePointer (bus * 2);
    float* r = mixBuf.getWritePointer (bus * 2 + 1);
    for (int i = 0; i < n; ++i) { l[i] += m[i] * gl; r[i] += m[i] * gr; }
}

// n <= maxChunk. Layers render into a mono scratch, get panned into master / bus A / bus B,
// the buses run their FX and sum into the master, glob_gain is applied, then the master chain
// (EQ, comp, output gain, limiter = last stage) runs.
void KickforgeProcessor::renderChunk (juce::AudioBuffer<float>& buffer, int start, int n)
{
    mixBuf.clear (0, n);
    for (int src = -1; src < kf::kNumClickLayers; ++src)
    {
        monoBuf.clear (0, n);
        if (src < 0) sub.process (monoBuf, 0, n);
        else click.processLayer (src, monoBuf.getWritePointer (0), n);
        if (src < 0) addRouted (sub.getBus(), sub.getPan(), n);
        else addRouted (click.getLayerBus (src), click.getLayerPan (src), n);
    }

    const float gTarget = juce::Decibels::decibelsToGain (gainParam != nullptr ? gainParam->load() : 0.0f);
    gainSm.setTargetValue (gTarget);
    float* g = monoBuf.getWritePointer (0);
    for (int i = 0; i < n; ++i) g[i] = gainSm.getNextValue();

    for (int ch = 0; ch < 2; ++ch)
    {
        auto& f = fx[(size_t) ch];
        f.beginBlock();
        float* m = mixBuf.getWritePointer (ch);
        float* a = mixBuf.getWritePointer (2 + ch);
        float* b = mixBuf.getWritePointer (4 + ch);
        f.processBus (0, a, n);
        f.processBus (1, b, n);
        for (int i = 0; i < n; ++i) m[i] = (m[i] + a[i] + b[i]) * g[i]; // glob_gain BEFORE the limiter
        f.processMaster (m, n);
    }

    const int chans = buffer.getNumChannels();
    const float* L = mixBuf.getReadPointer (0);
    const float* R = mixBuf.getReadPointer (1);
    if (chans >= 2)
    {
        buffer.copyFrom (0, start, L, n);
        buffer.copyFrom (1, start, R, n);
    }
    else if (chans == 1)
    {
        float* o = buffer.getWritePointer (0) + start;
        for (int i = 0; i < n; ++i) o[i] = 0.5f * (L[i] + R[i]);
    }
}

void KickforgeProcessor::renderBlock (juce::AudioBuffer<float>& buf, int start, int num)
{
    while (num > 0)
    {
        const int n = std::min (num, maxChunk);
        renderChunk (buf, start, n);
        start += n;
        num -= n;
    }
}

void KickforgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int n = buffer.getNumSamples();

    // Sample-accurate: render up to each MIDI event, then apply it.
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (pos, n, meta.samplePosition);
        renderBlock (buffer, pos, at - pos);
        pos = at;
        handleMidi (meta.getMessage());
    }
    renderBlock (buffer, pos, n - pos);
    if (fx[0].getLatencySamples() != getLatencySamples()) // limiter lookahead changed
        setLatencySamples (fx[0].getLatencySamples());
    midi.clear();
}

juce::AudioProcessorEditor* KickforgeProcessor::createEditor() { return new KickforgeEditor (*this); }

void KickforgeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    st.removeChild (st.getChildWithName ("click"), nullptr);
    st.appendChild (click.saveState(), nullptr); // sample paths + click envelopes
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, dest);
}

void KickforgeProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            click.loadState (apvts.state.getChildWithName ("click"));
            subCurves.loadFromState();
        }
}
