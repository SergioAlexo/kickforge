#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

class KickforgeProcessor;

namespace kf
{
class Reference;

/** The plugin side of the AI link (docs/AI-LINK.md). While enabled it
      - keeps <Live>/state.kfpreset current (debounced; written on the message thread only),
      - keeps <Live>/reference.json current,
      - polls <Live>/apply.kfpreset and loads it exactly like a preset (the Reference and the link switch are
        not part of the applied state), then rewrites state.kfpreset and bumps status.json,
      - claims <Live>/status.json with its instance id; an instance that sees another id there turns itself off.
    Folder: env KICKFORGE_LIVE_DIR, else %APPDATA%\Kickforge\Live.
    setEnabled may be called from any thread (the first tick does the file work); everything else: message thread. */
class LiveLink : private juce::Timer,
                 private juce::ValueTree::Listener,
                 private juce::AudioProcessorValueTreeState::Listener
{
public:
    struct Status
    {
        bool enabled = false;
        bool superseded = false;   // turned off because another instance took the link
        bool lastApplyOk = true;
        int applied = 0;
        juce::String error, instance;
        juce::File folder;
    };

    static constexpr int tickMs = 40, pollMs = 250, quietMs = 100, maxWaitMs = 200;

    LiveLink (KickforgeProcessor&, Reference&);
    ~LiveLink() override;

    static juce::File resolveFolder();

    void setEnabled (bool);
    bool isEnabled() const noexcept { return enabled.load(); }
    Status getStatus() const;
    const juce::String& getInstanceId() const noexcept { return instanceId; }
    int getWriteCount() const noexcept { return writeCount; }

    /** The timer body; `nowMs` is juce::Time::getMillisecondCounter() (tests pass synthetic times). */
    void tick (juce::uint32 nowMs);

private:
    void timerCallback() override { tick (juce::Time::getMillisecondCounter()); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { markDirty(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { markDirty(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { markDirty(); }
    void valueTreeRedirected (juce::ValueTree&) override { markDirty(); }
    void parameterChanged (const juce::String&, float) override { markDirty(); }
    void markDirty() noexcept { dirty.store (true, std::memory_order_relaxed); }

    void claim();
    void writeState();
    void writeReference();
    void writeStatus();
    void checkSuperseded();
    void pollApply();
    bool writeAtomically (const juce::File&, const juce::String&);

    KickforgeProcessor& proc;
    Reference& ref;
    const juce::String instanceId;
    std::atomic<bool> enabled { false }, claimPending { false }, dirty { false };

    juce::File folder;
    bool superseded = false, lastApplyOk = true, pendingWrite = false;
    int applied = 0, writeCount = 0, lastClickVer = -1, lastRefVer = -1;
    juce::String error;
    juce::uint32 lastChange = 0, firstDirty = 0, lastPoll = 0;
};
} // namespace kf
