#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <chrono>
#include <cmath>
#include "PluginProcessor.h"

namespace
{
constexpr double SR = 48000.0;
constexpr int BLOCK = 256;

void setP (KickforgeProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    REQUIRE (prm != nullptr);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

struct Stereo { std::vector<float> l, r; };

Stereo render (KickforgeProcessor& p, int blocks, int note = 36)
{
    Stereo o;
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buf (2, BLOCK);
        juce::MidiBuffer midi;
        if (b == 0 && note >= 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 110), 10);
        p.processBlock (buf, midi);
        for (int i = 0; i < BLOCK; ++i) { o.l.push_back (buf.getSample (0, i)); o.r.push_back (buf.getSample (1, i)); }
    }
    return o;
}

double rms (const std::vector<float>& x)
{
    double s = 0;
    for (float v : x) s += (double) v * v;
    return std::sqrt (s / (double) std::max<size_t> (1, x.size()));
}
float peakOf (const std::vector<float>& x)
{
    float p = 0;
    for (float v : x) { REQUIRE (std::isfinite (v)); p = std::max (p, std::abs (v)); }
    return p;
}

struct Proc
{
    juce::ScopedJuceInitialiser_GUI init;
    KickforgeProcessor p;
    Proc()
    {
        p.setPlayConfigDetails (0, 2, SR, BLOCK);
        p.prepareToPlay (SR, BLOCK);
    }
};
}

TEST_CASE ("Engine: click pan is constant-power and stereo; centre is unchanged")
{
    Proc t;
    setP (t.p, "sub_mute", 1);
    auto c = render (t.p, 40);
    REQUIRE (rms (c.l) > 0.001);
    REQUIRE (std::abs (rms (c.l) - rms (c.r)) < 1.0e-6);

    Proc t2;
    setP (t2.p, "sub_mute", 1);
    setP (t2.p, "click_1_pan", -1.0f);
    auto h = render (t2.p, 40);
    REQUIRE (rms (h.l) > rms (c.l));          // +3 dB at the extreme (centre is 0 dB)
    REQUIRE (rms (h.r) < 1.0e-4);

    Proc t3;
    setP (t3.p, "sub_mute", 1);
    setP (t3.p, "click_1_pan", 0.5f);
    auto q = render (t3.p, 40);
    const double power = rms (q.l) * rms (q.l) + rms (q.r) * rms (q.r);
    REQUIRE (power == Catch::Approx (2.0 * rms (c.l) * rms (c.l)).epsilon (0.02)); // constant power
}

TEST_CASE ("Engine: per-layer bus routing sends the sub through bus A only when asked")
{
    // bus A slot 1: Filter, cutoff max, high-pass => removes the sub completely
    auto setupFx = [] (KickforgeProcessor& p)
    {
        setP (p, "fx_a1_type", 12.0f);
        setP (p, "fx_a1_a", 1.0f);
        setP (p, "fx_a1_c", 0.5f);
        setP (p, "click_1_source", 0.0f);
    };
    Proc a; setupFx (a.p);
    const auto master = render (a.p, 60);
    Proc b; setupFx (b.p);
    setP (b.p, "sub_bus", 1.0f);
    const auto viaA = render (b.p, 60);
    Proc c; setupFx (c.p);
    setP (c.p, "sub_bus", 2.0f); // bus B has no effects: unchanged
    const auto viaB = render (c.p, 60);
    REQUIRE (rms (master.l) > 0.05);
    REQUIRE (rms (viaA.l) < 0.3 * rms (master.l));
    REQUIRE (rms (viaB.l) == Catch::Approx (rms (master.l)).epsilon (0.02));
}

TEST_CASE ("Engine: glob_gain is applied before the limiter, so the ceiling holds")
{
    Proc t;
    setP (t.p, "glob_gain", 6.0f);
    setP (t.p, "sub_gain", 6.0f);
    setP (t.p, "click_gain", 6.0f);
    const auto o = render (t.p, 80);
    const float ceil = juce::Decibels::decibelsToGain (-0.3f);
    REQUIRE (peakOf (o.l) > 0.5f);
    REQUIRE (peakOf (o.l) <= ceil * 1.001f);
    REQUIRE (peakOf (o.r) <= ceil * 1.001f);
}

// ---------------------------------------------------------------- sub layer extras
#include "synth/SubVoice.h"

namespace
{
struct SubRig
{
    kf::CurveSlot pitch, amp, drive, harm, kt;
    kf::SubVoice v;
    kf::SubSettings s;
    kf::GlobSettings g;
    explicit SubRig (kf::Curve p = kf::Curve ({ { 0, 0, 0 }, { 1, 0, 0 } }))
        : pitch (std::move (p)), amp (kf::Curve ({ { 0, 1, 0 }, { 1, 1, 0 } })),
          drive (kf::Curve ({ { 0, 1, 0 }, { 1, 1, 0 } })), harm (kf::Curve ({ { 0, 1, 0 }, { 1, 1, 0 } })),
          kt (kf::defaultKeyTrackCurve())
    {
        g.velSens = 0.0f;
        s.lengthMs = 1000.0f;
        v.prepare (SR, 256);
    }
    std::vector<float> run (int samples, int note = 33)
    {
        v.setCurveSlots (&pitch, &amp, &drive, &harm, &kt);
        v.setSettings (s, g);
        v.noteOn (note, 1.0f);
        std::vector<float> o;
        while ((int) o.size() < samples)
        {
            juce::AudioBuffer<float> b (1, 256);
            b.clear();
            v.process (b, 0, 256);
            for (int i = 0; i < 256; ++i) o.push_back (b.getSample (0, i));
        }
        return o;
    }
};

double goertzel (const std::vector<float>& x, int a, int b, double f)
{
    const double w = 2.0 * 3.14159265358979 * f / SR;
    double re = 0, im = 0;
    for (int i = a; i < b; ++i) { re += x[(size_t) i] * std::cos (w * i); im += x[(size_t) i] * std::sin (w * i); }
    return 2.0 * std::sqrt (re * re + im * im) / (double) (b - a);
}

double zcFreq (const std::vector<float>& x, int a, int b)
{
    std::vector<double> c;
    for (int i = a + 1; i < b; ++i)
        if (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f)
            c.push_back ((double) i - 1 + (double) -x[(size_t) i - 1] / (double) (x[(size_t) i] - x[(size_t) i - 1]));
    return c.size() < 2 ? 0.0 : (double) (c.size() - 1) * SR / (c.back() - c.front());
}
}

TEST_CASE ("Sub: harmonics add tuned partials with gain, decay and the harmonics-amount curve")
{
    const int a = 4800, b = 24000;
    SubRig base;
    const auto o0 = base.run (48000);
    REQUIRE (goertzel (o0, a, b, 110.0) < 0.01);

    SubRig h;
    h.s.harm[0].semis = 12.0f; h.s.harm[0].gainDb = -6.0f;
    const auto o1 = h.run (48000);
    REQUIRE (goertzel (o1, a, b, 110.0) == Catch::Approx (0.5).epsilon (0.05));
    REQUIRE (goertzel (o1, a, b, 55.0) == Catch::Approx (1.0).epsilon (0.05));

    SubRig hd; // S-06 decay
    hd.s.harm[0].gainDb = 0.0f; hd.s.harm[0].decayMs = 60.0f;
    const auto o2 = hd.run (48000);
    REQUIRE (goertzel (o2, 0, 9600, 110.0) > 10.0 * goertzel (o2, 24000, 33600, 110.0));

    SubRig hz; // S-07: curve at 0 mutes all harmonics
    hz.harm.set (kf::Curve ({ { 0, 0, 0 }, { 1, 0, 0 } }));
    hz.s.harm[0].gainDb = 0.0f;
    const auto o3 = hz.run (48000);
    REQUIRE (goertzel (o3, a, b, 110.0) < 0.01);

    SubRig h9; // all nine on, still finite
    for (auto& hm : h9.s.harm) hm.gainDb = -12.0f;
    for (float v : h9.run (48000)) REQUIRE (std::isfinite (v));
}

TEST_CASE ("Sub: drive adds odd harmonics and follows its curve")
{
    SubRig off;
    const auto o0 = off.run (48000);
    SubRig on;
    on.s.drive = 1.0f;
    const auto o1 = on.run (48000);
    REQUIRE (goertzel (o0, 4800, 24000, 165.0) < 0.005);
    REQUIRE (goertzel (o1, 4800, 24000, 165.0) > 0.05);
    SubRig cz;
    cz.s.drive = 1.0f;
    cz.drive.set (kf::Curve ({ { 0, 0, 0 }, { 1, 0, 0 } }));
    const auto o2 = cz.run (48000);
    REQUIRE (goertzel (o2, 4800, 24000, 165.0) < 0.005);
}

TEST_CASE ("Sub: start offset delays the layer")
{
    SubRig r;
    r.s.delayMs = 50.0f;
    const auto o = r.run (9600);
    for (int i = 0; i < 2300; ++i) REQUIRE (o[(size_t) i] == 0.0f);
    float pk = 0; for (int i = 2500; i < 9000; ++i) pk = std::max (pk, std::abs (o[(size_t) i]));
    REQUIRE (pk > 0.5f);
}

TEST_CASE ("Sub: selective key tracking keeps the thump fixed and transposes the tail")
{
    SubRig r;
    r.kt.set (kf::Curve ({ { 0, 0, 0 }, { 0.1, 0, 0 }, { 0.2, 1, 0 }, { 1, 1, 0 } }));
    const auto o = r.run (48000, 45); // +12 st
    REQUIRE (zcFreq (o, 480, 4000) == Catch::Approx (55.0).epsilon (0.03));
    REQUIRE (zcFreq (o, 24000, 44000) == Catch::Approx (110.0).epsilon (0.03));
}

TEST_CASE ("Sub: phase lock makes the tail independent of early pitch edits")
{
    auto tailDiff = [] (bool lock)
    {
        SubRig a (kf::Curve ({ { 0, 24, 0 }, { 0.1, 0, 0 }, { 1, 0, 0 } }));
        SubRig b (kf::Curve ({ { 0, 7, 0 }, { 0.1, 0, 0 }, { 1, 0, 0 } }));
        for (auto* r : { &a, &b }) { r->s.phaseLock = lock; r->s.lockPos = 0.3f; }
        const auto x = a.run (48000), y = b.run (48000);
        float d = 0;
        for (int i = 24000; i < 44000; ++i) d = std::max (d, std::abs (x[(size_t) i] - y[(size_t) i]));
        return d;
    };
    REQUIRE (tailDiff (true) < 1.0e-3f);
    REQUIRE (tailDiff (false) > 0.05f);
}
