#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdio>
#include "PluginProcessor.h"
#include "analysis/KickAnalysis.h"
#include "analysis/KickImport.h"
#include "presets/KickRenderer.h"
#include "synth/SubVoice.h"

using namespace kf;

namespace
{
constexpr double SR = 48000.0;

struct KickSpec
{
    float root = 33.0f, lengthMs = 450.0f, phase = 90.0f, gainDb = -3.0f;
    Curve pitch = defaultPitchCurveSemitones();
    Curve amp = defaultAmpCurve();
    float h1Db = -60.0f, h1DecayMs = 5000.0f; // 2nd harmonic
};

std::vector<float> makeKick (const KickSpec& k, int numSamples)
{
    CurveSlot pitch (k.pitch), amp (k.amp), harm (Curve ({ { 0, 1, 0 }, { 1, 1, 0 } }));
    SubVoice v;
    SubSettings s;
    GlobSettings g;
    g.velSens = 0.0f;
    s.rootNote = k.root;
    s.lengthMs = k.lengthMs;
    s.startPhaseDeg = k.phase;
    s.gainDb = k.gainDb;
    s.harm[0].gainDb = k.h1Db;
    s.harm[0].decayMs = k.h1DecayMs;
    v.prepare (SR, 256);
    v.setCurveSlots (&pitch, &amp, nullptr, &harm, nullptr);
    v.setSettings (s, g);
    v.noteOn ((int) k.root, 1.0f);
    std::vector<float> out;
    while ((int) out.size() < numSamples)
    {
        juce::AudioBuffer<float> b (1, 256);
        b.clear();
        v.process (b, 0, 256);
        for (int i = 0; i < 256; ++i) out.push_back (b.getSample (0, i));
    }
    out.resize ((size_t) numSamples);
    return out;
}

double hzOf (double midi) { return 440.0 * std::exp2 ((midi - 69.0) / 12.0); }

double zcFreq (const std::vector<float>& x, double t0, double t1)
{
    std::vector<double> c;
    for (int i = (int) (t0 * SR) + 1; i < (int) (t1 * SR) && i < (int) x.size(); ++i)
        if (x[(size_t) i - 1] < 0.0f && x[(size_t) i] >= 0.0f)
            c.push_back (i - 1 + (double) -x[(size_t) i - 1] / (double) (x[(size_t) i] - x[(size_t) i - 1]));
    if (c.size() < 2) return 0.0;
    return (double) (c.size() - 1) * SR / (c.back() - c.front());
}

/** RMS envelope in 10 ms windows. */
std::vector<double> rmsEnv (const std::vector<float>& x, int n)
{
    const int w = (int) (0.01 * SR);
    std::vector<double> e;
    for (int a = 0; a + w <= n && a + w <= (int) x.size(); a += w)
    {
        double s = 0;
        for (int i = a; i < a + w; ++i) s += (double) x[(size_t) i] * x[(size_t) i];
        e.push_back (std::sqrt (s / w));
    }
    return e;
}

double correlation (const std::vector<double>& a, const std::vector<double>& b)
{
    const size_t n = std::min (a.size(), b.size());
    double ma = 0, mb = 0;
    for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < n; ++i)
    {
        sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb);
    }
    return sab / std::sqrt (saa * sbb);
}

struct Accuracy { double tailErr, pitchRmsSemis, envCorr, waveCorr; };

Accuracy measure (const KickSpec& k, const std::vector<float>& orig, const analysis::Result& r, int regionOffset = 0)
{
    const int N = std::min ((int) (0.6 * SR), (int) orig.size() - regionOffset);
    // original tail frequency (zero-crossing, last 100 ms before 85 % of the original length)
    const double trueTail = hzOf (k.root);
    const auto re = analysis::renderSub (r, SR, N);
    const double t1 = 0.85 * r.sub.lengthMs * 0.001;
    Accuracy a {};
    a.tailErr = std::abs (zcFreq (re, t1 - 0.12, t1) / trueTail - 1.0);

    // pitch curve error in semitones (absolute pitch trace over the first 80 % of the estimated length)
    double se = 0;
    int cnt = 0;
    const double estLen = r.sub.lengthMs * 0.001, trueLen = k.lengthMs * 0.001;
    for (double t = 0.002; t < 0.8 * std::min (estLen, trueLen); t += 0.001)
    {
        const double tr = k.root + k.pitch.eval (t / trueLen);
        const double es = r.sub.rootNote + r.sub.tuneSemis + r.pitch.eval (t / estLen);
        se += (tr - es) * (tr - es);
        ++cnt;
    }
    a.pitchRmsSemis = std::sqrt (se / cnt);
    if (std::getenv ("KF_DEBUG"))
    {
        std::printf ("  tailHz %.2f startHz %.1f phase %.1f gain %.2f\n", r.tailHz, r.startHz, r.sub.startPhaseDeg, r.sub.gainDb);
        for (auto& p : r.pitch.getPoints()) std::printf ("   P t=%.4f v=%.2f k=%.2f\n", p.time, p.value, p.tension);
        for (auto& p : r.amp.getPoints()) std::printf ("   A t=%.4f v=%.3f k=%.2f\n", p.time, p.value, p.tension);
    }

    std::vector<float> o (orig.begin() + regionOffset, orig.end());
    a.envCorr = correlation (rmsEnv (o, N), rmsEnv (re, N));
    std::vector<double> x, y;
    for (int i = 0; i < N; ++i) { x.push_back (o[(size_t) i]); y.push_back (re[(size_t) i]); }
    a.waveCorr = correlation (x, y);
    std::printf ("  accuracy: tail err %.2f %%, pitch RMS %.3f st, env corr %.4f, waveform corr %.3f | est root %g tune %.2f len %g ms, fit pitch rms %.3f amp corr %.4f\n",
                 a.tailErr * 100.0, a.pitchRmsSemis, a.envCorr, a.waveCorr, r.sub.rootNote, r.sub.tuneSemis,
                 r.sub.lengthMs, r.pitchFitRmsSemis, r.ampFitCorrelation);
    return a;
}
}

TEST_CASE ("Import: fitBreakpoints reproduces an exponential decay and a pitch drop")
{
    std::vector<double> t, y;
    for (int i = 0; i <= 400; ++i) { t.push_back (i / 400.0); y.push_back (std::exp (-5.0 * i / 400.0)); }
    const auto c = analysis::fitBreakpoints (t, y, 0.03, 10, 0.1);
    REQUIRE (c.size() <= 10);
    double worst = 0;
    for (size_t i = 0; i < t.size(); ++i) worst = std::max (worst, std::abs (c.eval (t[i]) - y[i]));
    REQUIRE (worst < 0.06);
}

TEST_CASE ("Import: single kick, known curves are recovered (P-06/P-07)")
{
    struct Case { const char* name; KickSpec k; };
    KickSpec a; // defaults: A1 55 Hz, +31 st drop
    KickSpec b;
    b.root = 38; b.lengthMs = 600; b.phase = 0; b.gainDb = -6;
    b.pitch = Curve ({ { 0.0, 24.0, -0.5 }, { 0.15, 3.0, -0.3 }, { 0.4, 0.0, 0.0 }, { 1.0, 0.0, 0.0 } });
    b.amp = Curve ({ { 0.0, 1.0, -0.4 }, { 0.5, 0.35, -0.3 }, { 1.0, 0.0, 0.0 } });
    KickSpec c;
    c.root = 29; c.lengthMs = 800; c.phase = 200; c.gainDb = -2;
    c.pitch = Curve ({ { 0.0, 18.0, -0.6 }, { 0.1, 2.0, -0.2 }, { 1.0, 0.0, 0.0 } });
    c.amp = Curve ({ { 0.0, 1.0, 0.2 }, { 0.2, 0.8, -0.5 }, { 1.0, 0.0, 0.0 } });
    for (const auto& k : { a, b, c })
    {
        INFO ("root " << k.root);
        const auto kick = makeKick (k, (int) (1.0 * SR));
        if (std::getenv ("KF_DEBUG"))
        {
            auto e = rmsEnv (kick, (int) SR);
            for (size_t i = 0; i < e.size(); i += 5) std::printf ("   kick env[%zu ms]=%.3f\n", i * 10, e[i]);
        }
        const auto r = analysis::analyse (kick.data(), (int) kick.size(), SR);
        REQUIRE (r.ok);
        const auto acc = measure (k, kick, r);
        REQUIRE (acc.tailErr < 0.03);
        REQUIRE (acc.pitchRmsSemis < 1.0);
        REQUIRE (acc.envCorr > 0.9);
        REQUIRE (r.sub.lengthMs > 0.5 * k.lengthMs);
        REQUIRE (r.pitch.size() <= 16);
        // sub only: nothing much left for the top end
        double eo = 0, er = 0;
        for (float v : kick) eo += (double) v * v;
        for (float v : r.residual) er += (double) v * v;
        std::printf ("  residual energy / original energy: %.4f\n", er / eo);
        REQUIRE (er < 0.03 * eo);
    }
}

TEST_CASE ("Import: top end ends up in the residual, sub is removed from it")
{
    KickSpec k;
    auto kick = makeKick (k, (int) (1.0 * SR));
    juce::Random rnd (7);
    std::vector<float> click ((size_t) (0.012 * SR));
    float lp = 0;
    for (size_t i = 0; i < click.size(); ++i) // 1st-order high-passed noise, 3 ms decay
    {
        const float n = rnd.nextFloat() * 2.0f - 1.0f;
        const float hp = n - lp;
        lp += 0.5f * (n - lp);
        click[i] = 0.4f * hp * std::exp (-(float) i / (float) (0.003 * SR));
        kick[i + (size_t) (0.001 * SR)] += click[i];
    }
    const auto r = analysis::analyse (kick.data(), (int) kick.size(), SR);
    REQUIRE (r.ok);
    REQUIRE (! r.residual.empty());
    // correlation of residual with the injected click (offset 1 ms; analysis region starts ~ at the hit)
    double sxy = 0, sxx = 0, syy = 0;
    const int off = (int) (0.001 * SR) - r.regionStart; // region start is the hit start, clicks sit after it
    for (size_t i = 0; i < click.size(); ++i)
    {
        const int j = (int) i + (int) (0.001 * SR) - r.regionStart;
        if (j < 0 || j >= (int) r.residual.size()) continue;
        sxy += click[i] * r.residual[(size_t) j]; sxx += click[i] * click[i]; syy += r.residual[(size_t) j] * r.residual[(size_t) j];
    }
    (void) off;
    const double corr = sxy / std::sqrt (sxx * syy);
    std::printf ("  residual/click correlation %.3f, region start %d\n", corr, r.regionStart);
    REQUIRE (corr > 0.7);
    // little low-frequency content left in the residual after the first 20 ms
    double e = 0;
    for (size_t i = (size_t) (0.05 * SR); i < r.residual.size(); ++i) e += r.residual[i] * r.residual[i];
    REQUIRE (std::sqrt (e / (double) r.residual.size()) < 0.005);
}

TEST_CASE ("Import: a loop with 4 hits - strongest hit is picked, region can be chosen (P-08)")
{
    KickSpec k;
    const auto one = makeKick (k, (int) (0.5 * SR));
    const float amps[4] = { 0.5f, 1.0f, 0.7f, 0.6f };
    std::vector<float> loop ((size_t) (2.0 * SR), 0.0f);
    for (int h = 0; h < 4; ++h)
        for (size_t i = 0; i < one.size(); ++i)
            loop[(size_t) (h * 0.5 * SR) + i] += one[i] * amps[h];

    const auto hits = analysis::detectHits (loop.data(), (int) loop.size(), SR);
    REQUIRE (hits.size() == 4);
    for (int h = 0; h < 4; ++h)
        REQUIRE (std::abs (hits[(size_t) h].start - (int) (h * 0.5 * SR)) < (int) (0.01 * SR));
    REQUIRE (analysis::strongestHit (hits) == 1);

    const auto r = analysis::analyse (loop.data(), (int) loop.size(), SR);
    REQUIRE (r.ok);
    REQUIRE (std::abs (r.regionStart - (int) (0.5 * SR)) < (int) (0.01 * SR));
    REQUIRE (r.regionEnd <= (int) (1.0 * SR));
    const std::vector<float> ref (loop.begin() + (long) (0.5 * SR), loop.begin() + (long) (1.0 * SR));
    const auto acc = measure (k, ref, r, 0);
    REQUIRE (acc.tailErr < 0.03);
    REQUIRE (acc.envCorr > 0.9);

    // explicit region on the 4th hit
    const auto r4 = analysis::analyse (loop.data(), (int) loop.size(), SR, hits[3].start, hits[3].end);
    REQUIRE (r4.ok);
    REQUIRE (r4.regionStart == hits[3].start);
    REQUIRE (std::abs (r4.tailHz / hzOf (k.root) - 1.0) < 0.03);
    REQUIRE (r4.sub.gainDb < r.sub.gainDb); // 0.6 < 1.0
}

TEST_CASE ("Import: second harmonic level and decay are estimated")
{
    KickSpec k;
    k.h1Db = -9.0f;
    k.h1DecayMs = 250.0f;
    const auto kick = makeKick (k, (int) (1.0 * SR));
    const auto r = analysis::analyse (kick.data(), (int) kick.size(), SR);
    REQUIRE (r.ok);
    REQUIRE (r.numHarmonicsUsed >= 1);
    std::printf ("  harmonic 2: gain %.1f dB (true -9), decay %.0f ms (true 250)\n", r.sub.harm[0].gainDb, r.sub.harm[0].decayMs);
    REQUIRE (std::abs (r.sub.harm[0].gainDb - (-9.0f)) < 3.5f);
    REQUIRE (r.sub.harm[0].decayMs > 120.0f);
    REQUIRE (r.sub.harm[0].decayMs < 600.0f);
}

TEST_CASE ("Import: applies to the processor; swapping the top end leaves the sub alone (C-10)")
{
    juce::ScopedJuceInitialiser_GUI init;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("kf_import_test_" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    dir.createDirectory();

    KickSpec k;
    auto kick = makeKick (k, (int) (1.0 * SR));
    juce::Random rnd (3);
    for (int i = 0; i < (int) (0.01 * SR); ++i)
        kick[(size_t) i + 48] += 0.3f * (rnd.nextFloat() * 2 - 1) * std::exp (-(float) i / (float) (0.002 * SR));
    const auto src = dir.getChildFile ("kick.wav");
    juce::AudioBuffer<float> b (1, (int) kick.size());
    b.copyFrom (0, 0, kick.data(), (int) kick.size());
    REQUIRE (kf::writeWav24 (src, b, SR));

    ImportOptions opt;
    opt.importsDir = dir.getChildFile ("imports");
    const auto sum = analyseFile (src, opt);
    REQUIRE (sum.ok);
    REQUIRE (sum.topEndFile.existsAsFile());
    REQUIRE (sum.topEndFile.getParentDirectory() == opt.importsDir);

    KickforgeProcessor proc;
    proc.setPlayConfigDetails (0, 2, SR, 256);
    proc.prepareToPlay (SR, 256);
    applyImport (proc, sum, opt);
    REQUIRE (proc.getClickLayer().waitForLoads (10000));
    CHECK (std::abs (proc.apvts.getRawParameterValue ("sub_root")->load() - sum.result.sub.rootNote) < 0.01f);
    CHECK (std::abs (proc.apvts.getRawParameterValue ("sub_length")->load() - sum.result.sub.lengthMs) < 1.0f);
    CHECK (proc.getClickLayer().getSampleStatus (0) == ClickLayer::SampleStatus::loaded);
    CHECK (proc.getSubCurves().getCurve (SubCurveId::Pitch) == sum.result.pitch);
    CHECK (proc.getSubCurves().getCurve (SubCurveId::Amp) == sum.result.amp);

    // C-10: swap the top end; every sub_* parameter and the sub curves stay bit-identical
    std::vector<std::pair<juce::String, float>> before;
    for (auto* p : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->paramID.startsWith ("sub_")) before.push_back ({ rp->paramID, rp->getValue() });
    REQUIRE (before.size() > 20);
    const auto pitchBefore = proc.getSubCurves().getCurve (SubCurveId::Pitch);
    const auto ampBefore = proc.getSubCurves().getCurve (SubCurveId::Amp);

    const auto other = dir.getChildFile ("other_top.wav");
    juce::AudioBuffer<float> nb (1, 2000);
    for (int i = 0; i < 2000; ++i) nb.setSample (0, i, 0.5f * std::sin (0.9f * (float) i) * std::exp (-(float) i / 300.0f));
    REQUIRE (kf::writeWav24 (other, nb, SR));
    replaceTopEndSample (proc, other, 0);
    REQUIRE (proc.getClickLayer().waitForLoads (10000));
    CHECK (proc.getClickLayer().getSamplePath (0) == other.getFullPathName());
    for (auto& [id, v] : before)
        CHECK (proc.apvts.getParameter (id)->getValue() == v);
    CHECK (proc.getSubCurves().getCurve (SubCurveId::Pitch) == pitchBefore);
    CHECK (proc.getSubCurves().getCurve (SubCurveId::Amp) == ampBefore);

    dir.deleteRecursively();
}
