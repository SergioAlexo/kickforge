#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "synth/SubVoice.h"
#include "click/ClickLayer.h"
#include "fx/FxChain.h"
#include "trigger/Trigger.h"
#include <array>
#include "ui/AudioFifo.h"
#include "core/PreviewPlayer.h"
#include "live/Reference.h"
#include "live/LiveLink.h"

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
    bool producesMidi() const override { return true; }
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

    /** Sub-layer pitch/amp/... curves (thread-safe swap, mirrored into apvts.state "CURVES"). */
    kf::SubCurves& getSubCurves() noexcept { return subCurves; }
    /** Click/sample layers (presets/export: sample load + wait). */
    kf::ClickLayer& getClickLayer() noexcept { return click; }

    // ---- UI hooks (Source/ui) ----
    /** Final mono output, pushed from processBlock for the scope / analyser. */
    kf::AudioFifo scopeFifo;
    /** UI-injected notes (Audition button); merged into the MIDI stream at the start of each block. */
    juce::MidiKeyboardState keyboardState;
    /** Import-dialog audition: mixed into the main output after the FX chain. */
    kf::PreviewPlayer preview;
    /** AI link: the loaded reference kick (A/B against the synth) and the live-folder link. */
    kf::Reference& getReference() noexcept { return reference; }
    kf::LiveLink& getLiveLink() noexcept { return live; }

private:
    void handleMidi (const juce::MidiMessage&, int pos);
    void flushMidiOut (int upTo);
    void renderBlock (juce::AudioBuffer<float>&, int start, int num);
    void renderChunk (juce::AudioBuffer<float>&, int start, int n);
    void addRouted (int bus, float pan, int n, int stem);

    kf::SubCurves subCurves { apvts };
    kf::SubVoice sub;
    kf::ClickLayer click;
    std::array<kf::FxChain, 2> fx; // one chain per channel (the FX state is mono); bus A/B/master
    std::atomic<float>* gainParam = nullptr;

    // Per-layer routing scratch (preallocated in prepareToPlay): channels 0/1 master L/R, 2/3 bus A, 4/5 bus B.
    juce::AudioBuffer<float> mixBuf, monoBuf;
    int maxChunk = 0;
    juce::SmoothedValue<float> gainSm;

    // G-13 stems (pre-FX, panned): channels 0/1 sub, 2/3 click layers; written only when the aux outputs are enabled.
    juce::AudioBuffer<float> stemBuf;
    bool stemsOn[2] = { false, false };

    // G-06..G-10 sidechain trigger
    kf::TriggerParamPtrs trigPtrs;
    kf::TriggerSettings trigSet;
    kf::TriggerDetector trigDet;
    std::array<kf::TriggerDetector::Event, kf::TriggerDetector::kMaxEvents> trigEvents;
    juce::MidiBuffer mergedMidi, outMidi;

    // G-11 MIDI out
    std::atomic<float> *moEnable = nullptr, *moFollow = nullptr, *moNote = nullptr, *moChan = nullptr, *moLen = nullptr;
    bool moPending = false;
    int moPendNote = 0, moPendChan = 1, moPendIn = 0; // moPendIn: samples from the start of the current block

    kf::Reference reference;
    kf::LiveLink live { *this, reference }; // last: it listens to apvts and uses the members above
};
