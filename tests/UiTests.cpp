#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <set>
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
        for (int tab = 0; tab < 7; ++tab)
        {
            ke->selectTab (tab);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), false, 0.5f);
            REQUIRE (img.isValid());
        }
    }
    for (int c = 0; c < 9; ++c) ke->selectCurve (c);

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
        const char* names[] = { "sub", "harmonics", "click", "fx", "eq", "dyn", "global" };
        for (int tab = 0; tab < 7; ++tab)
        {
            ke->selectTab (tab);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f (dir);
            f = f.getChildFile (juce::String ("ui_") + names[tab] + ".png");
            juce::FileOutputStream os (f);
            if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
        }
        for (int c : { 0, 2, 3, 4 })
        {
            ke->selectCurve (c);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f = juce::File (dir).getChildFile ("ui_curve" + juce::String (c) + ".png");
            juce::FileOutputStream os (f);
            if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
        }
    }
}

namespace
{
void collectBoundNames (juce::Component& c, std::set<juce::String>& out)
{
    if (dynamic_cast<juce::Slider*> (&c) || dynamic_cast<juce::ComboBox*> (&c) || dynamic_cast<juce::ToggleButton*> (&c))
        out.insert (c.getName());
    for (auto* ch : c.getChildren())
        collectBoundNames (*ch, out);
}
}

TEST_CASE ("Every non-hidden parameter is bound by an editor control")
{
    juce::ScopedJuceInitialiser_GUI gui;
    KickforgeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    std::set<juce::String> bound;
    collectBoundNames (*ed, bound);

    std::vector<juce::String> missing;
    for (auto* p : proc.getParameters())
        if (auto* wp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! wp->isMetaParameter() && wp->isAutomatable() && bound.count (wp->getParameterID()) == 0)
                missing.push_back (wp->getParameterID());
    INFO ("unbound: " << juce::StringArray (missing.data(), (int) missing.size()).joinIntoString (", ").toStdString());
    REQUIRE (missing.empty());
}

TEST_CASE ("Default sub curves are smooth: no near-vertical drops")
{
    for (auto id : { SubCurveId::Pitch, SubCurveId::Amp, SubCurveId::Drive, SubCurveId::HarmAmount })
    {
        const auto& pts = (id == SubCurveId::Pitch ? defaultPitchCurveSemitones()
                         : id == SubCurveId::Amp ? defaultAmpCurve()
                         : id == SubCurveId::Drive ? defaultDriveCurve() : defaultHarmAmountCurve()).getPoints();
        for (size_t i = 1; i < pts.size(); ++i)
            REQUIRE (pts[i].time - pts[i - 1].time >= 0.009);
    }
    const auto a = defaultAmpCurve();
    // no step larger than 0.1 of full scale within any 2% of the length after the hold
    for (double t = 0.1; t < 0.98; t += 0.01)
        REQUIRE (std::abs (a.eval (t) - a.eval (t + 0.02)) < 0.15);
    REQUIRE (a.eval (0.05) == 1.0);
}

TEST_CASE ("Curve editor axis labels stay inside the component at all widths and zooms")
{
    juce::ScopedJuceInitialiser_GUI gui;
    CurveEditor ce;
    CurveEditor::Source s;
    s.name = "T";
    s.get = [] { return Curve ({ { 0.0, 0.0, 0.0 }, { 1.0, 31.0, 0.0 } }); };
    s.set = [] (const Curve&) {};
    s.lengthMs = [] { return 500.0; };
    s.vmin = -12; s.vmax = 48;
    ce.setSource (s);
    for (int w : { 300, 500, 736, 1200 })
        for (auto zoom : { std::pair<double, double> { 0.0, 1.0 }, { 0.0, 0.3 }, { 0.6, 1.0 }, { 0.9, 1.0 }, { 0.2, 0.25 } })
        {
            ce.setBounds (0, 0, w, 280);
            ce.view.t0 = zoom.first; ce.view.t1 = zoom.second;
            const auto bounds = ce.getLocalBounds();
            auto labels = ce.timeLabels();
            REQUIRE (! labels.empty());
            for (auto& l : labels) REQUIRE (bounds.contains (l.rect));
            for (auto& l : ce.valueLabels()) REQUIRE (bounds.contains (l.rect));
            if (zoom.second == 1.0 && zoom.first == 0.0)
                REQUIRE (labels.back().text == "500 ms");
        }
}

TEST_CASE ("Harmonic gain knob range: off at the minimum, -12 dB mid, 0 dB to the right")
{
    juce::ScopedJuceInitialiser_GUI gui;
    KickforgeProcessor proc;
    for (int i = 1; i <= SubSettings::kNumHarmonics; ++i)
    {
        auto* p = proc.apvts.getParameter ("sub_h" + juce::String (i) + "_gain");
        REQUIRE (p != nullptr);
        REQUIRE_THAT (p->convertTo0to1 (-60.0f), WithinAbs (0.0, 1e-6));
        REQUIRE_THAT (p->convertFrom0to1 (0.0f), WithinAbs (-60.0, 1e-4));
        REQUIRE_THAT (p->convertFrom0to1 (0.5f), WithinAbs (-12.0, 0.5));
        REQUIRE (p->convertTo0to1 (0.0f) > 0.7f);
        REQUIRE (p->convertTo0to1 (0.0f) < 0.95f);
        REQUIRE (p->getText (0.0f, 32) == "Off");
        REQUIRE (p->getText (p->convertTo0to1 (-6.0f), 32).contains ("-6"));
        REQUIRE_THAT (p->getValueForText ("Off"), WithinAbs (0.0, 1e-6));
    }
    // the editor's knob shows Off at the minimum and fills from the left (not bipolar)
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    std::function<juce::Slider*(juce::Component&)> find = [&] (juce::Component& c) -> juce::Slider* {
        if (auto* s = dynamic_cast<juce::Slider*> (&c); s != nullptr && s->getName() == "sub_h1_gain") return s;
        for (auto* ch : c.getChildren()) if (auto* r = find (*ch)) return r;
        return nullptr;
    };
    auto* s = find (*ed);
    REQUIRE (s != nullptr);
    REQUIRE (s->getTextFromValue (-60.0) == "Off");
    REQUIRE ((bool) s->getProperties()["unipolar"]);
}

TEST_CASE ("Selecting the pitch curve fits the view to its value range")
{
    juce::ScopedJuceInitialiser_GUI gui;
    KickforgeProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* ke = dynamic_cast<KickforgeEditor*> (ed.get());
    REQUIRE (ke != nullptr);
    ke->selectCurve (1);
    ke->selectCurve (0);
    auto& ce = ke->getCurveEditor();
    double lo = 1e9, hi = -1e9;
    for (auto& p : ce.getCurve().getPoints()) { lo = std::min (lo, p.value); hi = std::max (hi, p.value); }
    REQUIRE (hi - lo > 10.0);
    REQUIRE (ce.view.vBottom <= lo);
    REQUIRE (ce.view.vTop >= hi);
    REQUIRE ((ce.view.vTop - ce.view.vBottom) < (hi - lo) * 1.35); // tight, not the old -12..48
    // an externally loaded (very different) curve refits on the next timer tick path: Fit does the same
    proc.getSubCurves().setCurve (SubCurveId::Pitch, Curve ({ { 0.0, 60.0, 0.0 }, { 1.0, 0.0, 0.0 } }));
    ke->selectCurve (0); // re-select = reload from the processor
    REQUIRE (ce.view.vTop >= 60.0);
    // zooming out stays possible: view limits are not clamped
    ce.view.vBottom -= 100;
    REQUIRE (ce.view.vBottom < -90);
}
