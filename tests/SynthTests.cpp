#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <atomic>
#include <cmath>
#include <thread>
#include "PluginProcessor.h"
#include "synth/SubVoice.h"

using namespace kf;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
constexpr double SR = 48000.0;

struct Rig
{
    CurveSlot pitch, amp;
    SubVoice v;
    SubSettings s;
    GlobSettings g;
    std::vector<float> out;

    explicit Rig (Curve p = Curve ({ { 0, 0, 0 }, { 1, 0, 0 } }), Curve a = Curve ({ { 0, 1, 0 }, { 1, 1, 0 } }))
        : pitch (std::move (p)), amp (std::move (a))
    {
        g.velSens = 0.0f;
        s.lengthMs = 1000.0f;
        v.prepare (SR, 256);
        v.setCurveSlots (&pitch, &amp);
        apply();
    }
    void apply() { v.setSettings (s, g); }

    void render (int n)
    {
        while (n > 0)
        {
            const int b = std::min (n, 256);
            juce::AudioBuffer<float> buf (1, b);
            buf.clear();
            v.process (buf, 0, b);
            for (int i = 0; i < b; ++i) out.push_back (buf.getSample (0, i));
            n -= b;
        }
    }
    int ms (double t) const { return (int) (t * 0.001 * SR); }
};

// positive-going zero crossing frequency in [a,b) samples
double zcFreq (const std::vector<float>& x, int a, int b)
{
    std::vector<double> c;
    for (int i = a + 1; i < b; ++i)
        if (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f)
            c.push_back ((double) i - 1 + (double) -x[(size_t) i - 1] / (double) (x[(size_t) i] - x[(size_t) i - 1]));
    if (c.size() < 2) return 0.0;
    return (double) (c.size() - 1) * SR / (c.back() - c.front());
}

float peak (const std::vector<float>& x, int a, int b)
{
    float p = 0.0f;
    for (int i = a; i < b && i < (int) x.size(); ++i) p = std::max (p, std::abs (x[(size_t) i]));
    return p;
}
}

TEST_CASE ("Sub: flat pitch curve gives the root frequency, key-tracked and tuned")
{
    Rig r;
    r.v.noteOn (33, 1.0f);
    r.render (r.ms (400));
    REQUIRE_THAT (zcFreq (r.out, r.ms (50), r.ms (390)), WithinRel (55.0, 0.003));

    Rig up; // +12 semitones via MIDI note
    up.v.noteOn (45, 1.0f);
    up.render (up.ms (400));
    REQUIRE_THAT (zcFreq (up.out, up.ms (50), up.ms (390)), WithinRel (110.0, 0.003));

    Rig tuned; // tune +7 st, +50 cents, key tracking off, A4=432
    tuned.s.tuneSemis = 7.0f; tuned.s.fineCents = 50.0f; tuned.s.keyTrack = false; tuned.g.a4 = 432.0f;
    tuned.apply();
    tuned.v.noteOn (80, 1.0f);
    tuned.render (tuned.ms (400));
    const double expect = 432.0 * std::pow (2.0, (33.0 + 7.5 - 69.0) / 12.0);
    REQUIRE_THAT (zcFreq (tuned.out, tuned.ms (50), tuned.ms (390)), WithinRel (expect, 0.003));
}

TEST_CASE ("Sub: pitch envelope falls from high to the base pitch (default curve)")
{
    Rig r (defaultPitchCurveSemitones(), defaultAmpCurve());
    r.s.lengthMs = 500.0f; r.apply();
    r.v.noteOn (33, 1.0f);
    r.render (2);
    const double f0 = r.v.getLastFrequency();
    r.render (r.ms (300));
    const double fEnd = r.v.getLastFrequency();
    REQUIRE (f0 > 55.0 * 4.0);
    REQUIRE_THAT (fEnd, WithinRel (55.0, 0.01));
}

TEST_CASE ("Sub: Hz pitch mode is absolute and scales with key tracking")
{
    Rig r (Curve ({ { 0, 100, 0 }, { 1, 100, 0 } }));
    r.s.pitchMode = PitchMode::Hz; r.apply();
    r.v.noteOn (33, 1.0f);
    r.render (r.ms (400));
    REQUIRE_THAT (zcFreq (r.out, r.ms (50), r.ms (390)), WithinRel (100.0, 0.003));
    Rig o (Curve ({ { 0, 100, 0 }, { 1, 100, 0 } }));
    o.s.pitchMode = PitchMode::Hz; o.apply();
    o.v.noteOn (45, 1.0f);
    o.render (o.ms (400));
    REQUIRE_THAT (zcFreq (o.out, o.ms (50), o.ms (390)), WithinRel (200.0, 0.003));
}

TEST_CASE ("Sub: start phase is honoured exactly")
{
    for (float deg : { 0.0f, 90.0f, 180.0f })
    {
        Rig r;
        r.s.startPhaseDeg = deg; r.apply();
        r.v.noteOn (33, 1.0f);
        r.render (400);
        const int i = 300; // after the declick fade-in
        const double expect = std::sin (2.0 * juce::MathConstants<double>::pi * (55.0 * i / SR + deg / 360.0));
        REQUIRE_THAT ((double) r.out[(size_t) i], WithinAbs (expect, 1e-3));
    }
}

TEST_CASE ("Sub: length scaling stretches the amp envelope; voice ends cleanly")
{
    auto render = [] (Rig& r, float lenMs)
    {
        r.s.lengthMs = lenMs; r.apply();
        r.v.noteOn (33, 1.0f);
        r.render (r.ms (1000));
    };
    Rig a (Curve ({ { 0, 0, 0 }, { 1, 0, 0 } }), defaultAmpCurve());
    Rig b (Curve ({ { 0, 0, 0 }, { 1, 0, 0 } }), defaultAmpCurve());
    render (a, 200.0f);
    render (b, 400.0f);
    REQUIRE (peak (a.out, a.ms (100), a.ms (190)) > 0.0f);
    REQUIRE (peak (a.out, a.ms (205), a.ms (1000)) == 0.0f);
    REQUIRE_FALSE (a.v.isActive());
    REQUIRE (peak (b.out, b.ms (205), b.ms (390)) > 0.0f);
    REQUIRE (peak (b.out, b.ms (405), b.ms (1000)) == 0.0f);
    // decay shape is the same, time-scaled: level at 50% length matches
    REQUIRE_THAT (peak (a.out, a.ms (95), a.ms (105)), WithinRel ((double) peak (b.out, b.ms (195), b.ms (205)), 0.1));
}

TEST_CASE ("Sub: velocity scales level")
{
    Rig a, b;
    a.g.velSens = b.g.velSens = 1.0f; a.apply(); b.apply();
    a.v.noteOn (33, 1.0f);  a.render (2000);
    b.v.noteOn (33, 0.5f);  b.render (2000);
    REQUIRE_THAT ((double) (peak (b.out, 0, 2000) / peak (a.out, 0, 2000)), WithinRel (0.5, 0.01));
    Rig c; c.g.velSens = 0.0f; c.apply();
    c.v.noteOn (33, 0.1f); c.render (2000);
    REQUIRE_THAT ((double) peak (c.out, 0, 2000), WithinRel ((double) peak (a.out, 0, 2000), 0.01));
}

TEST_CASE ("Sub: retrigger crossfades without a click")
{
    Rig r;
    r.s.startPhaseDeg = 90.0f; r.apply();
    r.v.noteOn (33, 1.0f);
    r.render (r.ms (100) + 17);
    r.v.noteOn (33, 1.0f);
    r.render (r.ms (100));
    float maxStep = 0.0f;
    for (size_t i = 1; i < r.out.size(); ++i) maxStep = std::max (maxStep, std::abs (r.out[i] - r.out[i - 1]));
    REQUIRE (maxStep < 0.1f);
    REQUIRE (peak (r.out, r.ms (100) + 200, r.ms (200)) > 0.5f); // new note is audible
}

TEST_CASE ("Sub: legato glides pitch without retriggering")
{
    Rig r;
    r.g.legato = true; r.g.glideMs = 100.0f; r.apply();
    r.v.noteOn (33, 1.0f);
    r.render (r.ms (200));
    r.v.noteOn (45, 1.0f);
    const size_t at = r.out.size();
    r.render (r.ms (50));
    const double mid = r.v.getLastFrequency();
    r.render (r.ms (300));
    REQUIRE (mid > 58.0);
    REQUIRE (mid < 108.0);
    REQUIRE_THAT (zcFreq (r.out, (int) at + r.ms (150), (int) at + r.ms (340)), WithinRel (110.0, 0.005));
    float maxStep = 0.0f;
    for (size_t i = at; i < r.out.size(); ++i) maxStep = std::max (maxStep, std::abs (r.out[i] - r.out[i - 1]));
    REQUIRE (maxStep < 0.1f);
}

TEST_CASE ("Sub: pitch bend follows the bend range")
{
    Rig r;
    r.g.bendRange = 12.0f; r.apply();
    r.v.noteOn (33, 1.0f);
    r.v.setPitchBend (0.5f); // +6 st
    r.render (r.ms (600));
    REQUIRE_THAT (zcFreq (r.out, r.ms (300), r.ms (590)), WithinRel (55.0 * std::pow (2.0, 0.5), 0.004));
}

TEST_CASE ("Sub: waveforms are finite, bounded, and at the right pitch")
{
    for (auto w : { SubWave::Sine, SubWave::Triangle, SubWave::Saw, SubWave::Square, SubWave::SoftSine })
    {
        Rig r;
        r.s.wave = w; r.apply();
        r.v.noteOn (33, 1.0f);
        r.render (r.ms (400));
        for (float x : r.out) { REQUIRE (std::isfinite (x)); REQUIRE (std::abs (x) < 1.2f); }
        REQUIRE (peak (r.out, 0, r.ms (400)) > 0.7f);
        REQUIRE_THAT (zcFreq (r.out, r.ms (50), r.ms (390)), WithinRel (55.0, 0.01));
    }
}

TEST_CASE ("Sub: gate mode releases on note-off, one-shot ignores it")
{
    Rig g;
    g.s.gate = GateMode::Gate; g.s.releaseMs = 20.0f; g.apply();
    g.v.noteOn (33, 1.0f);
    g.render (g.ms (100));
    g.v.noteOff (33);
    g.render (g.ms (200));
    REQUIRE (peak (g.out, g.ms (100) + g.ms (40), g.ms (300)) < 1e-3f);
    REQUIRE_FALSE (g.v.isActive());

    Rig o;
    o.v.noteOn (33, 1.0f);
    o.render (o.ms (100));
    o.v.noteOff (33);
    o.render (o.ms (100));
    REQUIRE (peak (o.out, o.ms (150), o.ms (200)) > 0.5f);
}

TEST_CASE ("Sub: muted layer is silent")
{
    Rig r;
    r.s.mute = true; r.apply();
    r.v.noteOn (33, 1.0f);
    r.render (2000);
    REQUIRE (peak (r.out, 0, 2000) == 0.0f);
}

TEST_CASE ("CurveSlot: swapping from another thread is safe while the audio side acquires")
{
    CurveSlot slot (Curve ({ { 0, 0, 0 }, { 1, 1, 0 } }));
    std::atomic<bool> stop { false };
    std::thread writer ([&]
    {
        for (int i = 0; i < 3000; ++i)
            slot.set (Curve ({ { 0, (double) i, 0 }, { 1, (double) i + 1, 0 } }));
        stop = true;
    });
    long reads = 0;
    while (! stop)
    {
        auto* s = slot.acquire();
        const double a = s->curve.eval (0.0), b = s->curve.eval (1.0);
        REQUIRE ((b == a + 1.0 || (a == 0.0 && b == 1.0))); // a snapshot is always internally consistent
        CurveSlot::release (s);
        ++reads;
    }
    writer.join();
    slot.collect();
    REQUIRE (slot.numSnapshotsAlive() == 1);
    REQUIRE (reads > 0);
}

TEST_CASE ("Sub curves persist in the plugin state and swap into the voice")
{
    juce::ScopedJuceInitialiser_GUI init;
    KickforgeProcessor a;
    const Curve custom ({ { 0, 12, -0.5 }, { 0.5, 3, 0.2 }, { 1, 0, 0 } });
    a.getSubCurves().setCurve (SubCurveId::Pitch, custom);

    juce::MemoryBlock mb;
    a.getStateInformation (mb);

    KickforgeProcessor b;
    REQUIRE_FALSE (b.getSubCurves().getCurve (SubCurveId::Pitch) == custom);
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    REQUIRE (b.getSubCurves().getCurve (SubCurveId::Pitch) == custom);
    REQUIRE (b.getSubCurves().getCurve (SubCurveId::Amp) == defaultAmpCurve());
}

TEST_CASE ("Processor: default kick has a fast pitch fall and decays to silence")
{
    juce::ScopedJuceInitialiser_GUI init;
    KickforgeProcessor p;
    p.setPlayConfigDetails (0, 2, SR, 256);
    p.prepareToPlay (SR, 256);
    std::vector<float> x;
    for (int b = 0; b < 250; ++b)
    {
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 33, (juce::uint8) 127), 0);
        p.processBlock (buf, midi);
        for (int i = 0; i < 256; ++i) x.push_back (buf.getSample (0, i));
    }
    REQUIRE (peak (x, 0, 4800) > 0.5f);
    REQUIRE (peak (x, 40000, 64000) == 0.0f);
    const double early = zcFreq (x, 20, 1500), late = zcFreq (x, 7000, 14000);
    REQUIRE (early > late * 2.0);
    REQUIRE_THAT (late, WithinRel (55.0, 0.05));
}
