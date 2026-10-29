#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include "click/ClickLayer.h"

using namespace kf;
using Catch::Matchers::WithinAbs;

namespace
{
constexpr double SR = 48000.0;

float peakOf (const std::vector<float>& v, int from = 0, int to = -1)
{
    float p = 0;
    if (to < 0) to = (int) v.size();
    for (int i = from; i < to; ++i) p = std::max (p, std::abs (v[(size_t) i]));
    return p;
}
int zeroCrossings (const std::vector<float>& v, int n)
{
    int z = 0;
    for (int i = 1; i < n; ++i) z += (v[(size_t) i - 1] < 0) != (v[(size_t) i] < 0);
    return z;
}
/** Index where the amplitude first falls below 0.001 of the peak and stays there. */
int minus60Index (const std::vector<float>& v)
{
    const float thr = peakOf (v) * 0.001f;
    int last = 0;
    for (int i = 0; i < (int) v.size(); ++i)
        if (std::abs (v[(size_t) i]) > thr) last = i;
    return last;
}

/** Layer test rig: all layers off except layer 0 set up by the caller. */
struct Rig
{
    ClickLayer layer;
    Rig()
    {
        layer.prepare (SR, 512);
        for (int l = 0; l < kNumClickLayers; ++l)
            layer.setParam (l, CP::source, (float) srcOff);
        layer.setParam (0, CP::gain, 0.0f);
    }
    std::vector<float> render (int n, int note = 36, float vel = 1.0f)
    {
        layer.noteOn (note, vel);
        std::vector<float> out;
        juce::AudioBuffer<float> buf (1, 512);
        for (int done = 0; done < n; done += 512)
        {
            buf.clear();
            layer.process (buf, 0, 512);
            for (int i = 0; i < 512; ++i) out.push_back (buf.getSample (0, i));
        }
        out.resize ((size_t) n);
        return out;
    }
    void sampleLayer (std::vector<float> data, double rate = SR)
    {
        layer.loadSampleFromMemory (0, std::move (data), rate, "mem");
        layer.setParam (0, CP::source, (float) srcSample);
    }
};

std::vector<float> impulseAt (int idx, int len = 2000)
{
    std::vector<float> d ((size_t) len, 0.0f);
    d[(size_t) idx] = 1.0f;
    return d;
}
int argMax (const std::vector<float>& v)
{
    int b = 0;
    for (int i = 0; i < (int) v.size(); ++i)
        if (std::abs (v[(size_t) i]) > std::abs (v[(size_t) b])) b = i;
    return b;
}
}

TEST_CASE ("Click generators: finite, audible, decaying, bounded")
{
    for (int t = 0; t < (int) ClickType::count; ++t)
        for (float color : { 0.0f, 0.5f, 1.0f })
        {
            ClickSettings s { (ClickType) t, 3000.0f, 20.0f, color };
            auto x = renderClick (s, SR, 9600);
            INFO (clickTypeName ((ClickType) t) << " color " << color);
            for (float v : x) REQUIRE (std::isfinite (v));
            REQUIRE (peakOf (x) > 0.05f);
            REQUIRE (peakOf (x) <= 1.5f);
            REQUIRE (peakOf (x, 4800) < peakOf (x) * 0.01f); // 100 ms in: silent vs. 20 ms decay
        }
}

TEST_CASE ("Click decay control sets the -60 dB time")
{
    ClickSettings s { ClickType::chirp, 1500.0f, 40.0f, 0.3f };
    const int i40 = minus60Index (renderClick (s, SR, 48000));
    s.decayMs = 10.0f;
    const int i10 = minus60Index (renderClick (s, SR, 48000));
    REQUIRE (i10 < i40 / 2);
    REQUIRE (std::abs (i40 - 40 * 48) < 40 * 48 * 0.15); // close to 40 ms
}

TEST_CASE ("Click tone control moves the frequency")
{
    ClickSettings lo { ClickType::chirp, 800.0f, 60.0f, 0.0f };
    ClickSettings hi { ClickType::chirp, 3200.0f, 60.0f, 0.0f };
    const int zl = zeroCrossings (renderClick (lo, SR, 4800), 4800);
    const int zh = zeroCrossings (renderClick (hi, SR, 4800), 4800);
    REQUIRE (zh > zl * 3); // two octaves up -> ~4x
    ClickSettings il { ClickType::impulse, 500.0f, 30.0f, 0.5f };
    ClickSettings ih { ClickType::impulse, 4000.0f, 30.0f, 0.5f };
    REQUIRE (zeroCrossings (renderClick (ih, SR, 4800), 4800) > 4 * zeroCrossings (renderClick (il, SR, 4800), 4800));
    // noise colour: dark has fewer zero crossings than bright
    ClickSettings nd { ClickType::noiseBurst, 1000.0f, 50.0f, 0.0f };
    ClickSettings nb { ClickType::noiseBurst, 1000.0f, 50.0f, 1.0f };
    REQUIRE (zeroCrossings (renderClick (nb, SR, 2400), 2400) > 2 * zeroCrossings (renderClick (nd, SR, 2400), 2400));
}

TEST_CASE ("Default layer sounds with zero files")
{
    ClickLayer layer; // unbound: defaults => layer 1 = beater
    layer.prepare (SR, 512);
    layer.noteOn (36, 1.0f);
    juce::AudioBuffer<float> buf (1, 4800);
    buf.clear();
    layer.process (buf, 0, 4800);
    float p = 0;
    for (int i = 0; i < 4800; ++i) { REQUIRE (std::isfinite (buf.getSample (0, i))); p = std::max (p, std::abs (buf.getSample (0, i))); }
    REQUIRE (p > 0.05f);
}

TEST_CASE ("Sample layer plays the loaded audio, start, delay, length")
{
    Rig r;
    r.sampleLayer (impulseAt (100));
    r.layer.setParam (0, CP::length, 2000.0f);
    REQUIRE (argMax (r.render (2400)) == 100);

    r.layer.setParam (0, CP::start, 1.0f); // 48 samples into the sample
    REQUIRE (argMax (r.render (2400)) == 52);

    r.layer.setParam (0, CP::delay, 10.0f); // +480
    REQUIRE (argMax (r.render (2400)) == 52 + 480);

    // before the delay it is silent
    r.layer.setParam (0, CP::start, 0.0f);
    auto x = r.render (2400);
    REQUIRE (peakOf (x, 0, 480) == 0.0f);

    // length trims the sample
    r.layer.setParam (0, CP::delay, 0.0f);
    r.layer.setParam (0, CP::length, 1.0f); // 48 samples: impulse at 100 is cut off
    REQUIRE (peakOf (r.render (2400)) < 1.0e-6f);
}

TEST_CASE ("Sample pitch, key tracking, flip, gain, mute/solo")
{
    Rig r;
    r.sampleLayer (impulseAt (480));
    REQUIRE (argMax (r.render (2400)) == 480);
    r.layer.setParam (0, CP::pitch, 12.0f); // an octave up halves the time
    REQUIRE (std::abs (argMax (r.render (2400)) - 240) <= 1);
    r.layer.setParam (0, CP::pitch, 0.0f);

    r.layer.setParam (0, CP::keyTrack, 1.0f);
    REQUIRE (std::abs (argMax (r.render (2400, 48)) - 240) <= 1); // note 48 = +12 st from 36
    REQUIRE (argMax (r.render (2400, 36)) == 480);
    r.layer.setParam (0, CP::keyTrack, 0.0f);

    auto pos = r.render (2400);
    r.layer.setParam (0, CP::flip, 1.0f);
    auto neg = r.render (2400);
    REQUIRE_THAT (neg[480] + pos[480], WithinAbs (0.0, 1e-4));
    REQUIRE (pos[480] > 0.5f);
    r.layer.setParam (0, CP::flip, 0.0f);

    r.layer.setParam (0, CP::gain, -6.0206f);
    REQUIRE_THAT (r.render (2400)[480], WithinAbs (0.5, 0.01));
    r.layer.setParam (0, CP::gain, 0.0f);
    REQUIRE_THAT (r.render (2400, 36, 0.5f)[480], WithinAbs (0.5, 0.01)); // velocity

    r.layer.setParam (0, CP::mute, 1.0f);
    REQUIRE (peakOf (r.render (2400)) == 0.0f);
    r.layer.setParam (0, CP::mute, 0.0f);

    // solo on another layer silences this one
    r.layer.setParam (1, CP::source, (float) srcBeater);
    r.layer.setParam (1, CP::solo, 1.0f);
    r.layer.noteOn (36, 1.0f);
    juce::AudioBuffer<float> b (1, 2400);
    b.clear();
    r.layer.process (b, 0, 2400);
    REQUIRE (std::abs (b.getSample (0, 480)) < 0.9f); // sample impulse is gone (beater has no 1.0 spike here)
}

TEST_CASE ("Samples are resampled to the session rate")
{
    Rig r;
    r.sampleLayer (impulseAt (240, 1000), 24000.0); // 10 ms at 24k => 20 ms at 48k
    REQUIRE (r.layer.getSampleLength (0) == 2000);
    REQUIRE (std::abs (argMax (r.render (2400)) - 480) <= 6); // Lagrange resampler adds a few samples of delay

    // changing the session rate re-resamples in the background
    r.layer.prepare (96000.0, 512);
    REQUIRE (r.layer.waitForLoads());
    REQUIRE (r.layer.getSampleLength (0) == 4000);
}

TEST_CASE ("Amp envelope curve shapes the layer")
{
    Rig r;
    r.sampleLayer (std::vector<float> (4800, 0.5f)); // 100 ms DC
    r.layer.setParam (0, CP::length, 100.0f);
    r.layer.setEnvelope (0, Curve ({ { 0, 1, 0 }, { 1, 0, 0 } }));
    auto x = r.render (4800);
    REQUIRE_THAT (x[10], WithinAbs (0.5, 0.01));
    REQUIRE_THAT (x[2400], WithinAbs (0.25, 0.02));
    REQUIRE (std::abs (x[4700]) < 0.05f);
    // the length control also ends the sound (with a fade, no click)
    r.layer.setEnvelope (0, Curve ({ { 0, 1, 0 }, { 1, 1, 0 } }));
    r.layer.setParam (0, CP::length, 20.0f);
    x = r.render (4800);
    REQUIRE (peakOf (x, 1100, 4800) == 0.0f);
    REQUIRE (std::abs (x[958]) < 0.1f); // within the 2 ms tail fade
}

TEST_CASE ("Layer filter attenuates highs when low-passed")
{
    Rig r;
    r.layer.setParam (0, CP::source, (float) srcNoise);
    r.layer.setParam (0, CP::tone, 12000.0f);
    r.layer.setParam (0, CP::color, 1.0f);
    r.layer.setParam (0, CP::decay, 200.0f);
    auto raw = r.render (4800);
    r.layer.setParam (0, CP::filterType, (float) filtLP);
    r.layer.setParam (0, CP::cutoff, 300.0f);
    auto lp = r.render (4800);
    REQUIRE (zeroCrossings (lp, 4800) < zeroCrossings (raw, 4800) / 3);
    for (float v : lp) REQUIRE (std::isfinite (v));
}

TEST_CASE ("Loading real files: WAV load off-thread, missing file, state round trip")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kf_click_test");
    dir.createDirectory();
    auto wav = dir.getChildFile ("hit.wav");
    wav.deleteFile();
    {
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::FileOutputStream> os (wav.createOutputStream());
        REQUIRE (os != nullptr);
        std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (os.get(), 44100.0, 2, 16, {}, 0));
        REQUIRE (w != nullptr);
        os.release();
        juce::AudioBuffer<float> b (2, 4410);
        b.clear();
        b.setSample (0, 441, 0.8f);
        b.setSample (1, 441, 0.8f);
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    Rig r;
    r.layer.setParam (0, CP::source, (float) srcSample);
    r.layer.loadSample (0, wav);
    REQUIRE (r.layer.waitForLoads());
    REQUIRE (r.layer.getSampleStatus (0) == ClickLayer::SampleStatus::loaded);
    REQUIRE (r.layer.getSampleLength (0) == (int) std::floor (4410 * SR / 44100.0));
    auto x = r.render (4800);
    REQUIRE (std::abs (argMax (x) - (int) std::lround (441 * SR / 44100.0)) <= 2);
    REQUIRE (peakOf (x) > 0.6f);

    // state round trip into a fresh layer
    r.layer.setEnvelope (0, Curve ({ { 0, 1, 0 }, { 0.5, 0.25, 0.3 }, { 1, 0, 0 } }));
    auto state = r.layer.saveState();
    ClickLayer other;
    other.prepare (SR, 512);
    other.loadState (state);
    REQUIRE (other.waitForLoads());
    REQUIRE (other.getSampleStatus (0) == ClickLayer::SampleStatus::loaded);
    REQUIRE (other.getSamplePath (0) == wav.getFullPathName());
    REQUIRE (other.getEnvelope (0).size() == 3);

    // missing file: flagged, silent, path kept so the user can relocate it
    wav.deleteFile();
    ClickLayer missing;
    missing.prepare (SR, 512);
    missing.loadState (state);
    REQUIRE (missing.waitForLoads());
    REQUIRE (missing.getSampleStatus (0) == ClickLayer::SampleStatus::missing);
    REQUIRE (missing.getSamplePath (0) == wav.getFullPathName());
    missing.setParam (0, CP::source, (float) srcSample);
    missing.noteOn (36, 1.0f);
    juce::AudioBuffer<float> b (1, 512);
    b.clear();
    missing.process (b, 0, 512);
    REQUIRE (b.getMagnitude (0, 512) == 0.0f);

    // garbage file: failed, not a crash
    auto bad = dir.getChildFile ("bad.wav");
    bad.replaceWithText ("not audio");
    other.loadSample (1, bad);
    REQUIRE (other.waitForLoads());
    REQUIRE (other.getSampleStatus (1) == ClickLayer::SampleStatus::failed);
    dir.deleteRecursively();
}
