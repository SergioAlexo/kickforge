#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <random>
#include "fx/FxChain.h"
#include "fx/FxLimiter.h"
#include "PluginProcessor.h"

using namespace kf;

namespace
{
constexpr double kSr = 48000.0;

FxSettings neutral()
{
    FxSettings s;
    s.limBypass = true; // pure (delayed) passthrough unless a test turns things on
    return s;
}

std::vector<float> run (const FxSettings& s, const std::vector<float>& in, int block = 200)
{
    FxChain chain;
    chain.setSettings (s);
    chain.prepare (kSr, block);
    std::vector<float> out (in.size());
    juce::AudioBuffer<float> buf (1, block);
    for (size_t pos = 0; pos < in.size();)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - pos);
        std::copy (in.begin() + (long) pos, in.begin() + (long) pos + n, buf.getWritePointer (0));
        chain.process (buf, 0, n);
        std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + n, out.begin() + (long) pos);
        pos += (size_t) n;
    }
    return out;
}

std::vector<float> sine (double f, float amp, int n)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = amp * (float) std::sin (2.0 * 3.14159265358979 * f * i / kSr);
    return v;
}

double rms (const std::vector<float>& v, size_t from)
{
    double s = 0;
    for (size_t i = from; i < v.size(); ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / (double) (v.size() - from));
}

double gainDb (const FxSettings& s, double f, float amp = 0.1f)
{
    const auto in = sine (f, amp, 48000);
    return 20.0 * std::log10 (rms (run (s, in), 24000) / rms (in, 24000));
}

double goertzel (const std::vector<float>& v, double f, size_t from)
{
    double re = 0, im = 0;
    for (size_t i = from; i < v.size(); ++i)
    {
        const double w = 2.0 * 3.14159265358979 * f * (double) i / kSr;
        re += v[i] * std::cos (w); im += v[i] * std::sin (w);
    }
    return std::sqrt (re * re + im * im) * 2.0 / (double) (v.size() - from);
}
}

TEST_CASE ("Fx: neutral chain is a pure passthrough with output gain applied")
{
    auto s = neutral();
    const auto in = sine (440.0, 0.5f, 9600);
    auto out = run (s, in);
    // limiter bypassed still delays by lookahead
    const int L = Limiter::lookaheadSamples (s.limLook, kSr);
    for (size_t i = 2000; i < in.size(); ++i)
        REQUIRE_THAT (out[i], Catch::Matchers::WithinAbs (in[i - (size_t) L], 1.0e-5));

    s.outDb = 6.0f;
    REQUIRE_THAT (gainDb (s, 1000.0), Catch::Matchers::WithinAbs (6.0, 0.1));
    s.outDb = -12.0f;
    REQUIRE_THAT (gainDb (s, 1000.0), Catch::Matchers::WithinAbs (-12.0, 0.1));
}

TEST_CASE ("Fx: limiter never exceeds the ceiling, all lookahead/release settings")
{
    std::mt19937 rng (7);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    std::vector<float> in (48000);
    for (size_t i = 0; i < in.size(); ++i)
    {
        const float burst = (i / 4000) % 3 == 0 ? 6.0f : 0.4f; // loud bursts, quiet gaps
        in[i] = burst * (0.7f * std::sin (0.05f * (float) i) + 0.3f * d (rng));
    }
    for (int look = 0; look < 3; ++look)
        for (int rel = 0; rel < 3; ++rel)
            for (float sat : { 0.0f, 1.0f })
            {
                FxSettings s;
                s.limIn = 12.0f; s.limCeil = -3.0f; s.limLook = look; s.limRel = rel; s.limSat = sat;
                const float ceil = std::pow (10.0f, -3.0f / 20.0f);
                const auto out = run (s, in);
                float peak = 0;
                for (float v : out) { REQUIRE (std::isfinite (v)); peak = std::max (peak, std::abs (v)); }
                REQUIRE (peak <= ceil * 1.00001f);
                REQUIRE (peak > ceil * 0.9f); // actually reaches the ceiling
            }
}

TEST_CASE ("Fx: limiter is transparent below the ceiling and reports lookahead latency")
{
    FxSettings s;
    s.limCeil = -0.3f;
    const auto in = sine (200.0, 0.3f, 9600);
    const auto out = run (s, in);
    for (int look = 0; look < 3; ++look)
    {
        s.limLook = look;
        const int L = Limiter::lookaheadSamples (look, kSr);
        const auto o = run (s, in);
        for (size_t i = 1000; i < in.size(); ++i)
            REQUIRE_THAT (o[i], Catch::Matchers::WithinAbs (in[i - (size_t) L], 1.0e-4));
    }
    FxChain c;
    c.setSettings (s);
    c.prepare (kSr, 256);
    REQUIRE (c.getLatencySamples() == Limiter::lookaheadSamples (s.limLook, kSr));
    REQUIRE (out.size() == in.size());
}

TEST_CASE ("Fx: processor reports limiter latency")
{
    juce::ScopedJuceInitialiser_GUI init;
    KickforgeProcessor proc;
    proc.setPlayConfigDetails (0, 2, kSr, 256);
    proc.prepareToPlay (kSr, 256);
    REQUIRE (proc.getLatencySamples() == Limiter::lookaheadSamples (1, kSr));
    proc.apvts.getParameter ("fx_lim_look")->setValueNotifyingHost (1.0f);
    juce::AudioBuffer<float> buf (2, 256);
    juce::MidiBuffer midi;
    proc.processBlock (buf, midi);
    REQUIRE (proc.getLatencySamples() == Limiter::lookaheadSamples (2, kSr));
}

TEST_CASE ("Fx: master EQ response at test frequencies")
{
    auto s = neutral();
    s.eqBypass = false;
    REQUIRE_THAT (gainDb (s, 1000.0), Catch::Matchers::WithinAbs (0.0, 0.05)); // flat

    auto peak = s; peak.p1F = 1000.0f; peak.p1G = 12.0f; peak.p1Q = 1.0f;
    REQUIRE_THAT (gainDb (peak, 1000.0), Catch::Matchers::WithinAbs (12.0, 0.3));
    REQUIRE (std::abs (gainDb (peak, 100.0)) < 1.0);
    REQUIRE (std::abs (gainDb (peak, 10000.0)) < 1.0);

    auto cut = s; cut.p2F = 3000.0f; cut.p2G = -9.0f;
    REQUIRE_THAT (gainDb (cut, 3000.0), Catch::Matchers::WithinAbs (-9.0, 0.3));

    auto lsh = s; lsh.lsF = 120.0f; lsh.lsG = 8.0f;
    REQUIRE_THAT (gainDb (lsh, 30.0), Catch::Matchers::WithinAbs (8.0, 0.6));
    REQUIRE (std::abs (gainDb (lsh, 5000.0)) < 0.3);

    auto hsh = s; hsh.hsF = 6000.0f; hsh.hsG = -10.0f;
    REQUIRE_THAT (gainDb (hsh, 15000.0), Catch::Matchers::WithinAbs (-10.0, 1.0));
    REQUIRE (std::abs (gainDb (hsh, 200.0)) < 0.3);

    auto lc = s; lc.lowCut = 200.0f;
    REQUIRE (gainDb (lc, 50.0) < -20.0);
    REQUIRE_THAT (gainDb (lc, 200.0), Catch::Matchers::WithinAbs (-3.0, 0.5));
    REQUIRE (std::abs (gainDb (lc, 4000.0)) < 0.3);

    auto byp = peak; byp.eqBypass = true;
    REQUIRE_THAT (gainDb (byp, 1000.0), Catch::Matchers::WithinAbs (0.0, 0.05));
}

TEST_CASE ("Fx: master compressor reduces level above threshold, passes below")
{
    auto s = neutral();
    s.compBypass = false; s.compThr = -20.0f; s.compRatio = 8.0f; s.compAtt = 1.0f; s.compRel = 50.0f;
    const double loud = gainDb (s, 1000.0, 0.5f); // -6 dBFS, 14 dB over
    REQUIRE (loud < -8.0);
    REQUIRE (loud > -14.0);
    REQUIRE (std::abs (gainDb (s, 1000.0, 0.02f)) < 0.5); // -34 dBFS, below threshold
    s.compMakeup = 6.0f;
    REQUIRE (gainDb (s, 1000.0, 0.5f) > loud + 5.0);
    s.compBypass = true;
    REQUIRE_THAT (gainDb (s, 1000.0, 0.5f), Catch::Matchers::WithinAbs (0.0, 0.05));
}

TEST_CASE ("Fx: every slot type stays finite and bounded at extreme settings, all quality modes")
{
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    std::vector<float> in (9600);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = (i % 2400 < 1200 ? 100.0f : 1.0f) * (0.6f * std::sin (0.2f * (float) i) + 0.4f * d (rng));

    for (int type = 1; type < FxSlot::NumTypes; ++type)
        for (int q = 0; q < 4; ++q)
            for (float knob : { 0.0f, 1.0f })
            {
                auto s = neutral();
                s.quality = q;
                s.slot[0] = { type, knob, knob, knob, 1.0f, false };
                s.slot[3] = { type, knob, 1.0f - knob, knob, 1.0f, false };
                s.limBypass = false; s.limCeil = -0.3f;
                const auto out = run (s, in, 137);
                for (float v : out)
                {
                    REQUIRE (std::isfinite (v));
                    REQUIRE (std::abs (v) <= 1.0f);
                }
            }
}

TEST_CASE ("Fx: distortion gain staging - unity-ish at zero drive, louder at high drive, bounded")
{
    for (int type : { (int) FxSlot::Tube, (int) FxSlot::Wave, (int) FxSlot::Drive, (int) FxSlot::Complex,
                      (int) FxSlot::Tape })
    {
        auto s = neutral();
        s.slot[0] = { type, 0.0f, 1.0f, 0.5f, 1.0f, false };
        if (type == FxSlot::Complex) s.slot[0] = { type, 0.0f, 0.0f, 1.0f, 1.0f, false };
        if (type == FxSlot::Wave || type == FxSlot::Drive) s.slot[0].c = 0.0f;
        const double g0 = gainDb (s, 500.0, 0.05f);
        INFO ("type " << type);
        REQUIRE (std::abs (g0) < 1.5);
        s.slot[0].a = 1.0f;
        const double g1 = gainDb (s, 500.0, 0.5f);
        REQUIRE (g1 < 10.5); // bounded (full-scale square = +9 dB re sine)
    }
    // clipper ceiling
    auto s = neutral();
    s.slot[0] = { (int) FxSlot::Clipper, 0.5f, 1.0f, 0.5f, 1.0f, false };
    const auto out = run (s, sine (300.0, 1.0f, 9600));
    float pk = 0; for (size_t i = 3000; i < out.size(); ++i) pk = std::max (pk, std::abs (out[i]));
    REQUIRE_THAT (pk, Catch::Matchers::WithinAbs (std::pow (10.0, -9.0 / 20.0), 0.01));
}

TEST_CASE ("Fx: slot bypass and mix=0 leave the signal intact")
{
    auto s = neutral();
    s.slot[0] = { (int) FxSlot::Clipper, 1.0f, 1.0f, 0.0f, 1.0f, true };
    const auto in = sine (200.0, 0.5f, 9600);
    const int L = Limiter::lookaheadSamples (s.limLook, kSr);
    auto o = run (s, in);
    for (size_t i = 1000; i < in.size(); ++i) REQUIRE_THAT (o[i], Catch::Matchers::WithinAbs (in[i - (size_t) L], 1e-5));
    s.slot[0].bypass = false; s.slot[0].mix = 0.0f;
    o = run (s, in);
    for (size_t i = 2000; i < in.size(); ++i) REQUIRE_THAT (o[i], Catch::Matchers::WithinAbs (in[i - (size_t) L], 1e-5));
}

TEST_CASE ("Fx: oversampling reduces aliasing of a hard clipper")
{
    // 9 kHz clipped: 5th harmonic 45 kHz aliases to 3 kHz at 48 kHz.
    auto s = neutral();
    s.slot[0] = { (int) FxSlot::Clipper, 0.6f, 1.0f, 1.0f, 1.0f, false };
    const auto in = sine (9000.0, 0.8f, 48000);
    s.quality = 0;
    const double aliased = goertzel (run (s, in), 3000.0, 12000);
    s.quality = 3;
    const double clean = goertzel (run (s, in), 3000.0, 12000);
    INFO ("aliased " << aliased << " clean " << clean);
    REQUIRE (aliased > 0.005);
    REQUIRE (clean < aliased * 0.25);
}

TEST_CASE ("Fx: brassify adds harmonics, bit crush quantises, ring mod shifts, delay echoes, filter filters")
{
    const auto in = sine (1000.0, 0.3f, 48000);
    {
        auto s = neutral();
        s.slot[0] = { (int) FxSlot::Brassify, 1.0f, 0.0f, 0.3f, 1.0f, false };
        const auto o = run (s, in);
        REQUIRE (goertzel (o, 2000.0, 12000) > 0.02);   // 2nd harmonic created
        REQUIRE (goertzel (run (neutral(), in), 2000.0, 12000) < 1e-3);
    }
    {
        auto s = neutral();
        s.slot[0] = { (int) FxSlot::BitCrush, 1.0f, 0.0f, 0.5f, 1.0f, false }; // 1 bit
        const auto o = run (s, in);
        std::vector<float> uniq;
        for (size_t i = 12000; i < o.size(); ++i)
            if (std::find_if (uniq.begin(), uniq.end(), [&] (float u) { return std::abs (u - o[i]) < 1e-6f; }) == uniq.end())
                uniq.push_back (o[i]);
        REQUIRE (uniq.size() <= 3);
    }
    {
        auto s = neutral();
        s.slot[0] = { (int) FxSlot::RingMod, 0.5f, 1.0f, 0.0f, 1.0f, false };
        const double f = 20.0 * std::pow (250.0, 0.5); // ~316 Hz carrier
        const auto o = run (s, in);
        REQUIRE (goertzel (o, 1000.0 + f, 12000) > 0.05);
        REQUIRE (goertzel (o, 1000.0, 12000) < 0.01);
    }
    {
        auto s = neutral();
        s.slot[0] = { (int) FxSlot::Delay, 0.1f, 0.0f, 0.0f, 1.0f, false }; // ~50.9 ms
        std::vector<float> imp (24000, 0.0f); imp[100] = 0.5f;
        const auto o = run (s, imp);
        const size_t echo = 100 + (size_t) std::lround (50.9 * 0.001 * kSr);
        float pk = 0; size_t at = 0;
        for (size_t i = 600; i < o.size(); ++i) if (std::abs (o[i]) > pk) { pk = std::abs (o[i]); at = i; }
        REQUIRE (pk > 0.2f);
        REQUIRE (std::abs ((long) at - (long) echo - (long) Limiter::lookaheadSamples (1, kSr)) <= 3);
    }
    {
        auto s = neutral();
        s.slot[0] = { (int) FxSlot::Filter, 0.3f, 0.0f, 0.0f, 1.0f, false }; // lowpass ~ 30*600^0.3 = 205 Hz
        REQUIRE (gainDb (s, 5000.0) < -30.0);
        REQUIRE (gainDb (s, 40.0) > -1.0);
    }
    {
        auto s = neutral();
        s.slot[0] = { (int) FxSlot::Reverb, 0.7f, 0.5f, 1.0f, 0.5f, false };
        std::vector<float> imp (48000, 0.0f); imp[10] = 0.8f;
        const auto o = run (s, imp);
        float late = 0; for (size_t i = 12000; i < 36000; ++i) late = std::max (late, std::abs (o[i]));
        REQUIRE (late > 1e-4f);
        for (float v : o) REQUIRE (std::isfinite (v));
    }
    {
        auto s = neutral();
        s.slot[0] = { (int) FxSlot::FetComp, 0.5f, 0.5f, 0.5f, 1.0f, false };
        REQUIRE (std::abs (gainDb (s, 1000.0, 0.5f)) < 40.0);
        s.slot[1] = { (int) FxSlot::Eq, 1.0f, 0.5f, 0.5f, 1.0f, false };
        REQUIRE (gainDb (s, 40.0, 0.01f) > 5.0);
    }
}

TEST_CASE ("Fx: macros drive a slot knob")
{
    auto s = neutral();
    s.slot[1] = { (int) FxSlot::Clipper, 1.0f, 1.0f, 1.0f, 0.0f, false }; // mix 0 -> clean
    const auto in = sine (200.0, 0.5f, 9600);
    const auto clean = run (s, in);
    s.macroTarget[2] = 1 + 1 * 4 + 3; // slot 1, mix
    s.macro[2] = 1.0f;
    const auto wet = run (s, in);
    double diff = 0;
    for (size_t i = 2000; i < in.size(); ++i) diff += std::abs (wet[i] - clean[i]);
    REQUIRE (diff > 1.0);
}

TEST_CASE ("Fx: processor APVTS wiring reaches the chain")
{
    juce::ScopedJuceInitialiser_GUI init;
    KickforgeProcessor proc;
    proc.setPlayConfigDetails (0, 2, kSr, 256);
    proc.prepareToPlay (kSr, 256);
    auto set = [&] (const char* id, float v)
    {
        auto* p = proc.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    set ("fx_a1_type", 1.0f); set ("fx_a1_a", 1.0f); set ("fx_quality", 2.0f);
    set ("fx_lim_ceil", -6.0f); set ("fx_lim_in", 12.0f);
    float peak = 0;
    for (int b = 0; b < 60; ++b)
    {
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);
        proc.processBlock (buf, midi);
        for (int i = 0; i < 256; ++i) { REQUIRE (std::isfinite (buf.getSample (0, i))); peak = std::max (peak, std::abs (buf.getSample (0, i))); }
    }
    REQUIRE (peak <= std::pow (10.0f, -6.0f / 20.0f) * 1.0001f + 1e-6f); // glob_gain default 0 dB assumed
    REQUIRE (peak > 0.1f);
}
