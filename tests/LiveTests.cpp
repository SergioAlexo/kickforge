#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdlib>

using Catch::Matchers::WithinAbs;

namespace
{
/** A scratch live folder wired in through the KICKFORGE_LIVE_DIR override. */
struct LiveDir
{
    LiveDir()
    {
        dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kf-live-" + juce::Uuid().toString());
        dir.createDirectory();
        set (dir.getFullPathName());
    }
    ~LiveDir()
    {
        set ({});
        dir.deleteRecursively();
    }
    static void set (const juce::String& v)
    {
#if JUCE_WINDOWS
        _putenv_s ("KICKFORGE_LIVE_DIR", v.toRawUTF8());
#else
        setenv ("KICKFORGE_LIVE_DIR", v.toRawUTF8(), 1);
#endif
    }
    juce::File file (const char* n) const { return dir.getChildFile (n); }
    juce::var json (const char* n) const { return juce::JSON::parse (file (n).loadFileAsString()); }
    juce::File dir;
};

juce::File writeDcWav (const juce::File& dir, const char* name, float v, int n, double rate = 48000.0)
{
    auto f = dir.getChildFile (name);
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), rate, 1, 16, {}, 0));
    if (w != nullptr)
    {
        os.release();
        juce::AudioBuffer<float> b (1, n);
        for (int i = 0; i < n; ++i) b.setSample (0, i, v);
        w->writeFromAudioSampleBuffer (b, 0, n);
    }
    return f;
}

void prep (KickforgeProcessor& p, int block = 512)
{
    p.setPlayConfigDetails (0, 2, 48000.0, block);
    p.prepareToPlay (48000.0, block);
}

void setParam (KickforgeProcessor& p, const char* id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

using Buf = juce::AudioBuffer<float>;

/** Renders `blocks` blocks; note-on at `onAt` of block 0. Returns the left channel. */
std::vector<float> render (KickforgeProcessor& p, int blocks, int onAt = 0, int block = 512, std::vector<int> extraOns = {})
{
    std::vector<float> out;
    Buf b (p.getTotalNumOutputChannels(), block);
    for (int k = 0; k < blocks; ++k)
    {
        juce::MidiBuffer m;
        if (k == 0) m.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), onAt);
        for (int e : extraOns)
            if (e / block == k) m.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), e % block);
        b.clear();
        p.processBlock (b, m);
        out.insert (out.end(), b.getReadPointer (0), b.getReadPointer (0) + block);
    }
    return out;
}
}

TEST_CASE ("LiveLink: write, apply round trip in a temp dir", "[live]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LiveDir ld;
    KickforgeProcessor p;
    prep (p);
    auto wav = writeDcWav (ld.dir, "ref.wav", 0.25f, 4800);
    REQUIRE (p.getReference().load (wav, 100, 4000));

    auto& link = p.getLiveLink();
    REQUIRE (link.resolveFolder() == ld.dir);
    link.setEnabled (true);
    link.tick (1000);
    REQUIRE (ld.file ("state.kfpreset").existsAsFile());
    REQUIRE (ld.file ("status.json").existsAsFile());
    REQUIRE (ld.file ("reference.json").existsAsFile());

    auto st = ld.json ("status.json");
    REQUIRE (st["instance"].toString() == link.getInstanceId());
    REQUIRE ((int) st["applied"] == 0);
    auto rj = ld.json ("reference.json");
    REQUIRE (rj["path"].toString() == wav.getFullPathName());
    REQUIRE ((int) rj["regionStart"] == 100);
    REQUIRE ((int) rj["regionEnd"] == 4000);
    REQUIRE ((double) rj["sampleRate"] == 48000.0);
    REQUIRE ((int) rj["note"] == (int) p.apvts.getRawParameterValue ("sub_root")->load());

    // state.kfpreset is the preset XML and carries no link / reference data
    auto xml = juce::XmlDocument::parse (ld.file ("state.kfpreset"));
    REQUIRE (xml != nullptr);
    REQUIRE (xml->hasTagName (p.apvts.state.getType()));
    REQUIRE (xml->getChildByName ("kfLive") == nullptr);
    REQUIRE (xml->getChildByName ("click") != nullptr);
    REQUIRE (xml->getChildByName ("CURVES") != nullptr);

    // apply: change a param + a curve, add a kfLive child that must be ignored
    for (auto* c = xml->getFirstChildElement(); c != nullptr; c = c->getNextElement())
        if (c->hasTagName ("PARAM") && c->getStringAttribute ("id") == "glob_gain")
            c->setAttribute ("value", -6.0);
    auto* bogus = xml->createNewChildElement ("kfLive");
    bogus->setAttribute ("refPath", "");
    bogus->setAttribute ("link", 0);
    ld.file ("apply.kfpreset").replaceWithText (xml->toString());

    const int before = link.getWriteCount();
    link.tick (1100);          // < 250 ms since the last poll: not picked up yet
    REQUIRE (ld.file ("apply.kfpreset").existsAsFile());
    link.tick (1300);
    REQUIRE_FALSE (ld.file ("apply.kfpreset").existsAsFile());
    REQUIRE_THAT (p.apvts.getRawParameterValue ("glob_gain")->load(), WithinAbs (-6.0, 1e-3));
    REQUIRE (link.getWriteCount() == before + 1); // state rewritten immediately
    auto st2 = ld.json ("status.json");
    REQUIRE ((int) st2["applied"] == 1);
    REQUIRE ((bool) st2["lastApplyOk"]);
    REQUIRE (st2["error"].toString().isEmpty());
    REQUIRE (st2["time"].toString().isNotEmpty());
    auto again = juce::XmlDocument::parse (ld.file ("state.kfpreset"));
    bool found = false;
    for (auto* c = again->getFirstChildElement(); c != nullptr; c = c->getNextElement())
        if (c->hasTagName ("PARAM") && c->getStringAttribute ("id") == "glob_gain")
            found = std::abs (c->getDoubleAttribute ("value") + 6.0) < 1e-3;
    REQUIRE (found);

    // the Reference and the link switch survive an apply
    REQUIRE (p.getReference().hasAudio());
    REQUIRE (p.getReference().getInfo().path == wav.getFullPathName());
    REQUIRE (link.isEnabled());
    link.setEnabled (false);
}

TEST_CASE ("LiveLink: malformed apply file is rejected and removed", "[live]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LiveDir ld;
    KickforgeProcessor p;
    prep (p);
    setParam (p, "glob_gain", -3.0f);
    auto& link = p.getLiveLink();
    link.setEnabled (true);
    link.tick (1000);

    for (const char* bad : { "this is <not xml", "<?xml version=\"1.0\"?><WRONGROOT/>" })
    {
        const int applied0 = (int) ld.json ("status.json")["applied"];
        ld.file ("apply.kfpreset").replaceWithText (bad);
        static juce::uint32 t = 1300;
        link.tick (t += 300);
        REQUIRE_FALSE (ld.file ("apply.kfpreset").existsAsFile());
        auto st = ld.json ("status.json");
        REQUIRE_FALSE ((bool) st["lastApplyOk"]);
        REQUIRE (st["error"].toString().isNotEmpty());
        REQUIRE ((int) st["applied"] == applied0 + 1);
        REQUIRE_THAT (p.apvts.getRawParameterValue ("glob_gain")->load(), WithinAbs (-3.0, 1e-3)); // unchanged
    }
    link.setEnabled (false);
}

TEST_CASE ("LiveLink: only one instance stays linked", "[live]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LiveDir ld;
    KickforgeProcessor a, b;
    prep (a);
    prep (b);
    a.getLiveLink().setEnabled (true);
    a.getLiveLink().tick (1000);
    REQUIRE ((ld.json ("status.json")["instance"].toString()) == a.getLiveLink().getInstanceId());

    // B takes over, continuing the applied counter
    ld.file ("apply.kfpreset").deleteFile();
    b.getLiveLink().setEnabled (true);
    b.getLiveLink().tick (1100);
    REQUIRE ((ld.json ("status.json")["instance"].toString()) == b.getLiveLink().getInstanceId());

    a.getLiveLink().tick (1400);
    REQUIRE_FALSE (a.getLiveLink().isEnabled());
    REQUIRE (a.getLiveLink().getStatus().superseded);
    REQUIRE (b.getLiveLink().isEnabled());
    b.getLiveLink().tick (1400);
    REQUIRE (b.getLiveLink().isEnabled());
    REQUIRE ((ld.json ("status.json")["instance"].toString()) == b.getLiveLink().getInstanceId());

    // A no longer polls apply files
    ld.file ("apply.kfpreset").replaceWithText ("garbage");
    a.getLiveLink().tick (1700);
    REQUIRE (ld.file ("apply.kfpreset").existsAsFile());
    b.getLiveLink().setEnabled (false);
}

TEST_CASE ("LiveLink: many changes collapse into one debounced write", "[live]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LiveDir ld;
    KickforgeProcessor p;
    prep (p);
    auto& link = p.getLiveLink();
    link.setEnabled (true);
    link.tick (1000);
    link.tick (1040);
    const int w0 = link.getWriteCount();

    // a burst of 40 edits within 60 ms
    juce::uint32 t = 2000;
    for (int i = 0; i < 40; ++i)
    {
        setParam (p, "glob_gain", -(float) (i % 12));
        if (i % 4 == 0) link.tick (t += 6);
    }
    for (; t < 2400; t += 10) link.tick (t);
    REQUIRE (link.getWriteCount() == w0 + 1);

    // quiet: no further writes
    for (; t < 3000; t += 40) link.tick (t);
    REQUIRE (link.getWriteCount() == w0 + 1);

    // a continuous drag is still flushed at least every maxWait
    const int w1 = link.getWriteCount();
    for (int i = 0; i < 100; ++i) // 1 s of edits, one every 10 ms
    {
        setParam (p, "glob_gain", -(float) (i % 12));
        link.tick (t += 10);
    }
    REQUIRE (link.getWriteCount() >= w1 + 4);
    REQUIRE (link.getWriteCount() <= w1 + 8);
    for (int i = 0; i < 20; ++i) link.tick (t += 40);
    // an envelope edit of a click layer (version counter) is picked up as well
    const int w2 = link.getWriteCount();
    p.getClickLayer().setEnvelope (0, kf::Curve ({ { 0.0, 1.0, 0.0 }, { 1.0, 0.2, 0.0 } }));
    for (int i = 0; i < 20; ++i) link.tick (t += 40);
    REQUIRE (link.getWriteCount() == w2 + 1);
    link.setEnabled (false);
}

TEST_CASE ("Reference A/B mixing: Synth, Reference, Both", "[live]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LiveDir ld;
    auto wav = writeDcWav (ld.dir, "dc.wav", 0.25f, 9600); // 200 ms

    auto run = [&] (kf::RefMode m, bool withRef)
    {
        KickforgeProcessor p;
        prep (p);
        if (withRef) REQUIRE (p.getReference().load (wav));
        p.getReference().setMode (m);
        return render (p, 8);
    };
    const auto synthOnly = run (kf::RefMode::synth, false);
    const auto synthMode = run (kf::RefMode::synth, true);
    float synthPeak = 0.0f;
    for (float s : synthOnly) synthPeak = std::max (synthPeak, std::abs (s));
    REQUIRE (synthPeak > 0.1f);

    // Synth mode = the reference is not heard
    REQUIRE (synthMode == synthOnly);

}

TEST_CASE ("Reference A/B mixing: sum and levels", "[live]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LiveDir ld;
    auto wav = writeDcWav (ld.dir, "dc.wav", 0.25f, 3000);

    auto run = [&] (kf::RefMode m, bool withRef, float gainDb = 0.0f)
    {
        KickforgeProcessor p;
        prep (p);
        if (withRef) REQUIRE (p.getReference().load (wav));
        p.getReference().setMode (m);
        p.getReference().setGainDb (gainDb);
        return render (p, 12);
    };
    const auto synthOnly = run (kf::RefMode::synth, false);
    const auto refMode = run (kf::RefMode::reference, true);
    const auto both = run (kf::RefMode::both, true);
    const auto refHalf = run (kf::RefMode::reference, true, -6.0206f);

    KickforgeProcessor probe;
    prep (probe);
    const size_t lat = (size_t) probe.getLatencySamples();
    for (size_t i = 0; i < refMode.size(); ++i)
    {
        const float expect = (i >= lat && i < lat + 2999) ? 0.25f : 0.0f;
        REQUIRE_THAT (refMode[i], WithinAbs (expect, 1e-4)); // no synth in Reference mode
        REQUIRE_THAT (both[i], WithinAbs (synthOnly[i] + refMode[i], 1e-5)); // Both = sum
    }
    REQUIRE_THAT (refHalf[lat + 500], WithinAbs (0.125, 1e-3)); // reference gain
}

TEST_CASE ("Reference retriggers on every note-on, aligned to the synth latency", "[live]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LiveDir ld;
    auto wav = writeDcWav (ld.dir, "dc.wav", 0.25f, 1000);
    KickforgeProcessor p;
    prep (p);
    REQUIRE (p.getReference().load (wav));
    p.getReference().setMode (kf::RefMode::reference);
    const int lat = p.getLatencySamples();

    // note-ons at 100 and at 3000: two separate 1000-sample bursts
    const auto o = render (p, 10, 100, 512, { 3000 });
    auto count = [&] (size_t a, size_t b) { int n = 0; for (size_t i = a; i < b; ++i) n += o[i] > 0.2f ? 1 : 0; return n; };
    REQUIRE (o[(size_t) (100 + lat) - 1] == 0.0f);
    REQUIRE (o[(size_t) (100 + lat)] > 0.2499f);
    REQUIRE (count (0, 2500) == 999);   // the clip's last sample is not played (interpolation edge)
    REQUIRE (o[(size_t) (3000 + lat) - 1] == 0.0f);
    REQUIRE (o[(size_t) (3000 + lat)] > 0.2499f);
    REQUIRE (count (2500, o.size()) == 999);

    // a retrigger while still playing restarts from the clip start
    KickforgeProcessor q;
    prep (q);
    REQUIRE (q.getReference().load (writeDcWav (ld.dir, "ramp.wav", 0.25f, 2000)));
    q.getReference().setMode (kf::RefMode::reference);
    const auto r = render (q, 10, 0, 512, { 1000 });
    const size_t l2 = (size_t) q.getLatencySamples();
    REQUIRE (r[l2 + 999] > 0.2499f);
    REQUIRE (r[l2 + 1000 - 1] > 0.2499f);
    int plateau = 0;
    for (size_t i = l2; i < r.size(); ++i) plateau += r[i] > 0.2f ? 1 : 0;
    REQUIRE (plateau == 1000 + 1999); // first run cut at the retrigger, second run complete
}

TEST_CASE ("Reference and Live-link switch persist in plugin state", "[live]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LiveDir ld;
    auto wav = writeDcWav (ld.dir, "dc.wav", 0.25f, 4800);
    juce::MemoryBlock mb;
    {
        KickforgeProcessor a;
        prep (a);
        REQUIRE (a.getReference().load (wav, 10, 2000));
        a.getReference().setNote (40);
        a.getReference().setGainDb (-4.5f);
        a.getReference().setMode (kf::RefMode::both);
        a.getLiveLink().setEnabled (true);
        a.getStateInformation (mb);
        a.getLiveLink().setEnabled (false);
    }
    KickforgeProcessor b;
    prep (b);
    REQUIRE_FALSE (b.getLiveLink().isEnabled());
    b.setStateInformation (mb.getData(), (int) mb.getSize());
    const auto i = b.getReference().getInfo();
    REQUIRE (i.path == wav.getFullPathName());
    REQUIRE (i.regionStart == 10);
    REQUIRE (i.regionEnd == 2000);
    REQUIRE (i.note == 40);
    REQUIRE_THAT (i.gainDb, WithinAbs (-4.5, 1e-4));
    REQUIRE (i.mode == kf::RefMode::both);
    REQUIRE (b.getReference().hasAudio());
    REQUIRE (b.getLiveLink().isEnabled());
    b.getLiveLink().setEnabled (false);

    // a state without the reference clears nothing silently, an empty reference clears it
    KickforgeProcessor c;
    prep (c);
    juce::MemoryBlock mc;
    c.getStateInformation (mc);
    b.setStateInformation (mc.getData(), (int) mc.getSize());
    REQUIRE_FALSE (b.getReference().hasReference());
    REQUIRE_FALSE (b.getLiveLink().isEnabled());
}

TEST_CASE ("Editor constructs and paints with the Live link header and Reference strip", "[live][ui]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    LiveDir ld;
    auto wav = writeDcWav (ld.dir, "dc.wav", 0.25f, 4800);
    KickforgeProcessor p;
    prep (p);
    REQUIRE (p.getReference().load (wav));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* ke = dynamic_cast<KickforgeEditor*> (ed.get());
    REQUIRE (ke != nullptr);
    for (int w : { 560, 1120 })
    {
        ed->setSize (w, (int) std::lround (w * (double) KickforgeEditor::designH / KickforgeEditor::designW));
        REQUIRE (ed->createComponentSnapshot (ed->getLocalBounds(), false, 0.5f).isValid());
    }
    ke->getLiveToggle().clicked();
    REQUIRE (p.getLiveLink().isEnabled());
    ke->getLiveToggle().clicked();
    REQUIRE_FALSE (p.getLiveLink().isEnabled());
    ke->getRefStrip().setModeIndex (2);
    REQUIRE (p.getReference().getMode() == kf::RefMode::both);

    if (auto dir = juce::SystemStats::getEnvironmentVariable ("KF_UI_SNAPSHOT_DIR", {}); dir.isNotEmpty())
    {
        ed->setSize (KickforgeEditor::designW, KickforgeEditor::designH);
        // some synth output so the scope has a kick to overlay the reference on
        p.getReference().setMode (kf::RefMode::both);
        ke->getRefStrip().refresh();
        render (p, 40, 0);
        ke->pumpForTest();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os (juce::File (dir).getChildFile ("ui_live.png"));
        if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
    }
}
