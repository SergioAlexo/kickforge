#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "ClickGen.h"
#include "ClickParams.h"
#include "core/Curve.h"

namespace kf
{
/** Click/sample layers (kNumClickLayers slots). Each slot is either a user sample or a
    synthesized click (see ClickGen). process() ADDS into channel 0. velocity is 0..1.

    Threading: noteOn/noteOff/process on the audio thread. Everything else (loading, envelope
    edits, state) on the message thread. Samples are decoded + resampled on a background
    thread and handed to the audio thread through an atomic pointer swap (no locks/allocs). */
class ClickLayer
{
public:
    enum class SampleStatus { empty, loading, loaded, missing, failed };

    ClickLayer();
    ~ClickLayer();

    /** Point parameter reads at the APVTS. Without this, the layer uses internal defaults
        (settable via setParam, used by tests). */
    void bindParams (juce::AudioProcessorValueTreeState&);
    void setParam (int layer, CP p, float value) noexcept;      // unbound only
    void setMasterGainDb (float db) noexcept { ownMaster.store (db); } // unbound only

    void prepare (double sampleRate, int maxBlockSize);
    void noteOn (int note, float velocity);
    void noteOff();
    void process (juce::AudioBuffer<float>& buffer, int start, int num);
    /** ADDS one layer (already gain/mute/solo applied, NOT panned) into out[0..num). Audio thread. */
    void processLayer (int layer, float* out, int num);
    /** Layer pan -1..1 (applied by the processor, constant-power). */
    float getLayerPan (int layer) const noexcept;

    // ---- samples (message thread) ----
    /** Loads WAV/AIFF/FLAC/OGG asynchronously. Status becomes loaded or failed. */
    void loadSample (int layer, const juce::File& file);
    /** Synchronously installs mono audio (resampled to the session rate). */
    void loadSampleFromMemory (int layer, std::vector<float> mono, double sourceRate, const juce::String& label = {});
    void clearSample (int layer);
    SampleStatus getSampleStatus (int layer) const noexcept;
    juce::String getSamplePath (int layer) const;
    int getSampleLength (int layer) const; // samples at session rate (0 if none)
    bool isLoading() const noexcept { return pendingJobs.load() > 0; }
    /** Blocks until background loads finish (tests / offline export). */
    bool waitForLoads (int timeoutMs = 10000) const;

    // ---- amp envelope, normalized time 0..1 over the layer length, value 0..1 ----
    void setEnvelope (int layer, const Curve&);
    Curve getEnvelope (int layer) const;

    /** Which FX bus a layer routes to (C-07; the FX module reads this). */
    int getLayerBus (int layer) const noexcept;

    /** Bumped on every envelope edit / sample load / clear (live-link change detection). */
    int getStateVersion() const noexcept { return stateVer.load(); }

    /** Sample paths + envelope curves. Put under the plugin state tree. */
    juce::ValueTree saveState() const;
    void loadState (const juce::ValueTree&);

    /** The decoded sample of a slot at its original rate (null if none / still loading). Lets a scratch renderer copy it. */
    std::shared_ptr<const std::vector<float>> getSourceSample (int layer, double& rate) const;

    static constexpr int envTableSize = 256;

private:
    struct Snapshot
    {
        std::shared_ptr<const std::vector<float>> sample;
        std::array<float, envTableSize> env {};
    };
    struct Svf
    {
        double ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;
        void set (double sr, double fc, double q) noexcept;
        float process (float x, int mode) noexcept;
        void reset() noexcept { ic1 = ic2 = 0; }
    };
    struct Voice
    {
        bool active = false, fresh = true;
        long delayLeft = 0, age = 0, length = 0;
        double pos = 0, rate = 1;
        float vel = 1;
        int src = 0;
        ClickGen gen;
        Svf filter;
        juce::SmoothedValue<float> gain;
        float cutoffSm = 8000.0f;
    };
    struct Slot
    {
        // message-thread state (guarded by stateLock)
        juce::String path;
        std::shared_ptr<const std::vector<float>> original; // mono, original rate
        double originalRate = 0;
        std::shared_ptr<const std::vector<float>> currentSample; // resampled to the session rate
        Curve curve;
        // audio-thread handoff
        std::atomic<Snapshot*> pending { nullptr };
        Snapshot* current = nullptr; // audio thread only
        std::array<std::atomic<Snapshot*>, 16> retired {};
        std::atomic<int> status { (int) SampleStatus::empty };
        std::atomic<int> generation { 0 };
        std::atomic<int> length { 0 };
        Voice voice;
    };

    float param (int layer, CP p) const noexcept
    {
        return ptrs[(size_t) layer][(size_t) p]->load (std::memory_order_relaxed);
    }
    void publish (int layer);
    void sweepRetired (Slot&);
    void retire (Slot&, Snapshot*) noexcept;
    void startLoadJob (int layer, juce::File file, int gen);
    void resampleAndPublish (int layer, int gen);
    static std::unique_ptr<Snapshot> makeSnapshot (std::shared_ptr<const std::vector<float>>, const Curve&);

    std::array<Slot, kNumClickLayers> slots;
    std::array<std::array<std::atomic<float>, (size_t) CP::count>, kNumClickLayers> own;
    std::array<std::array<std::atomic<float>*, (size_t) CP::count>, kNumClickLayers> ptrs {};
    std::atomic<float> ownMaster { 0.0f };
    std::atomic<float>* masterPtr = nullptr;

    mutable juce::CriticalSection stateLock;
    juce::AudioFormatManager formats;
    std::atomic<double> sampleRate { 44100.0 };
    std::atomic<int> pendingJobs { 0 };
    std::atomic<int> stateVer { 0 };
    int currentNote = 36;
    juce::ThreadPool pool { 1 };
};
}
