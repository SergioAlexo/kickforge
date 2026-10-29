#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <set>
#include "PluginProcessor.h"
#include "presets/PresetManager.h"
#include "presets/KickRenderer.h"

namespace
{
struct TempDir
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("kf_preset_test_" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    TempDir() { dir.createDirectory(); }
    ~TempDir() { dir.deleteRecursively(); }
};

float peakOf (const juce::AudioBuffer<float>& b)
{
    float p = 0;
    for (int i = 0; i < b.getNumSamples(); ++i) p = std::max (p, std::abs (b.getSample (0, i)));
    return p;
}

// zero-crossing frequency estimate over [t0, t1] seconds
double estimateHz (const juce::AudioBuffer<float>& b, double sr, double t0, double t1)
{
    const int a = (int) (t0 * sr), e = std::min (b.getNumSamples() - 1, (int) (t1 * sr));
    int crossings = 0;
    for (int i = a + 1; i < e; ++i)
        if ((b.getSample (0, i - 1) < 0) != (b.getSample (0, i) < 0)) ++crossings;
    return crossings / 2.0 / ((e - a) / sr);
}
}

TEST_CASE ("Factory presets: count, unique names, render sanely")
{
    juce::ScopedJuceInitialiser_GUI init;
    TempDir td;
    KickforgeProcessor proc;
    kf::PresetManager pm (proc, td.dir);

    const auto& list = kf::factoryPresets();
    REQUIRE (list.size() >= 16);
    std::set<juce::String> names;
    for (auto& f : list) REQUIRE (names.insert (f.name).second);

    for (auto& f : list)
    {
        INFO (f.name);
        const int idx = pm.findByName (f.name);
        REQUIRE (idx >= 0);
        REQUIRE (pm.load (idx));
        REQUIRE_FALSE (pm.isDirty());
        const auto buf = kf::renderKick (proc);
        const float pk = peakOf (buf);
        REQUIRE (buf.getNumSamples() >= (int) (0.05 * 48000));
        REQUIRE (buf.getNumSamples() < (int) (8.0 * 48000));
        REQUIRE (pk > 0.02f);
        REQUIRE (pk <= 0.98f); // limiter ceiling -0.3 dB
        for (int i = 0; i < buf.getNumSamples(); ++i) REQUIRE (std::isfinite (buf.getSample (0, i)));
    }
}

TEST_CASE ("Clean Sub renders at its root pitch")
{
    juce::ScopedJuceInitialiser_GUI init;
    TempDir td;
    KickforgeProcessor proc;
    kf::PresetManager pm (proc, td.dir);
    REQUIRE (pm.load (pm.findByName ("Clean Sub")));
    const auto buf = kf::renderKick (proc);
    const double hz = estimateHz (buf, 48000.0, 0.30, 0.45);
    INFO (hz);
    REQUIRE (hz > 55.0 * 0.9);
    REQUIRE (hz < 55.0 * 1.1);
}

TEST_CASE ("Preset round trip keeps params, curves and click sample paths")
{
    juce::ScopedJuceInitialiser_GUI init;
    TempDir td;
    KickforgeProcessor proc;
    kf::PresetManager pm (proc, td.dir);

    // a click sample on disk
    juce::AudioBuffer<float> sine (1, 4800);
    for (int i = 0; i < 4800; ++i) sine.setSample (0, i, 0.5f * std::sin (0.2f * (float) i));
    const auto wav = td.dir.getChildFile ("click.wav");
    REQUIRE (kf::writeWav24 (wav, sine, 48000.0));

    auto setP = [&] (const char* id, float v) {
        auto* p = proc.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    setP ("sub_length", 777.0f);
    setP ("sub_wave", 2.0f);
    setP ("fx_a1_type", 2.0f);
    setP ("click_2_source", 1.0f);
    proc.getSubCurves().setCurve (kf::SubCurveId::Pitch,
                                  kf::Curve ({ { 0, 20, -0.2 }, { 0.5, 3, 0.1 }, { 1, 0, 0 } }));
    proc.getClickLayer().loadSample (1, wav);
    REQUIRE (proc.getClickLayer().waitForLoads());

    REQUIRE (pm.isDirty());
    const auto before = pm.captureXmlString();
    const auto pitchBefore = proc.getSubCurves().getCurve (kf::SubCurveId::Pitch);
    REQUIRE (pm.save ("RoundTrip", "Tests"));
    REQUIRE_FALSE (pm.isDirty());
    REQUIRE (td.dir.getChildFile ("Tests").getChildFile ("RoundTrip.kfpreset").existsAsFile());

    REQUIRE (pm.loadInit());
    REQUIRE (proc.getClickLayer().getSamplePath (1).isEmpty());
    REQUIRE (proc.apvts.getRawParameterValue ("sub_length")->load() != 777.0f);

    const int idx = pm.findByName ("RoundTrip");
    REQUIRE (idx >= 0);
    REQUIRE (pm.load (idx));
    proc.getClickLayer().waitForLoads();
    REQUIRE (pm.captureXmlString() == before);
    REQUIRE (proc.getSubCurves().getCurve (kf::SubCurveId::Pitch) == pitchBefore);
    REQUIRE (proc.getClickLayer().getSamplePath (1) == wav.getFullPathName());
    REQUIRE (proc.apvts.getRawParameterValue ("sub_length")->load() == 777.0f);
    REQUIRE_FALSE (pm.isDirty());

    // file survives a fresh manager (rescan from disk)
    kf::PresetManager pm2 (proc, td.dir);
    REQUIRE (pm2.findByName ("RoundTrip") >= 0);
}

TEST_CASE ("Preset manager: next/prev, dirty, rename, delete, favourites, partial, import/export")
{
    juce::ScopedJuceInitialiser_GUI init;
    TempDir td;
    KickforgeProcessor proc;
    kf::PresetManager pm (proc, td.dir);
    const int n = (int) pm.all().size();
    REQUIRE (n >= 16);

    REQUIRE (pm.next());
    const int first = pm.getCurrentIndex();
    REQUIRE (first == 0);
    REQUIRE (pm.previous());
    REQUIRE (pm.getCurrentIndex() == n - 1);
    REQUIRE (pm.next());
    REQUIRE (pm.getCurrentIndex() == 0);

    // dirty flag
    auto* p = proc.apvts.getParameter ("sub_tune");
    p->setValueNotifyingHost (0.9f);
    REQUIRE (pm.isDirty());

    // filter
    REQUIRE_FALSE (pm.filter ("808").empty());
    REQUIRE (pm.filter ("zzzz-nothing").empty());
    REQUIRE (pm.filter ("", "Techno").size() == 3);

    // save, rename, favourite, delete
    REQUIRE (pm.save ("Mine", "User"));
    int idx = pm.findByName ("Mine");
    REQUIRE (idx >= 0);
    pm.setFavourite (idx, true);
    REQUIRE (pm.filter ("", "", true).size() == 1);
    REQUIRE (pm.rename (idx, "Mine Renamed"));
    idx = pm.findByName ("Mine Renamed");
    REQUIRE (idx >= 0);
    REQUIRE (pm.isFavourite (idx));
    REQUIRE (pm.findByName ("Mine") < 0);

    // export / import
    const auto out = td.dir.getChildFile ("exported.kfpreset");
    REQUIRE (pm.exportFile (idx, out));
    REQUIRE (pm.importFile (out, "Imported"));
    REQUIRE (pm.findByName ("Mine Renamed") >= 0);

    // partial preset: sub-only leaves FX alone
    auto setP = [&] (const char* id, float v) {
        auto* q = proc.apvts.getParameter (id);
        q->setValueNotifyingHost (q->convertTo0to1 (v));
    };
    setP ("sub_length", 1234.0f);
    REQUIRE (pm.save ("SubOnly", "Parts", kf::PresetScope::sub));
    setP ("sub_length", 100.0f);
    setP ("fx_a1_type", 3.0f);
    REQUIRE (pm.load (pm.findByName ("SubOnly")));
    REQUIRE (proc.apvts.getRawParameterValue ("sub_length")->load() == 1234.0f);
    REQUIRE (proc.apvts.getRawParameterValue ("fx_a1_type")->load() == 3.0f);

    REQUIRE (pm.remove (pm.findByName ("SubOnly")));
    REQUIRE (pm.findByName ("SubOnly") < 0);
    // factory presets are protected
    REQUIRE_FALSE (pm.remove (0));
    REQUIRE_FALSE (pm.rename (0, "x"));
}

TEST_CASE ("WAV export is readable with the right length and bit depth")
{
    juce::ScopedJuceInitialiser_GUI init;
    TempDir td;
    KickforgeProcessor proc;
    kf::PresetManager pm (proc, td.dir);
    REQUIRE (pm.load (pm.findByName ("Classic House")));

    kf::RenderOptions o;
    o.sampleRate = 44100.0;
    const auto buf = kf::renderKick (proc, o);
    const auto f = td.dir.getChildFile ("kick.wav");
    REQUIRE (kf::writeWav24 (f, buf, o.sampleRate));

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    REQUIRE (r != nullptr);
    REQUIRE (r->bitsPerSample == 24);
    REQUIRE (r->sampleRate == 44100.0);
    REQUIRE (r->lengthInSamples == buf.getNumSamples());
    juce::AudioBuffer<float> back (1, buf.getNumSamples());
    r->read (&back, 0, buf.getNumSamples(), 0, true, false);
    float maxErr = 0;
    for (int i = 0; i < buf.getNumSamples(); ++i)
        maxErr = std::max (maxErr, std::abs (back.getSample (0, i) - buf.getSample (0, i)));
    REQUIRE (maxErr < 1.0e-5f);
}

TEST_CASE ("Rendering offline does not disturb the live processor")
{
    juce::ScopedJuceInitialiser_GUI init;
    KickforgeProcessor live;
    live.setPlayConfigDetails (0, 2, 48000.0, 256);
    live.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> b (2, 256);
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, 33, (juce::uint8) 100), 0);
    live.processBlock (b, m);
    const float before = std::abs (b.getSample (0, 200));
    REQUIRE (before > 0.0f);
    (void) kf::renderKick (live);
    juce::MidiBuffer none;
    live.processBlock (b, none); // live voice still sounding
    float pk = 0;
    for (int i = 0; i < 256; ++i) pk = std::max (pk, std::abs (b.getSample (0, i)));
    REQUIRE (pk > 0.01f);
}
