#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"

KickforgeProcessor::KickforgeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", kf::createLayout())
{
    gainParam = apvts.getRawParameterValue ("glob_gain");
}

void KickforgeProcessor::prepareToPlay (double sr, int block)
{
    sub.prepare (sr, block);
    click.prepare (sr, block);
    fx.prepare (sr, block);
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
    else if (m.isNoteOff() || m.isAllNotesOff() || m.isAllSoundOff())
    {
        sub.noteOff();
        click.noteOff();
    }
}

// The engine is mono: it renders into channel 0 and the result is copied to the other channels.
void KickforgeProcessor::renderMono (juce::AudioBuffer<float>& buf, int start, int num)
{
    if (num <= 0) return;
    sub.process (buf, start, num);
    click.process (buf, start, num);
    fx.process (buf, start, num);
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
        renderMono (buffer, pos, at - pos);
        pos = at;
        handleMidi (meta.getMessage());
    }
    renderMono (buffer, pos, n - pos);

    const float g = juce::Decibels::decibelsToGain (gainParam != nullptr ? gainParam->load() : 0.0f);
    buffer.applyGain (0, 0, n, g);
    for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, n);
    midi.clear();
}

juce::AudioProcessorEditor* KickforgeProcessor::createEditor() { return new KickforgeEditor (*this); }

void KickforgeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void KickforgeProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}
