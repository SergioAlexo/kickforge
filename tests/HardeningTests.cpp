#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include "PluginProcessor.h"
#include "fx/FxChain.h"
#include "fx/FxLimiter.h"
#include "presets/PresetManager.h"

namespace
{
using Clock = std::chrono::steady_clock;

struct Stereo { std::vector<float> l, r; };

void setP (KickforgeProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    REQUIRE (prm != nullptr);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

struct Host
{
    juce::ScopedJuceInitialiser_GUI init;
    KickforgeProcessor p;
    double sr;
    explicit Host (double rate = 48000.0, int maxBlock = 4096) : sr (rate) { prepare (rate, maxBlock); }
    void prepare (double rate, int maxBlock)
    {
        sr = rate;
        p.setPlayConfigDetails (0, 2, rate, maxBlock);
        p.prepareToPlay (rate, maxBlock);
    }
    /** Renders `total` samples using the repeating block-size schedule; note-on (if >= 0) at sample 0. */
    Stereo render (int total, const std::vector<int>& sched, int note = 33, int vel = 110)
    {
        Stereo o;
        o.l.reserve ((size_t) total); o.r.reserve ((size_t) total);
        int done = 0, k = 0;
        while (done < total)
        {
            const int n = std::min (sched[(size_t) (k++ % (int) sched.size())], total - done);
            juce::AudioBuffer<float> buf (2, n);
            buf.clear();
            juce::MidiBuffer midi;
            if (done == 0 && note >= 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) vel), 0);
            p.processBlock (buf, midi);
            for (int i = 0; i < n; ++i) { o.l.push_back (buf.getSample (0, i)); o.r.push_back (buf.getSample (1, i)); }
            done += n;
        }
        return o;
    }
};

void muteClicks (KickforgeProcessor& p)
{
    for (int i = 1; i <= kf::kNumClickLayers; ++i)
        setP (p, ("click_" + juce::String (i) + "_mute").toRawUTF8(), 1.0f);
}

void flatSubOnly (KickforgeProcessor& p)
{
    p.getSubCurves().setCurve (kf::SubCurveId::Pitch, kf::Curve ({ { 0, 0, 0 }, { 1, 0, 0 } }));
    setP (p, "sub_length", 1500.0f);
    muteClicks (p);
}

double zcFreq (const std::vector<float>& x, double sr, double t0, double t1)
{
    const int a = (int) (t0 * sr), b = std::min ((int) x.size(), (int) (t1 * sr));
    std::vector<double> c;
    for (int i = a + 1; i < b; ++i)
        if (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f)
            c.push_back ((double) i - 1 + (double) -x[(size_t) i - 1] / (double) (x[(size_t) i] - x[(size_t) i - 1]));
    return c.size() < 3 ? 0.0 : (double) (c.size() - 1) * sr / (c.back() - c.front());
}

bool allFinite (const Stereo& s)
{
    for (float v : s.l) if (! std::isfinite (v)) return false;
    for (float v : s.r) if (! std::isfinite (v)) return false;
    return true;
}
float peak (const Stereo& s)
{
    float p = 0;
    for (float v : s.l) p = std::max (p, std::abs (v));
    for (float v : s.r) p = std::max (p, std::abs (v));
    return p;
}
}

// ------------------------------------------------------------------ stereo link
TEST_CASE ("Hardening: master limiter + compressor are stereo-linked (L/R ratio constant, ceiling holds)")
{
    using namespace kf;
    FxChain a, b;
    FxSettings s;
    s.compBypass = false; s.compThr = -30.0f; s.compRatio = 10.0f; s.compAtt = 1.0f; s.compRel = 80.0f;
    s.limIn = 12.0f; s.limCeil = -0.3f; s.limLook = 1;
    a.setSettings (s); b.setSettings (s);
    a.prepare (48000.0, 512); b.prepare (48000.0, 512);

    const int total = 48000;
    std::vector<float> l ((size_t) total), r ((size_t) total);
    for (int i = 0; i < total; ++i)
    {
        const float x = std::sin (2.0f * 3.14159265f * 110.0f * (float) i / 48000.0f) * (i < 24000 ? 0.9f : 0.3f);
        l[(size_t) i] = x; r[(size_t) i] = 0.2f * x; // hard-panned-ish layer
    }
    for (int i = 0; i < total; i += 512)
    {
        const int n = std::min (512, total - i);
        a.beginBlock(); b.beginBlock();
        FxChain::processMasterPair (a, b, l.data() + i, r.data() + i, n);
    }
    const float ceil = std::pow (10.0f, -0.3f / 20.0f);
    int checked = 0;
    for (int i = 4000; i < total; ++i)
    {
        REQUIRE (std::abs (l[(size_t) i]) <= ceil * 1.0001f);
        REQUIRE (std::abs (r[(size_t) i]) <= ceil * 1.0001f);
        if (std::abs (l[(size_t) i]) > 0.05f)
        {
            REQUIRE (r[(size_t) i] / l[(size_t) i] == Catch::Approx (0.2).epsilon (0.002));
            ++checked;
        }
    }
    REQUIRE (checked > 10000);
}

TEST_CASE ("Hardening: Comp and FetComp slots are stereo-linked")
{
    using namespace kf;
    for (int type : { (int) FxSlot::Comp, (int) FxSlot::FetComp })
    {
        INFO ("type " << type);
        FxChain a, b;
        FxSettings s;
        s.limBypass = true;
        s.slot[0].type = type; s.slot[0].a = 0.2f; s.slot[0].b = 0.9f; s.slot[0].c = 0.3f; s.slot[0].mix = 1.0f;
        a.setSettings (s); b.setSettings (s);
        a.prepare (48000.0, 512); b.prepare (48000.0, 512);
        const int total = 24000;
        std::vector<float> l ((size_t) total), r ((size_t) total);
        for (int i = 0; i < total; ++i)
        {
            const float x = std::sin (2.0f * 3.14159265f * 90.0f * (float) i / 48000.0f) * (i % 8000 < 4000 ? 0.8f : 0.05f);
            l[(size_t) i] = x; r[(size_t) i] = 0.1f * x;
        }
        for (int i = 0; i < total; i += 512)
        {
            const int n = std::min (512, total - i);
            a.beginBlock(); b.beginBlock();
            FxChain::processBusPair (a, b, 0, l.data() + i, r.data() + i, n);
        }
        int checked = 0;
        for (int i = 2000; i < total; ++i)
            if (std::abs (l[(size_t) i]) > 0.02f)
            {
                REQUIRE (r[(size_t) i] / l[(size_t) i] == Catch::Approx (0.1).epsilon (type == (int) FxSlot::Comp ? 0.003 : 0.03)); // FetComp has a tanh stage after the gain
                ++checked;
            }
        REQUIRE (checked > 1000);
    }
}

TEST_CASE ("Hardening: a panned layer under heavy limiting keeps the L/R ratio (full processor)")
{
    Host h;
    muteClicks (h.p);
    setP (h.p, "sub_pan", 0.5f);
    setP (h.p, "sub_gain", 6.0f);
    setP (h.p, "glob_gain", 6.0f);
    setP (h.p, "fx_lim_in", 12.0f);
    h.p.getSubCurves().setCurve (kf::SubCurveId::Pitch, kf::Curve ({ { 0, 0, 0 }, { 1, 0, 0 } }));
    const auto o = h.render (24000, { 256 });
    REQUIRE (allFinite (o));
    const float ceil = juce::Decibels::decibelsToGain (-0.3f);
    REQUIRE (peak (o) <= ceil * 1.0001f);
    const double ratio = std::tan (1.5 * 3.14159265358979 * 0.25); // pan 0.5 constant-power: R/L
    int checked = 0;
    for (size_t i = 3000; i < o.l.size(); ++i)
        if (std::abs (o.l[i]) > 0.1f)
        {
            REQUIRE (o.r[i] / o.l[i] == Catch::Approx (ratio).epsilon (0.01));
            ++checked;
        }
    REQUIRE (checked > 2000);
}

// ------------------------------------------------------------------ host robustness
TEST_CASE ("Hardening: pitch is correct at every sample rate")
{
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        INFO ("sr " << sr);
        Host h (sr);
        flatSubOnly (h.p);
        const auto o = h.render ((int) (0.7 * sr), { 512 });
        REQUIRE (allFinite (o));
        REQUIRE (zcFreq (o.l, sr, 0.1, 0.6) == Catch::Approx (55.0).epsilon (0.01));
    }
}

TEST_CASE ("Hardening: output is independent of the host block size")
{
    const int total = 24000;
    Host ref;
    const auto a = ref.render (total, { 256 });
    REQUIRE (peak (a) > 0.1f);
    const std::vector<std::vector<int>> scheds { { 1 }, { 7 }, { 64 }, { 512 }, { 4096 }, { 1, 7, 64, 512, 4096, 33, 3, 1000 } };
    for (auto& sc : scheds)
    {
        INFO ("first block " << sc[0] << " (" << sc.size() << " sizes)");
        Host h;
        const auto b = h.render (total, sc);
        REQUIRE (allFinite (b));
        float d = 0;
        for (size_t i = 0; i < a.l.size(); ++i) d = std::max ({ d, std::abs (a.l[i] - b.l[i]), std::abs (a.r[i] - b.r[i]) });
        REQUIRE (d < 1.0e-3f);
    }
}

TEST_CASE ("Hardening: prepareToPlay twice and sample-rate change mid-session")
{
    Host h;
    flatSubOnly (h.p);
    const auto first = h.render (24000, { 256 });
    h.prepare (48000.0, 4096);               // same settings again
    const auto again = h.render (24000, { 256 });
    float d = 0;
    for (size_t i = 0; i < first.l.size(); ++i) d = std::max (d, std::abs (first.l[i] - again.l[i]));
    REQUIRE (d < 1.0e-5f);                   // reset to the same initial state

    h.render (4000, { 100 }, 33);            // note sounding...
    h.prepare (96000.0, 512);                // ...then the host changes the rate
    const auto o = h.render (67200, { 512 });
    REQUIRE (allFinite (o));
    REQUIRE (zcFreq (o.l, 96000.0, 0.1, 0.6) == Catch::Approx (55.0).epsilon (0.01));
    h.prepare (44100.0, 128);
    const auto o2 = h.render (30870, { 128 });
    REQUIRE (zcFreq (o2.l, 44100.0, 0.1, 0.6) == Catch::Approx (55.0).epsilon (0.01));
    REQUIRE (h.p.getLatencySamples() == kf::Limiter::lookaheadSamples (1, 44100.0));
}

TEST_CASE ("Hardening: save/load state while a note is playing")
{
    Host h;
    h.render (6000, { 256 });                // mid-note
    juce::MemoryBlock mb;
    h.p.getStateInformation (mb);
    REQUIRE (mb.getSize() > 0);
    h.p.setStateInformation (mb.getData(), (int) mb.getSize());
    const auto o = h.render (24000, { 256 }, -1);
    REQUIRE (allFinite (o));
    REQUIRE (peak (o) <= 1.0f);

    juce::MemoryBlock mb2;
    h.p.getStateInformation (mb2);
    Host other;
    other.p.setStateInformation (mb2.getData(), (int) mb2.getSize());
    juce::MemoryBlock mb3;
    other.p.getStateInformation (mb3);
    REQUIRE (mb2.getSize() == mb3.getSize());
    REQUIRE (mb2 == mb3);
    REQUIRE (allFinite (other.render (12000, { 100 })));
}

TEST_CASE ("Hardening: every factory preset at every quality mode is finite and within the ceiling")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("kf_hard_" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    dir.createDirectory();
    {
        Host h (48000.0, 512);
        kf::PresetManager pm (h.p, dir);
        for (auto& f : kf::factoryPresets())
        {
            const int idx = pm.findByName (f.name);
            REQUIRE (idx >= 0);
            for (int q = 0; q < 4; ++q)
            {
                INFO (f.name << " quality " << q);
                REQUIRE (pm.load (idx));
                setP (h.p, "fx_quality", (float) q);
                h.prepare (48000.0, 512);
                const auto o = h.render (36000, { 512, 100 }, 36, 127);
                REQUIRE (allFinite (o));
                const bool lim = h.p.apvts.getRawParameterValue ("fx_lim_byp")->load() < 0.5f;
                const float ceil = juce::Decibels::decibelsToGain (h.p.apvts.getRawParameterValue ("fx_lim_ceil")->load());
                if (lim) REQUIRE (peak (o) <= ceil * 1.001f);
                else REQUIRE (peak (o) < 64.0f);
            }
        }
    }
    dir.deleteRecursively();
}

TEST_CASE ("Hardening: parameter automation spam (10 s) stays finite and does not spike in time")
{
    Host h (48000.0, 512);
    juce::Random rnd (1234);
    const auto& params = h.p.getParameters();
    const int blocks = (int) (10.0 * 48000.0 / 512.0);
    std::vector<double> times;
    times.reserve ((size_t) blocks);
    bool finite = true;
    int subnormals = 0;
    for (int b = 0; b < blocks; ++b)
    {
        for (auto* prm : params) prm->setValueNotifyingHost (rnd.nextFloat());
        juce::AudioBuffer<float> buf (2, 512);
        buf.clear();
        juce::MidiBuffer midi;
        if (b % 12 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 24 + rnd.nextInt (40), (juce::uint8) 100), rnd.nextInt (512));
        if (b % 12 == 6) midi.addEvent (juce::MidiMessage::noteOff (1, 40), 3);
        const auto t0 = Clock::now();
        h.p.processBlock (buf, midi);
        times.push_back (std::chrono::duration<double, std::milli> (Clock::now() - t0).count());
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) finite = false;
                if (std::fpclassify (v) == FP_SUBNORMAL) ++subnormals;
            }
    }
    REQUIRE (finite);
    REQUIRE (subnormals == 0);
    auto sorted = times;
    std::sort (sorted.begin(), sorted.end());
    INFO ("median " << sorted[sorted.size() / 2] << " ms, max " << sorted.back() << " ms");
    REQUIRE (sorted.back() < 250.0);           // no pathological block (denormal storms / runaway allocation)

    // after the spam, reset to defaults: silent idle blocks must not be slower than on a fresh instance
    for (auto* prm : params) prm->setValueNotifyingHost (prm->getDefaultValue());
    auto idle = [] (KickforgeProcessor& p)
    {
        std::vector<double> t;
        for (int b = 0; b < 400; ++b)
        {
            juce::AudioBuffer<float> buf (2, 512);
            buf.clear();
            juce::MidiBuffer midi;
            const auto t0 = Clock::now();
            p.processBlock (buf, midi);
            t.push_back (std::chrono::duration<double, std::milli> (Clock::now() - t0).count());
        }
        std::sort (t.begin(), t.end());
        return t[t.size() / 2];
    };
    idle (h.p); // let tails (delay/reverb) flush
    Host fresh (48000.0, 512);
    const double tSpam = idle (h.p), tFresh = idle (fresh.p);
    INFO ("idle median after spam " << tSpam << " ms, fresh " << tFresh << " ms");
    REQUIRE (tSpam < tFresh * 8.0 + 0.2);
}

TEST_CASE ("Hardening: latency tracks the limiter lookahead; quality change keeps audio sane")
{
    Host h (48000.0, 256);
    for (int look = 0; look < 3; ++look)
    {
        setP (h.p, "fx_lim_look", (float) look);
        h.render (1024, { 256 }, -1);
        REQUIRE (h.p.getLatencySamples() == kf::Limiter::lookaheadSamples (look, 48000.0));
    }
    for (int q : { 3, 0, 2, 1 })
    {
        setP (h.p, "fx_quality", (float) q);
        setP (h.p, "fx_a1_type", 1.0f); // clipper (oversampled)
        const auto o = h.render (9600, { 256 }, 36);
        REQUIRE (allFinite (o));
        REQUIRE (h.p.getLatencySamples() == kf::Limiter::lookaheadSamples (2, 48000.0));
    }
    // prepareToPlay resets: silence afterwards, latency reported immediately
    h.prepare (48000.0, 256);
    REQUIRE (h.p.getLatencySamples() == kf::Limiter::lookaheadSamples (2, 48000.0));
    const auto s = h.render (4096, { 256 }, -1);
    REQUIRE (peak (s) == 0.0f);
}
