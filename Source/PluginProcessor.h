#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "synth/SubVoice.h"
#include "click/ClickLayer.h"
#include "fx/FxChain.h"

class KickforgeProcessor : public juce::AudioProcessor
{
public:
    KickforgeProcessor();
    ~KickforgeProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Kickforge"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    void handleMidi (const juce::MidiMessage&);
    void renderMono (juce::AudioBuffer<float>&, int start, int num);

    kf::SubVoice sub;
    kf::ClickLayer click;
    kf::FxChain fx;
    std::atomic<float>* gainParam = nullptr;
};
