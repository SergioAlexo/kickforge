#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/Analyser.h"
#include "ui/CurveGeometry.h"
#include "ui/EqResponse.h"
#include "ui/FxLabels.h"
#include "ui/Readout.h"

using namespace kf;
using namespace kf::ui;
using Catch::Matchers::WithinAbs;

namespace
{
CurveView makeView (bool log = false)
{
    CurveView v;
    v.x = 10; v.y = 20; v.w = 400; v.h = 200;
    v.t0 = 0; v.t1 = 1; v.vBottom = 0; v.vTop = 100; v.logTime = log;
    return v;
}
}

TEST_CASE ("CurveView mapping round-trips (linear and log)")
{
    for (bool log : { false, true })
    {
        auto v = makeView (log);
        for (double t : { 0.0, 0.01, 0.3, 0.77, 1.0 })
            REQUIRE_THAT (v.xToTime (v.timeToX (t)), WithinAbs (t, 1e-4));
        for (double val : { 0.0, 12.5, 100.0 })
            REQUIRE_THAT (v.yToValue (v.valueToY (val)), WithinAbs (val, 1e-3));
    }
    auto v = makeView (true);
    REQUIRE (v.timeToX (0.1) > makeView (false).timeToX (0.1)); // log zooms into the attack
}

TEST_CASE ("niceStep and snapping")
{
    REQUIRE_THAT (niceStep (1.0, 8), WithinAbs (0.2, 1e-9));
    REQUIRE_THAT (niceStep (500.0, 8), WithinAbs (100.0, 1e-9));
    REQUIRE_THAT (snapToStep (0.37, 0.1), WithinAbs (0.4, 1e-9));
    // S-12: semitone-mode pitch snap lands on whole notes (root 33, tune 0)
    REQUIRE_THAT (snapPitchValue (6.4, false, 33, 0, 0, 440), WithinAbs (6.0, 1e-9));
    REQUIRE_THAT (snapPitchValue (6.6, false, 33, 0, 0, 440), WithinAbs (7.0, 1e-9));
    // with a fractional tune the snapped ABSOLUTE pitch is still a whole note
    const double v = snapPitchValue (6.4, false, 33, 0.3, 0, 440);
    REQUIRE_THAT (std::fmod (33 + 0.3 + v, 1.0), WithinAbs (0.0, 1e-9));
    // Hz mode: 56 Hz -> A1 = 55 Hz
    REQUIRE_THAT (snapPitchValue (56.0, true, 33, 0, 0, 440), WithinAbs (55.0, 1e-6));
}

TEST_CASE ("CurveEditor hit testing")
{
    Curve c ({ { 0.0, 100.0, 0.0 }, { 0.5, 50.0, 0.0 }, { 1.0, 0.0, 0.0 } });
    auto v = makeView();
    const float px = v.timeToX (0.5), py = v.valueToY (50.0);
    REQUIRE (hitPoint (c, v, px + 3, py - 2) == 1);
    REQUIRE (hitPoint (c, v, px + 30, py) == -1);
    REQUIRE (hitPoint (c, v, v.timeToX (0.0), v.valueToY (100.0)) == 0);
    // on the first segment, between points
    const float sx = v.timeToX (0.25), sy = v.valueToY (c.eval (0.25));
    REQUIRE (hitSegment (c, v, sx, sy + 2) == 0);
    REQUIRE (hitSegment (c, v, sx, sy + 40) == -1);
    REQUIRE (hitSegment (c, v, px, py) == -1); // on a point: point wins
    REQUIRE (hitSegment (c, v, v.timeToX (0.75), v.valueToY (c.eval (0.75))) == 1);
}

TEST_CASE ("Segment tension follows the cursor")
{
    for (double u : { 0.3, 0.5, 0.7 })
        for (double s : { 0.1, 0.4, 0.8 })
        {
            const double t = tensionForTarget (u, s);
            REQUIRE_THAT (Curve::shape (u, t), WithinAbs (s, 0.01));
        }
    Curve c ({ { 0.0, 10.0, 0.0 }, { 1.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 } });
    REQUIRE_THAT (clampPointTime (c, 1, 5.0), WithinAbs (2.0, 1e-3)); // limited by the next point
    Curve d ({ { 0.0, 1.0, 0.0 }, { 0.4, 1.0, 0.0 }, { 1.0, 0.0, 0.0 } });
    REQUIRE (clampPointTime (d, 1, 0.9) < 1.0);
    REQUIRE_THAT (clampPointTime (d, 0, 0.5), WithinAbs (0.0, 1e-9));
}

TEST_CASE ("Fx labels and EQ response")
{
    REQUIRE (fxKnobLabels (0).a == nullptr);
    REQUIRE (std::string (fxKnobLabels (10).a) == "Time"); // Delay
    REQUIRE (std::string (fxKnobLabels (10).mix) == "Send");
    REQUIRE (fxKnobLabels (6).c == nullptr); // BitCrush has no third control
    EqSettings s;
    s.p1F = 250; s.p1G = 9; s.p1Q = 1;
    REQUIRE_THAT (eqResponseDb (s, 250.0, 48000.0), WithinAbs (9.0, 0.1));
    REQUIRE_THAT (eqResponseDb (EqSettings {}, 1000.0, 48000.0), WithinAbs (0.0, 1e-6));
    EqSettings hs; hs.hsG = -12; hs.hsF = 6000;
    REQUIRE (eqResponseDb (hs, 18000.0, 48000.0) < -9.0);
    EqSettings lc; lc.lowCut = 200;
    REQUIRE (eqResponseDb (lc, 40.0, 48000.0) < -20.0);
}

TEST_CASE ("AudioFifo + Analyser: spectrum peak and kick capture")
{
    AudioFifo fifo (1 << 14);
    std::vector<float> in (3000);
    for (size_t i = 0; i < in.size(); ++i) in[i] = (float) i;
    fifo.push (in.data(), (int) in.size());
    std::vector<float> out (4000);
    REQUIRE (fifo.pull (out.data(), 4000) == 3000);
    REQUIRE (out[2999] == 2999.0f);

    Analyser a;
    a.setSampleRate (48000.0);
    // silence, then a 100 Hz decaying sine
    std::vector<float> sig (48000, 0.0f);
    for (int i = 4800; i < 48000; ++i)
        sig[(size_t) i] = 0.8f * std::exp (-(i - 4800) / 20000.0f) * std::sin (2.0f * juce::MathConstants<float>::pi * 100.0f * (i - 4800) / 48000.0f);
    // feed in blocks like the FIFO would
    for (size_t p = 0; p < 4800 + 4096; p += 512) a.feed (sig.data() + p, 512);
    const auto& sp = a.getSpectrumDb();
    int peakBin = 0;
    for (int i = 1; i < Analyser::numBins; ++i) if (sp[(size_t) i] > sp[(size_t) peakBin]) peakBin = i;
    REQUIRE_THAT (a.binToHz (peakBin), WithinAbs (100.0, 25.0));
    REQUIRE (sp[(size_t) peakBin] > -20.0f);
    REQUIRE (! a.getKick().empty()); // onset detected after the silence
    REQUIRE (a.getKickVersion() > 0);
}

TEST_CASE ("Editor constructs and paints offscreen at several sizes")
{
    juce::ScopedJuceInitialiser_GUI gui;
    KickforgeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    REQUIRE (ed != nullptr);
    auto* ke = dynamic_cast<KickforgeEditor*> (ed.get());
    REQUIRE (ke != nullptr);

    for (int w : { 560, 1120, 1680 })
    {
        ed->setSize (w, (int) std::lround (w * (double) KickforgeEditor::designH / KickforgeEditor::designW));
        for (int tab = 0; tab < 6; ++tab)
        {
            ke->selectTab (tab);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 0.5f);
            REQUIRE (img.isValid());
        }
    }
    for (int c = 0; c < 8; ++c) ke->selectCurve (c);

    // curve editing goes through the thread-safe setters
    ke->selectCurve (1);
    auto& ce = ke->getCurveEditor();
    const int before = ce.getCurve().size();
    ce.addPointAt (ce.view.timeToX (0.5), ce.view.valueToY (0.4));
    REQUIRE (ce.getCurve().size() == before + 1);
    REQUIRE (proc.getSubCurves().getCurve (SubCurveId::Amp).size() == before + 1);
    ce.undo();
    REQUIRE (proc.getSubCurves().getCurve (SubCurveId::Amp).size() == before);

    // optional snapshot for visual checks
    if (auto dir = juce::SystemStats::getEnvironmentVariable ("KF_UI_SNAPSHOT_DIR", {}); dir.isNotEmpty())
    {
        ed->setSize (KickforgeEditor::designW, KickforgeEditor::designH);
        const char* names[] = { "sub", "click", "fx", "eq", "dyn", "global" };
        for (int tab = 0; tab < 6; ++tab)
        {
            ke->selectTab (tab);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f (dir);
            f = f.getChildFile (juce::String ("ui_") + names[tab] + ".png");
            juce::FileOutputStream os (f);
            if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
        }
    }
}
