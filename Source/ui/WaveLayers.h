#pragma once
#include "CurveGeometry.h"
#include "live/Reference.h"
#include "presets/KickRenderer.h"
#include <atomic>
#include <functional>
#include <memory>

class KickforgeProcessor;

namespace kf::ui
{
/** Min/max of `data` (n samples at `sr`, sample 0 = note-on) under pixel column [px, px+1) of the curve view.
    `lengthMs` is the editor's time axis (normalized curve time 1.0 == lengthMs), so zoom, pan and log time are the
    view's own mapping. Returns false when the column lies entirely outside the data. */
inline bool waveColumn (const float* data, int n, double sr, const CurveView& v, double lengthMs, int px, float& lo, float& hi)
{
    if (n <= 0 || sr <= 0.0) return false;
    const double k = lengthMs * 0.001 * sr; // samples per unit of normalized time
    const double sa = v.xToTime ((float) px) * k, sb = v.xToTime ((float) px + 1.0f) * k;
    if (sb < 0.0 || sa > (double) (n - 1)) return false;
    const int i0 = std::clamp ((int) std::floor (std::min (sa, sb)), 0, n - 1);
    const int i1 = std::clamp ((int) std::ceil (std::max (sa, sb)), 0, n - 1);
    lo = hi = data[i0];
    for (int i = i0 + 1; i <= i1; ++i) { lo = std::min (lo, data[i]); hi = std::max (hi, data[i]); }
    return true;
}

/** Immutable result of one background render. */
struct WaveResult
{
    LayerBuffers buffers;
    juce::uint64 serial = 0;     // request generation this render covers
    double renderMs = 0.0;       // wall time of sync + render
};

/** Background renderer of the layered kick for the curve editor's backdrop. Watches the processor's parameters, curves,
    click layers and reference (listeners only set a flag; poll() runs on the message thread at 60 Hz and issues
    requests). Renders happen on one worker thread from a snapshot into a private scratch processor (the live voice and
    audio thread are never touched). Requests coalesce: at most one render in flight plus one pending; the last
    requested state is always rendered. The result is published as an immutable shared_ptr (atomic swap). */
class WaveService : private juce::Thread, private juce::Timer, private juce::AsyncUpdater,
                    private juce::AudioProcessorValueTreeState::Listener, private juce::ValueTree::Listener
{
public:
    static constexpr double renderRate = 44100.0;

    explicit WaveService (KickforgeProcessor&);
    ~WaveService() override;

    std::shared_ptr<const WaveResult> latest() const { return result.load(); }
    /** Any thread. */
    void requestRender();
    /** Message thread: issues a request when anything the render depends on changed. Called by the 60 Hz timer. */
    void poll();
    /** Blocks until every request made so far was rendered (tests). */
    bool waitIdle (int timeoutMs = 5000) const;

    std::function<void()> onResult; // message thread, after a new result was published

    juce::uint64 getRequestCount() const noexcept { return requested.load(); }
    juce::uint64 getRenderCount() const noexcept { return renders.load(); }

private:
    void run() override;
    void timerCallback() override { poll(); }
    void handleAsyncUpdate() override { if (onResult) onResult(); }
    void parameterChanged (const juce::String&, float) override { dirty.store (true, std::memory_order_relaxed); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { dirty.store (true, std::memory_order_relaxed); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { dirty.store (true, std::memory_order_relaxed); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { dirty.store (true, std::memory_order_relaxed); }
    void valueTreeRedirected (juce::ValueTree&) override { dirty.store (true, std::memory_order_relaxed); }

    KickforgeProcessor& proc;
    std::unique_ptr<LayerScratch> scratch;
    std::atomic<std::shared_ptr<const WaveResult>> result;
    std::atomic<juce::uint64> requested { 0 }, rendered { 0 }, renders { 0 };
    std::atomic<bool> dirty { true };
    juce::WaitableEvent wake;
    int lastClickVer = -1, lastRefVer = -1;
    juce::StringArray listened;
};

/** Reference kick for drawing: the playing region at its source rate, aligned at the onset, with the A/B gain applied. */
struct RefWave
{
    std::shared_ptr<const std::vector<float>> data;
    double rate = 0.0;
    int onset = 0;
    float gain = 1.0f;
    bool valid() const noexcept { return data != nullptr && rate > 0.0 && (int) data->size() > onset; }
};

/** Caches the onset of the displayed reference clip (first sample above 2 % of full scale, like the output scope). */
class RefTracker
{
public:
    RefWave get (Reference&);
private:
    std::shared_ptr<const std::vector<float>> seen;
    int onset = 0;
};

struct WaveDraw
{
    bool sub = true, clicks = true, reference = true, mix = false;
    int activeClick = -1; // click envelope tab: that layer is drawn brighter
};

namespace wavecol
{
inline const juce::Colour sub { 0xff6f93c8 }, ref { 0xfff2f2f2 }, mix { 0xffb48cff };
inline juce::Colour click (int l)
{
    static const juce::uint32 c[] = { 0xffffd166, 0xff6fe0a2, 0xffff7fb0, 0xff5fd6ff };
    return juce::Colour (c[std::clamp (l, 0, 3)]);
}
}

/** Draws the layers into `plot` using the editor's own view mapping. */
void paintWaveLayers (juce::Graphics&, const WaveResult*, const RefWave&, const WaveDraw&, const CurveView&,
                      juce::Rectangle<int> plot, double lengthMs);
} // namespace kf::ui
