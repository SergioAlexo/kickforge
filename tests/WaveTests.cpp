#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_approx.hpp>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "presets/KickRenderer.h"
#include "ui/WaveLayers.h"

using Catch::Matchers::WithinAbs;

namespace
{
void setP (KickforgeProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    REQUIRE (prm != nullptr);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

int zeroCrossings (const std::vector<float>& v, int from, int to)
{
    int n = 0;
    for (int i = std::max (1, from); i < std::min ((int) v.size(), to); ++i)
        if ((v[(size_t) i - 1] < 0.0f) != (v[(size_t) i] < 0.0f)) ++n;
    return n;
}

juce::File synthRefWav (const juce::File& dir)
{
    auto f = dir.getChildFile ("ref.wav");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), 44100.0, 1, 16, {}, 0));
    REQUIRE (w != nullptr);
    os.release();
    const int n = 44100;
    juce::AudioBuffer<float> b (1, n);
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / 44100.0;
        ph += juce::MathConstants<double>::twoPi * (50.0 + 140.0 * std::exp (-t * 30.0)) / 44100.0;
        b.setSample (0, i, (float) (0.8 * std::sin (ph) * std::exp (-t * 6.0)));
    }
    w->writeFromAudioSampleBuffer (b, 0, n);
    return f;
}
}

TEST_CASE ("wave: per-layer buffers sum to the pre-FX mix", "[wave]")
{
    KickforgeProcessor live;
    setP (live, kf::clickParamId (0, kf::CP::source).toRawUTF8(), 2.0f); // noise burst
    kf::LayerScratch sc;
    sc.sync (live);
    auto r = sc.render (0.4);
    REQUIRE (r.mix.size() == r.layer[0].size());
    REQUIRE (r.preFx.size() == r.layer[0].size());
    double err = 0.0, ref = 0.0, clickE = 0.0;
    for (size_t i = 0; i < r.preFx.size(); ++i)
    {
        float sum = 0.0f;
        for (auto& l : r.layer) sum += l[i];
        err = std::max (err, (double) std::abs (sum - r.preFx[i]));
        ref = std::max (ref, (double) std::abs (r.preFx[i]));
        clickE += std::abs (r.layer[1][i]);
    }
    REQUIRE (ref > 0.05);
    REQUIRE (err < 1.0e-4 * std::max (1.0, ref));
    REQUIRE (clickE > 0.0);
    float mixPeak = 0.0f;
    for (float s : r.mix) mixPeak = std::max (mixPeak, std::abs (s));
    REQUIRE (mixPeak > 0.05f);
}

TEST_CASE ("wave: sub_tune and pitch curve change the sub zero-crossing spacing", "[wave]")
{
    KickforgeProcessor live;
    kf::LayerScratch sc;
    auto zc = [&]
    {
        sc.sync (live);
        auto r = sc.render (0.3);
        return zeroCrossings (r.layer[0], 0, (int) (0.25 * r.sampleRate));
    };
    const int base = zc();
    REQUIRE (base > 10);
    setP (live, "sub_tune", 12.0f);
    const int up = zc();
    REQUIRE (up > (int) (base * 1.6));
    setP (live, "sub_tune", 0.0f);
    REQUIRE (zc() == base);

    auto c = live.getSubCurves().getCurve (kf::SubCurveId::Pitch);
    auto pts = c.getPoints();
    for (auto& p : pts) p.value += 12.0; // an octave up everywhere
    live.getSubCurves().setCurve (kf::SubCurveId::Pitch, kf::Curve (pts));
    REQUIRE (zc() > (int) (base * 1.6));
}

TEST_CASE ("wave: amp curve shrinks the body", "[wave]")
{
    KickforgeProcessor live;
    kf::LayerScratch sc;
    sc.sync (live);
    auto a = sc.render (0.4);
    live.getSubCurves().setCurve (kf::SubCurveId::Amp, kf::Curve ({ { 0.0, 1.0, 0.0 }, { 0.05, 0.0, 0.0 }, { 1.0, 0.0, 0.0 } }));
    sc.sync (live);
    auto b = sc.render (0.4);
    auto energy = [] (const std::vector<float>& v, int from)
    {
        double e = 0;
        for (int i = from; i < (int) v.size(); ++i) e += v[(size_t) i] * v[(size_t) i];
        return e;
    };
    REQUIRE (energy (b.layer[0], 8000) < 0.05 * energy (a.layer[0], 8000));
}

TEST_CASE ("wave: requests coalesce and the final state is rendered", "[wave]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    KickforgeProcessor live;
    kf::ui::WaveService ws (live);
    REQUIRE (ws.waitIdle());
    const auto before = ws.getRenderCount();
    for (int i = 0; i < 400; ++i)
    {
        setP (live, "sub_tune", (float) (i % 7));
        ws.requestRender();
    }
    setP (live, "sub_tune", 12.0f);
    ws.requestRender();
    REQUIRE (ws.waitIdle());
    const auto n = ws.getRenderCount() - before;
    REQUIRE (n >= 1);
    REQUIRE (n < 40);
    auto res = ws.latest();
    REQUIRE (res != nullptr);
    const int zcFinal = zeroCrossings (res->buffers.layer[0], 0, (int) (0.25 * res->buffers.sampleRate));
    kf::LayerScratch sc;
    sc.sync (live);
    auto direct = sc.render (0.3);
    const int zcDirect = zeroCrossings (direct.layer[0], 0, (int) (0.25 * direct.sampleRate));
    REQUIRE (std::abs (zcFinal - zcDirect) <= 1);
}

TEST_CASE ("wave: render time per update", "[wave]")
{
    KickforgeProcessor live;
    setP (live, kf::clickParamId (0, kf::CP::source).toRawUTF8(), 2.0f);
    kf::LayerScratch sc;
    sc.sync (live);
    sc.render (0.5);
    double tot = 0.0;
    const int N = 20;
    for (int i = 0; i < N; ++i)
    {
        setP (live, "sub_tune", (float) (i % 5));
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        sc.sync (live);
        sc.render (0.54);
        tot += juce::Time::getMillisecondCounterHiRes() - t0;
    }
    WARN ("avg sync+render ms (0.54 s of audio): " << tot / N);
    REQUIRE (tot / N < 40.0);
}

TEST_CASE ("wave: min/max column mapping under zoom and log time", "[wave]")
{
    std::vector<float> d (1000, 0.0f);
    d[500] = 1.0f;
    d[10] = -1.0f;
    kf::ui::CurveView v;
    v.x = 0; v.w = 100;
    const double sr = 1000.0, lenMs = 1000.0; // 1 sample == 1 ms
    float lo, hi;
    REQUIRE (kf::ui::waveColumn (d.data(), 1000, sr, v, lenMs, 50, lo, hi));
    REQUIRE_THAT (hi, WithinAbs (1.0, 1e-6));
    REQUIRE (kf::ui::waveColumn (d.data(), 1000, sr, v, lenMs, 1, lo, hi));
    REQUIRE_THAT (lo, WithinAbs (-1.0, 1e-6));
    REQUIRE (kf::ui::waveColumn (d.data(), 1000, sr, v, lenMs, 30, lo, hi));
    REQUIRE (hi == 0.0f);
    REQUIRE (lo == 0.0f);
    // zoomed to t 0.49..0.51 (20 samples over 100 px)
    v.t0 = 0.49; v.t1 = 0.51;
    REQUIRE (kf::ui::waveColumn (d.data(), 1000, sr, v, lenMs, 49, lo, hi));
    REQUIRE_THAT (hi, WithinAbs (1.0, 1e-6));
    REQUIRE (kf::ui::waveColumn (d.data(), 1000, sr, v, lenMs, 10, lo, hi));
    REQUIRE (hi == 0.0f);
    // beyond the data
    v.t0 = 1.5; v.t1 = 2.0;
    REQUIRE_FALSE (kf::ui::waveColumn (d.data(), 1000, sr, v, lenMs, 10, lo, hi));
    // log time: the attack gets the pixels
    v.t0 = 0.0; v.t1 = 1.0; v.logTime = true;
    bool found = false;
    for (int px = 0; px < 100 && ! found; ++px)
        if (kf::ui::waveColumn (d.data(), 1000, sr, v, lenMs, px, lo, hi) && lo <= -0.99f) found = true;
    REQUIRE (found);
    REQUIRE (v.xToTime (v.timeToX (0.01)) == Catch::Approx (0.01).margin (1e-6));
    REQUIRE (v.timeToX (0.01) < 25.0f); // linear would be 1
}

TEST_CASE ("wave: editor paints with all layers on at several sizes; toggles persist", "[wave]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kf-wave-" + juce::Uuid().toString());
    dir.createDirectory();
    auto wav = synthRefWav (dir);

    KickforgeProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    setP (p, kf::clickParamId (0, kf::CP::source).toRawUTF8(), 2.0f);
    REQUIRE (p.getReference().load (wav));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* ke = dynamic_cast<KickforgeEditor*> (ed.get());
    REQUIRE (ke != nullptr);
    auto& ce = ke->getCurveEditor();
    for (int i = 0; i < 4; ++i) ce.getLayerButton (i).setToggleState (true, juce::sendNotification);
    REQUIRE (p.getWaveSettings().mix.load());
    ke->getWaveService().requestRender();
    REQUIRE (ke->getWaveService().waitIdle());
    ke->pumpForTest();
    REQUIRE (ce.getLayerButton (2).isEnabled());

    for (int w : { 560, 1120, 2240 })
    {
        ed->setSize (w, (int) std::lround (w * (double) KickforgeEditor::designH / KickforgeEditor::designW));
        for (int c : { 0, 1, 2, 5 })
        {
            ke->selectCurve (c);
            REQUIRE (ed->createComponentSnapshot (ed->getLocalBounds(), false, 0.5f).isValid());
        }
    }

    // persisted as UI state, not a parameter
    p.getWaveSettings().clicks = false;
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    KickforgeProcessor q;
    q.setStateInformation (mb.getData(), (int) mb.getSize());
    REQUIRE_FALSE (q.getWaveSettings().clicks.load());
    REQUIRE (q.getWaveSettings().mix.load());

    if (auto out = juce::SystemStats::getEnvironmentVariable ("KF_UI_SNAPSHOT_DIR", {}); out.isNotEmpty())
    {
        p.getWaveSettings().clicks = true;
        ed->setSize (KickforgeEditor::designW, KickforgeEditor::designH);
        auto& ws = ke->getWaveService();
        auto snap = [&] (int curve, const char* name)
        {
            ke->selectCurve (curve);
            ws.requestRender();
            ws.waitIdle();
            ke->pumpForTest();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream os (juce::File (out).getChildFile (name));
            if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
        };
        snap (0, "wave_pitch.png");
        snap (1, "wave_amp.png");
        snap (5, "wave_click1.png");
    }
    ed.reset();
    dir.deleteRecursively();
}
