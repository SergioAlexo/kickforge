#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"

KickforgeProcessor::KickforgeProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Sub", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Click", juce::AudioChannelSet::stereo(), false)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "STATE", kf::createLayout())
{
    gainParam = apvts.getRawParameterValue ("glob_gain");
    click.bindParams (apvts);
    sub.bind (apvts, subCurves);
    subCurves.resetAll();
    for (auto& f : fx) f.bind (apvts);
    trigPtrs.bind (apvts);
    moEnable = apvts.getRawParameterValue ("midiout_enable");
    moFollow = apvts.getRawParameterValue ("midiout_follow");
    moNote = apvts.getRawParameterValue ("midiout_note");
    moChan = apvts.getRawParameterValue ("midiout_chan");
    moLen = apvts.getRawParameterValue ("midiout_len");
}

void KickforgeProcessor::prepareToPlay (double sr, int block)
{
    sub.prepare (sr, block);
    click.prepare (sr, block);
    preview.prepare (sr);
    reference.prepare (sr);
    for (auto& f : fx) f.prepare (sr, block);
    maxChunk = std::max (16, block);
    mixBuf.setSize (6, maxChunk);
    monoBuf.setSize (1, maxChunk);
    stemBuf.setSize (4, maxChunk);
    mergedMidi.ensureSize (8192);
    outMidi.ensureSize (2048);
    trigDet.prepare (sr);
    moPending = false;
    gainSm.reset (sr, 0.01);
    gainSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (gainParam != nullptr ? gainParam->load() : 0.0f));
    setLatencySamples (fx[0].getLatencySamples());
}

bool KickforgeProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < l.outputBuses.size(); ++i) // G-13: stems are stereo or off
        if (! l.outputBuses[i].isDisabled() && l.outputBuses[i] != juce::AudioChannelSet::stereo()) return false;
    if (l.inputBuses.size() > 0) // G-06 sidechain: off, mono or stereo
    {
        const auto& sc = l.inputBuses.getReference (0);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void KickforgeProcessor::flushMidiOut (int upTo)
{
    if (moPending && moPendIn < upTo)
    {
        outMidi.addEvent (juce::MidiMessage::noteOff (moPendChan, moPendNote), std::max (0, moPendIn));
        moPending = false;
    }
}

void KickforgeProcessor::handleMidi (const juce::MidiMessage& m, int pos)
{
    if (m.isNoteOn())
    {
        sub.noteOn (m.getNoteNumber(), m.getFloatVelocity());
        click.noteOn (m.getNoteNumber(), m.getFloatVelocity());
        reference.noteOn (pos + std::max (0, getLatencySamples())); // same latency as the synth output
        if (moEnable != nullptr && moEnable->load() > 0.5f) // G-11
        {
            flushMidiOut (pos + 1);
            if (moPending) // still sounding: close it right before the new one
            {
                outMidi.addEvent (juce::MidiMessage::noteOff (moPendChan, moPendNote), pos);
                moPending = false;
            }
            moPendChan = juce::jlimit (1, 16, (int) (moChan->load() + 0.5f));
            moPendNote = moFollow->load() > 0.5f ? m.getNoteNumber() : juce::jlimit (0, 127, (int) (moNote->load() + 0.5f));
            outMidi.addEvent (juce::MidiMessage::noteOn (moPendChan, moPendNote, m.getFloatVelocity()), pos);
            moPendIn = pos + std::max (1, (int) (moLen->load() * 0.001 * getSampleRate()));
            moPending = true;
        }
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
void KickforgeProcessor::addRouted (int bus, float pan, int n, int stem)
{
    const float a = (juce::jlimit (-1.0f, 1.0f, pan) + 1.0f) * (juce::MathConstants<float>::pi * 0.25f);
    const float gl = std::cos (a) * juce::MathConstants<float>::sqrt2, gr = std::sin (a) * juce::MathConstants<float>::sqrt2;
    const float* m = monoBuf.getReadPointer (0);
    float* l = mixBuf.getWritePointer (bus * 2);
    float* r = mixBuf.getWritePointer (bus * 2 + 1);
    for (int i = 0; i < n; ++i) { l[i] += m[i] * gl; r[i] += m[i] * gr; }
    if (stem >= 0 && stemsOn[stem])
    {
        float* sl = stemBuf.getWritePointer (stem * 2);
        float* sr = stemBuf.getWritePointer (stem * 2 + 1);
        for (int i = 0; i < n; ++i) { sl[i] += m[i] * gl; sr[i] += m[i] * gr; }
    }
}

// n <= maxChunk. Layers render into a mono scratch, get panned into master / bus A / bus B,
// the buses run their FX and sum into the master, glob_gain is applied, then the master chain
// (EQ, comp, output gain, limiter = last stage) runs.
void KickforgeProcessor::renderChunk (juce::AudioBuffer<float>& buffer, int start, int n)
{
    mixBuf.clear (0, n);
    stemBuf.clear (0, n);
    for (int src = -1; src < kf::kNumClickLayers; ++src)
    {
        monoBuf.clear (0, n);
        if (src < 0) sub.process (monoBuf, 0, n);
        else click.processLayer (src, monoBuf.getWritePointer (0), n);
        if (src < 0) addRouted (sub.getBus(), sub.getPan(), n, 0);
        else addRouted (click.getLayerBus (src), click.getLayerPan (src), n, 1);
    }

    const float gTarget = juce::Decibels::decibelsToGain (gainParam != nullptr ? gainParam->load() : 0.0f);
    gainSm.setTargetValue (gTarget);
    float* g = monoBuf.getWritePointer (0);
    for (int i = 0; i < n; ++i) g[i] = gainSm.getNextValue();

    // Stereo-linked FX: Comp/FetComp slots, master compressor and limiter detect on max(|L|,|R|)
    // and apply one gain to both channels (the image does not shift under gain reduction).
    fx[0].beginBlock();
    fx[1].beginBlock();
    for (int bus = 0; bus < 2; ++bus)
        kf::FxChain::processBusPair (fx[0], fx[1], bus, mixBuf.getWritePointer (2 + bus * 2),
                                     mixBuf.getWritePointer (3 + bus * 2), n);
    for (int ch = 0; ch < 2; ++ch)
    {
        float* m = mixBuf.getWritePointer (ch);
        const float* a = mixBuf.getReadPointer (2 + ch);
        const float* b = mixBuf.getReadPointer (4 + ch);
        for (int i = 0; i < n; ++i) m[i] = (m[i] + a[i] + b[i]) * g[i]; // glob_gain BEFORE the limiter
    }
    kf::FxChain::processMasterPair (fx[0], fx[1], mixBuf.getWritePointer (0), mixBuf.getWritePointer (1), n);

    auto mainOut = getBusBuffer (buffer, false, 0);
    const int chans = mainOut.getNumChannels();
    const float* L = mixBuf.getReadPointer (0);
    const float* R = mixBuf.getReadPointer (1);
    if (chans >= 2)
    {
        mainOut.copyFrom (0, start, L, n);
        mainOut.copyFrom (1, start, R, n);
    }
    else if (chans == 1)
    {
        float* o = mainOut.getWritePointer (0) + start;
        for (int i = 0; i < n; ++i) o[i] = 0.5f * (L[i] + R[i]);
    }
    for (int k = 0; k < 2; ++k) // G-13 stems
        if (stemsOn[k])
        {
            auto bb = getBusBuffer (buffer, false, k + 1);
            if (bb.getNumChannels() >= 2)
            {
                bb.copyFrom (0, start, stemBuf.getReadPointer (k * 2), n);
                bb.copyFrom (1, start, stemBuf.getReadPointer (k * 2 + 1), n);
            }
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
    const int n = buffer.getNumSamples();
    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    // G-06..G-10: the sidechain input shares channels with the output, so detect before clearing.
    trigPtrs.read (trigSet);
    int numTrig = 0;
    if (trigSet.enable && getBusCount (true) > 0 && getBus (true, 0) != nullptr && getBus (true, 0)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 0);
        numTrig = trigDet.process (trigSet, sc.getReadPointer (0), sc.getNumChannels() > 1 ? sc.getReadPointer (1) : nullptr,
                                   n, trigEvents.data(), (int) trigEvents.size());
    }
    else
        trigDet.reset();
    buffer.clear();
    for (int k = 0; k < 2; ++k)
        stemsOn[k] = getBusCount (false) > k + 1 && getBus (false, k + 1) != nullptr && getBus (false, k + 1)->isEnabled();

    mergedMidi.clear();
    mergedMidi.addEvents (midi, 0, n, 0);
    for (int i = 0; i < numTrig; ++i)
    {
        const auto& e = trigEvents[(size_t) i];
        mergedMidi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, e.vel) : juce::MidiMessage::noteOff (1, e.note), e.pos);
    }
    outMidi.clear();

    // Sample-accurate: render up to each MIDI event, then apply it.
    int pos = 0;
    for (const auto meta : mergedMidi)
    {
        const int at = juce::jlimit (pos, n, meta.samplePosition);
        renderBlock (buffer, pos, at - pos);
        pos = at;
        handleMidi (meta.getMessage(), at);
    }
    renderBlock (buffer, pos, n - pos);
    if (fx[0].getLatencySamples() != getLatencySamples()) // limiter lookahead changed
        setLatencySamples (fx[0].getLatencySamples());

    flushMidiOut (n);          // note-offs that fall inside this block
    if (moPending) moPendIn -= n;


    scopeFifo.push (buffer.getReadPointer (0), n);
    auto mainOut = getBusBuffer (buffer, false, 0);
    {   // AI link A/B: the scope above stays synth-only; Reference mode replaces the synth, Both sums.
        const bool refAudible = reference.isAudible();
        if (refAudible && reference.getMode() == kf::RefMode::reference) mainOut.clear();
        reference.process (mainOut, refAudible);
    }
    preview.process (mainOut);
    midi.clear();
    midi.addEvents (outMidi, 0, n, 0);
}

juce::AudioProcessorEditor* KickforgeProcessor::createEditor() { return new KickforgeEditor (*this); }

void KickforgeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    st.removeChild (st.getChildWithName ("click"), nullptr);
    st.appendChild (click.saveState(), nullptr); // sample paths + click envelopes
    if (auto xml = st.createXml())
    {   // Reference + Live-link switch live beside the parameter state (never part of presets / apply)
        auto lx = reference.saveState();
        lx->setAttribute ("link", live.isEnabled());
        xml->addChildElement (lx.release());
        copyXmlToBinary (*xml, dest);
    }
}

void KickforgeProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            std::unique_ptr<juce::XmlElement> liveXml;
            if (auto* l = xml->getChildByName ("kfLive"))
            {
                liveXml = std::make_unique<juce::XmlElement> (*l);
                xml->removeChildElement (l, true);
            }
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            click.loadState (apvts.state.getChildWithName ("click"));
            subCurves.loadFromState();
            if (liveXml != nullptr)
            {
                reference.restoreState (*liveXml);
                live.setEnabled (liveXml->getBoolAttribute ("link", false));
            }
        }
}
