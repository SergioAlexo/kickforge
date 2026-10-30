#include "WaveLayers.h"
#include "PluginProcessor.h"

namespace kf::ui
{
WaveService::WaveService (KickforgeProcessor& p)
    : juce::Thread ("Kickforge wave"), proc (p), scratch (std::make_unique<LayerScratch>())
{
    for (auto* prm : proc.getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (prm))
        {
            proc.apvts.addParameterListener (wp->paramID, this);
            listened.add (wp->paramID);
        }
    proc.apvts.state.addListener (this);
    result.store (std::make_shared<const WaveResult>());
    startThread (juce::Thread::Priority::low);
    startTimerHz (60);
    poll();
}

WaveService::~WaveService()
{
    stopTimer();
    cancelPendingUpdate();
    proc.apvts.state.removeListener (this);
    for (auto& id : listened) proc.apvts.removeParameterListener (id, this);
    signalThreadShouldExit();
    wake.signal();
    stopThread (5000);
}

void WaveService::requestRender()
{
    requested.fetch_add (1);
    wake.signal();
}

void WaveService::poll()
{
    bool want = dirty.exchange (false);
    const int cv = proc.getClickLayer().getStateVersion(), rv = proc.getReference().getVersion();
    if (cv != lastClickVer) { lastClickVer = cv; want = true; }
    if (rv != lastRefVer) { lastRefVer = rv; want = true; }
    if (want) requestRender();
}

bool WaveService::waitIdle (int timeoutMs) const
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (rendered.load() < requested.load())
    {
        if (juce::Time::getMillisecondCounter() > end) return false;
        juce::Thread::sleep (1);
    }
    return true;
}

void WaveService::run()
{
    while (! threadShouldExit())
    {
        wake.wait (-1);
        if (threadShouldExit()) break;
        for (;;)
        {
            const auto gen = requested.load(); // everything requested up to here is covered by this render
            if (gen == rendered.load() || threadShouldExit()) break;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            auto r = std::make_shared<WaveResult>();
            scratch->sync (proc);
            const double ms = std::clamp (LayerScratch::neededMs (proc) + 40.0, 100.0, 4500.0);
            r->buffers = scratch->render (ms * 0.001, renderRate);
            r->serial = gen;
            r->renderMs = juce::Time::getMillisecondCounterHiRes() - t0;
            result.store (std::shared_ptr<const WaveResult> (std::move (r)));
            renders.fetch_add (1);
            rendered.store (gen);
            triggerAsyncUpdate();
        }
    }
}

RefWave RefTracker::get (Reference& r)
{
    RefWave w;
    if (! r.hasAudio()) { seen.reset(); return w; }
    double rate = 0.0;
    auto d = r.getDisplay (rate);
    if (d == nullptr || rate <= 0.0) return w;
    if (d != seen)
    {
        seen = d;
        onset = 0;
        for (size_t i = 0; i < d->size(); ++i)
            if (std::abs ((*d)[i]) > 0.02f) { onset = (int) i; break; }
    }
    w.data = d;
    w.rate = rate;
    w.onset = onset;
    w.gain = juce::Decibels::decibelsToGain (r.getInfo().gainDb, -60.0f);
    return w;
}

namespace
{
constexpr float kFull = 0.94f; // |sample| == 1 reaches this fraction of half the plot height

struct Cols
{
    std::vector<float> lo, hi;
    std::vector<char> ok;
};

Cols columns (const float* d, int n, double sr, const CurveView& v, double lengthMs, int x0, int x1, float gain)
{
    Cols c;
    const int w = std::max (0, x1 - x0);
    c.lo.assign ((size_t) w, 0.0f);
    c.hi.assign ((size_t) w, 0.0f);
    c.ok.assign ((size_t) w, 0);
    for (int i = 0; i < w; ++i)
    {
        float lo, hi;
        if (waveColumn (d, n, sr, v, lengthMs, x0 + i, lo, hi))
        {
            c.lo[(size_t) i] = lo * gain;
            c.hi[(size_t) i] = hi * gain;
            c.ok[(size_t) i] = 1;
        }
    }
    return c;
}

void fillCols (juce::Graphics& g, const Cols& c, int x0, float cy, float half, juce::Colour col)
{
    juce::Path p;
    const int w = (int) c.ok.size();
    constexpr float minH = 0.004f; // keep silence visible as a hairline
    for (int i = 0; i < w;)
    {
        if (! c.ok[(size_t) i]) { ++i; continue; }
        int j = i;
        while (j < w && c.ok[(size_t) j]) ++j;
        p.startNewSubPath ((float) (x0 + i), cy - std::max (c.hi[(size_t) i], minH) * half);
        for (int k = i; k < j; ++k)
        {
            const float y = cy - std::max (c.hi[(size_t) k], minH) * half;
            p.lineTo ((float) (x0 + k), y);
            p.lineTo ((float) (x0 + k + 1), y);
        }
        for (int k = j - 1; k >= i; --k)
        {
            const float y = cy - std::min (c.lo[(size_t) k], -minH) * half;
            p.lineTo ((float) (x0 + k + 1), y);
            p.lineTo ((float) (x0 + k), y);
        }
        p.closeSubPath();
        i = j;
    }
    g.setColour (col);
    g.fillPath (p);
}

void outlineCols (juce::Graphics& g, const Cols& c, int x0, float cy, float half, juce::Colour col, float thick)
{
    juce::Path top, bot;
    bool first = true;
    for (size_t i = 0; i < c.ok.size(); ++i)
    {
        if (! c.ok[i]) { first = true; continue; }
        const float x = (float) x0 + (float) i + 0.5f;
        const float yt = cy - c.hi[i] * half, yb = cy - c.lo[i] * half;
        if (first) { top.startNewSubPath (x, yt); bot.startNewSubPath (x, yb); first = false; }
        else { top.lineTo (x, yt); bot.lineTo (x, yb); }
    }
    g.setColour (col);
    g.strokePath (top, juce::PathStrokeType (thick, juce::PathStrokeType::curved));
    g.strokePath (bot, juce::PathStrokeType (thick, juce::PathStrokeType::curved));
}
} // namespace

void paintWaveLayers (juce::Graphics& g, const WaveResult* res, const RefWave& ref, const WaveDraw& d, const CurveView& v,
                      juce::Rectangle<int> plot, double lengthMs)
{
    if (plot.isEmpty() || lengthMs <= 0.0) return;
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (plot);
    const float cy = (float) plot.getCentreY(), half = (float) plot.getHeight() * 0.5f * kFull;
    const int x0 = plot.getX(), x1 = plot.getRight();

    g.setColour (juce::Colours::white.withAlpha (0.06f)); // zero line: the waveform's own axis
    g.drawHorizontalLine ((int) cy, (float) x0, (float) x1);

    if (res != nullptr && ! res->buffers.mix.empty())
    {
        const auto& b = res->buffers;
        auto cols = [&] (const std::vector<float>& s)
        {
            return columns (s.data(), (int) s.size(), b.sampleRate, v, lengthMs, x0, x1, 1.0f);
        };
        if (d.mix)
            outlineCols (g, cols (b.mix), x0, cy, half, wavecol::mix.withAlpha (0.75f), 1.0f);
        if (d.sub)
        {
            const auto c = cols (b.layer[0]);
            fillCols (g, c, x0, cy, half, wavecol::sub.withAlpha (0.42f));
            outlineCols (g, c, x0, cy, half, wavecol::sub.brighter (0.2f).withAlpha (0.55f), 1.0f);
        }
        if (d.clicks)
            for (int l = 0; l < 4; ++l)
            {
                const bool active = d.activeClick == l;
                const float a = d.activeClick < 0 ? 0.7f : (active ? 0.7f : 0.3f);
                const auto c = cols (b.layer[(size_t) l + 1]);
                fillCols (g, c, x0, cy, half, wavecol::click (l).withAlpha (a));
                if (active) outlineCols (g, c, x0, cy, half, wavecol::click (l).withAlpha (0.8f), 1.0f);
            }
    }
    if (d.reference && ref.valid())
    {
        const float* rd = ref.data->data() + ref.onset;
        const int rn = (int) ref.data->size() - ref.onset;
        outlineCols (g, columns (rd, rn, ref.rate, v, lengthMs, x0, x1, ref.gain), x0, cy, half,
                     wavecol::ref.withAlpha (0.8f), 1.1f);
    }
}
} // namespace kf::ui
