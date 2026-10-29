#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <memory>
#include <vector>

namespace kf
{
/** A/B mode of the reference: what reaches the main output. */
enum class RefMode { synth = 0, reference = 1, both = 2 };

/** The "Reference" kick the user loads next to the synth (AI link): a mono region of an audio file that is
    retriggered on every note-on (pitch NOT shifted) and mixed per A/B mode.
    Message thread: load / clear / setRegion / setNote / setGainDb / setMode / save+restore state.
    Audio thread: prepare / noteOn / process. The clip is handed over by atomic pointer exchange (like
    PreviewPlayer): the audio thread never allocates or frees; a replaced clip is parked in `retired`. */
class Reference : public juce::ChangeBroadcaster
{
public:
    struct Info
    {
        juce::String path;
        int regionStart = 0, regionEnd = 0; // source samples; regionEnd 0 = whole file
        double sampleRate = 0.0;            // of the source file
        int totalSamples = 0;               // of the source file
        int note = -1;                      // MIDI note the user plays it at; -1 = follow sub_root
        float gainDb = 0.0f;
        RefMode mode = RefMode::synth;
        bool loaded = false;                // audio is available (file readable)
    };

    Reference() = default;
    ~Reference() override;

    // ---- message thread
    bool load (const juce::File&, int regionStart = 0, int regionEnd = 0);
    void clear();
    bool setRegion (int regionStart, int regionEnd);
    void setNote (int note);
    void setGainDb (float db);
    void setMode (RefMode m);
    bool hasReference() const; // a path is set (the file may be missing)
    bool hasAudio() const noexcept { return clipLoaded.load(); }
    Info getInfo() const;
    /** The playing region (mono, source rate) for display. Null when nothing is loaded. */
    std::shared_ptr<const std::vector<float>> getDisplay (double& rate) const;
    /** Bumped whenever reference.json content would change (load / clear / region / note). */
    int getVersion() const noexcept { return version.load(); }
    void collectGarbage() { delete retired.exchange (nullptr); }

    /** <kfLive> element (reference + gain + mode; the owner adds the link flag). */
    std::unique_ptr<juce::XmlElement> saveState() const;
    void restoreState (const juce::XmlElement&);

    // ---- audio thread
    void prepare (double sampleRate);
    /** Restart from the clip start `delaySamples` after the start of the current block. */
    void noteOn (int delaySamples) noexcept;
    RefMode getMode() const noexcept { return (RefMode) mode.load (std::memory_order_relaxed); }
    /** True when the reference should replace/join the synth (mode != synth and audio is loaded). */
    bool isAudible() const noexcept { return getMode() != RefMode::synth && clipLoaded.load (std::memory_order_relaxed); }
    /** Advances the playback by one block; adds it (all channels, at the reference gain) into `out` if `audible`. */
    void process (juce::AudioBuffer<float>& out, bool audible) noexcept;

private:
    struct Clip
    {
        std::shared_ptr<const std::vector<float>> data;
        double rate = 44100.0;
    };

    void publish (std::shared_ptr<const std::vector<float>> data, double rate);
    void takePending() noexcept;
    bool decode (const juce::File&, int rs, int re, bool storeRegion);

    mutable juce::CriticalSection lock; // settings + display (never taken on the audio thread)
    Info info;
    std::shared_ptr<const std::vector<float>> display;
    double displayRate = 0.0;

    std::atomic<Clip*> pending { nullptr }, retired { nullptr };
    std::atomic<bool> clipLoaded { false }, pendingClear { false };
    std::atomic<int> mode { 0 }, version { 0 };
    std::atomic<float> gain { 1.0f };

    // audio thread only
    Clip* current = nullptr;
    double hostRate = 44100.0, pos = 0.0;
    static constexpr int kMaxTrig = 64;
    int trig[kMaxTrig] {}; // pending (re)starts, in samples from the start of the block being built
    int nTrig = 0;
    bool playing = false;
};
} // namespace kf
