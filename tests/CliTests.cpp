#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <cmath>
#include <thread>
#include "PluginProcessor.h"
#include "analysis/KickMetrics.h"
#include "cli/CliCore.h"
#include "presets/KickRenderer.h"

#ifdef _WIN32
 #include <stdlib.h>
#endif

using namespace kf;

namespace
{
struct Dir
{
    juce::File d;
    Dir()
    {
        d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kfcli_" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
        d.createDirectory();
#ifdef _WIN32
        _putenv_s ("KICKFORGE_LIVE_DIR", d.getChildFile ("Live").getFullPathName().toRawUTF8());
#endif
    }
    ~Dir() { d.deleteRecursively(); }
    juce::File operator[] (const char* n) const { return d.getChildFile (n); }
};

juce::var runJson (std::vector<std::string> args, int* codeOut = nullptr)
{
    int code = 0;
    const auto out = cli::run (args, code);
    if (codeOut) *codeOut = code;
    juce::var v;
    REQUIRE (juce::JSON::parse (juce::String (out), v).wasOk());
    return v;
}

void writeStateFile (KickforgeProcessor& p, const juce::File& f)
{
    juce::MemoryBlock mb;
    p.getStateInformation (mb);
    auto x = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
    REQUIRE (x != nullptr);
    x->setAttribute ("kfName", "t");
    REQUIRE (cli::writeFileAtomic (f, x->toString()));
}

void setP (KickforgeProcessor& p, const char* id, double v)
{
    auto* prm = p.apvts.getParameter (id);
    REQUIRE (prm != nullptr);
    prm->setValueNotifyingHost (prm->convertTo0to1 ((float) v));
}

juce::File renderWav (const juce::File& state, const juce::File& wav)
{
    int code = 0;
    cli::run ({ "render", "--state", state.getFullPathName().toStdString(), "--out", wav.getFullPathName().toStdString() }, code);
    REQUIRE (code == 0);
    return wav;
}

double num (const juce::var& v, const char* a) { return (double) v.getProperty (a, 0.0); }
} // namespace

TEST_CASE ("cli: param set/get round trip and choices by label", "[cli]")
{
    Dir t;
    KickforgeProcessor p;
    writeStateFile (p, t["a.kfpreset"]);
    int code = 1;
    auto r = runJson ({ "set", "sub_tune=2.5", "sub_wave=Triangle", "--state", t["a.kfpreset"].getFullPathName().toStdString(),
                        "--out", t["b.kfpreset"].getFullPathName().toStdString() }, &code);
    REQUIRE (code == 0);
    REQUIRE (t["b.kfpreset"].existsAsFile());
    REQUIRE (! t["b.kfpreset"].getSiblingFile ("b.kfpreset.tmp").exists());
    auto g = runJson ({ "get", "sub_tune", "sub_wave", "--state", t["b.kfpreset"].getFullPathName().toStdString() });
    REQUIRE (std::abs (num (g.getProperty ("values", {}), "sub_tune") - 2.5) < 1.0e-3);
    REQUIRE (num (g.getProperty ("values", {}), "sub_wave") == 1.0);
    // unknown id -> JSON error + non-zero exit
    runJson ({ "get", "nope", "--state", t["a.kfpreset"].getFullPathName().toStdString() }, &code);
    REQUIRE (code != 0);
    // params listing
    auto pl = runJson ({ "params", "--filter", "sub_tu", "--state", t["b.kfpreset"].getFullPathName().toStdString() });
    REQUIRE (pl.getProperty ("params", {}).size() == 1);
}

TEST_CASE ("cli: curve set/get round trip", "[cli]")
{
    Dir t;
    KickforgeProcessor p;
    writeStateFile (p, t["a.kfpreset"]);
    const auto a = t["a.kfpreset"].getFullPathName().toStdString(), b = t["b.kfpreset"].getFullPathName().toStdString();
    int code = 1;
    runJson ({ "curve", "set", "pitch", "{\"points\":[{\"t\":0,\"v\":20,\"tension\":-0.5},{\"t\":0.1,\"v\":3,\"tension\":0.2},{\"t\":1,\"v\":0}]}",
               "--state", a, "--out", b }, &code);
    REQUIRE (code == 0);
    auto g = runJson ({ "curve", "get", "pitch", "--state", b });
    auto pts = g.getProperty ("points", {});
    REQUIRE (pts.size() == 3);
    REQUIRE (std::abs (num (pts[0], "v") - 20.0) < 1.0e-3);
    REQUIRE (std::abs (num (pts[0], "tension") + 0.5) < 1.0e-3);
    REQUIRE (std::abs (num (pts[1], "t") - 0.1) < 1.0e-3);
    runJson ({ "curve", "get", "bogus", "--state", b }, &code);
    REQUIRE (code != 0);
}

TEST_CASE ("cli: render is deterministic", "[cli]")
{
    Dir t;
    KickforgeProcessor p;
    writeStateFile (p, t["a.kfpreset"]);
    renderWav (t["a.kfpreset"], t["1.wav"]);
    renderWav (t["a.kfpreset"], t["2.wav"]);
    juce::MemoryBlock m1, m2;
    REQUIRE (t["1.wav"].loadFileAsData (m1));
    REQUIRE (t["2.wav"].loadFileAsData (m2));
    REQUIRE (m1.getSize() > 1000);
    REQUIRE (m1 == m2);
}

TEST_CASE ("cli: analyze finds the known tail Hz of a synthesized kick", "[cli]")
{
    Dir t;
    const double sr = 48000.0, tail = 50.0;
    juce::AudioBuffer<float> b (1, (int) (0.8 * sr));
    double ph = 0.0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double ts = i / sr;
        const double f = tail + 120.0 * std::exp (-ts / 0.02);
        ph += 2.0 * juce::MathConstants<double>::pi * f / sr;
        b.setSample (0, i, (float) (0.8 * std::sin (ph + 1.5707963) * std::exp (-ts / 0.25)));
    }
    REQUIRE (writeWav24 (t["k.wav"], b, sr));
    auto r = runJson ({ "analyze", t["k.wav"].getFullPathName().toStdString() });
    const double hz = num (r.getProperty ("features", {}), "tail_hz");
    REQUIRE (std::abs (hz - tail) / tail < 0.01);
    // output budget
    int code = 0;
    REQUIRE (cli::run ({ "analyze", t["k.wav"].getFullPathName().toStdString() }, code).size() < 4096);
}

TEST_CASE ("cli: compare self ~100, +3 st sub_tune drops with pitch hints, fit recovers", "[cli]")
{
    Dir t;
    KickforgeProcessor p;
    setP (p, "sub_length", 400.0);
    writeStateFile (p, t["a.kfpreset"]);
    const auto A = t["a.kfpreset"].getFullPathName().toStdString();
    renderWav (t["a.kfpreset"], t["ref.wav"]);
    const auto ref = t["ref.wav"].getFullPathName().toStdString();

    int code = 1;
    auto self = runJson ({ "compare", "--state", A, "--ref", ref }, &code);
    REQUIRE (code == 0);
    CHECK (num (self, "score") > 97.0);

    // +3 st tune
    runJson ({ "set", "sub_tune=3", "--state", A, "--out", t["b.kfpreset"].getFullPathName().toStdString() });
    const auto B = t["b.kfpreset"].getFullPathName().toStdString();
    auto off = runJson ({ "compare", "--state", B, "--ref", ref });
    CHECK (num (off, "score") < num (self, "score") - 10.0);
    bool pitchHint = false;
    for (auto& h : *off.getProperty ("hints", {}).getArray()) pitchHint |= h.toString().containsIgnoreCase ("pitch");
    CHECK (pitchHint);
    int c2 = 0;
    CHECK (cli::run ({ "compare", "--state", B, "--ref", ref }, c2).size() < 4096);

    // fit: perturb tune + length, expect recovery (fit only those two)
    KickforgeProcessor q;
    setP (q, "sub_length", 520.0);
    setP (q, "sub_tune", 1.2);
    writeStateFile (q, t["c.kfpreset"]);
    const auto C = t["c.kfpreset"].getFullPathName().toStdString(), D = t["d.kfpreset"].getFullPathName().toStdString();
    auto fit = runJson ({ "fit", "--state", C, "--ref", ref, "--params", "sub_tune,sub_length", "--curves", "", "--iters", "80", "--out", D }, &code);
    REQUIRE (code == 0);
    CHECK (num (fit.getProperty ("after", {}), "score") > num (fit.getProperty ("before", {}), "score"));
    auto g = runJson ({ "get", "sub_tune", "sub_length", "--state", D });
    auto vals = g.getProperty ("values", {});
    CHECK (std::abs (num (vals, "sub_tune")) < 0.15);
    CHECK (std::abs (num (vals, "sub_length") - 400.0) < 32.0);
    // the live state must not have been touched without --apply
    CHECK (! t["Live"].getChildFile ("apply.kfpreset").exists());
}

TEST_CASE ("cli: apply is atomic, confirmed by status.json, and tolerates no plugin", "[cli]")
{
    Dir t;
    KickforgeProcessor p;
    writeStateFile (p, t["a.kfpreset"]);
    const auto A = t["a.kfpreset"].getFullPathName().toStdString();
    const auto live = t["Live"];
    live.createDirectory();

    // no plugin: timeout, not an error, stale file removed
    int code = 1;
    auto none = runJson ({ "apply", "--state", A, "--timeout", "0.4" }, &code);
    REQUIRE (code == 0);
    CHECK (! (bool) none.getProperty ("applied", true));
    CHECK (none.getProperty ("note", "").toString().containsIgnoreCase ("no plugin"));
    CHECK (! live.getChildFile ("apply.kfpreset").exists());

    // fake plugin
    std::atomic<bool> stop { false }, sawWellFormed { false };
    std::thread plugin ([&] {
        live.getChildFile ("status.json").replaceWithText ("{\"instance\":\"x\",\"pid\":1,\"applied\":0,\"lastApplyOk\":true,\"error\":\"\",\"time\":\"\"}");
        int counter = 0;
        while (! stop)
        {
            auto f = live.getChildFile ("apply.kfpreset");
            if (f.existsAsFile())
            {
                auto x = juce::XmlDocument::parse (f); // an atomic rename never exposes a partial file
                sawWellFormed = x != nullptr;
                f.deleteFile();
                live.getChildFile ("status.json").replaceWithText ("{\"instance\":\"x\",\"pid\":1,\"applied\":" + juce::String (++counter) + ",\"lastApplyOk\":true,\"error\":\"\",\"time\":\"\"}");
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
    });
    juce::Thread::sleep (100);
    auto ok = runJson ({ "apply", "--state", A, "--timeout", "3" }, &code);
    stop = true;
    plugin.join();
    CHECK (code == 0);
    CHECK ((bool) ok.getProperty ("applied", false));
    CHECK (sawWellFormed.load());
    CHECK (! live.getChildFile ("apply.kfpreset.tmp").exists());
}

TEST_CASE ("cli: plot writes a valid 1200x800 PNG", "[cli]")
{
    Dir t;
    KickforgeProcessor p;
    writeStateFile (p, t["a.kfpreset"]);
    renderWav (t["a.kfpreset"], t["ref.wav"]);
    KickforgeProcessor q;
    setP (q, "sub_tune", 2.0);
    writeStateFile (q, t["b.kfpreset"]);
    int code = 1;
    auto r = runJson ({ "plot", "--state", t["b.kfpreset"].getFullPathName().toStdString(), "--ref", t["ref.wav"].getFullPathName().toStdString(),
                        "--out", t["p.png"].getFullPathName().toStdString() }, &code);
    REQUIRE (code == 0);
    auto img = juce::ImageFileFormat::loadFrom (t["p.png"]);
    REQUIRE (img.isValid());
    CHECK (img.getWidth() == 1200);
    CHECK (img.getHeight() == 800);
    if (auto* keep = std::getenv ("KF_CLI_KEEP_PLOT")) // optional: keep artefacts for the smoke script
    {
        const juce::File k (keep);
        t["p.png"].copyFileTo (k);
        t["b.kfpreset"].copyFileTo (k.getSiblingFile ("smoke_synth.kfpreset"));
        t["a.kfpreset"].copyFileTo (k.getSiblingFile ("smoke_ref.kfpreset"));
        t["ref.wav"].copyFileTo (k.getSiblingFile ("smoke_ref.wav"));
    }
}

TEST_CASE ("cli: the real kickforge-cli.exe runs (if built)", "[cli]")
{
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("kickforge-cli.exe");
    if (! exe.existsAsFile()) { SUCCEED ("exe not built, skipped"); return; }
    Dir t;
    KickforgeProcessor p;
    writeStateFile (p, t["a.kfpreset"]);
    juce::ChildProcess proc;
    REQUIRE (proc.start (juce::StringArray { exe.getFullPathName(), "get", "sub_tune", "--state", t["a.kfpreset"].getFullPathName() }));
    const auto out = proc.readAllProcessOutput();
    CHECK (proc.waitForProcessToFinish (20000));
    CHECK (proc.getExitCode() == 0);
    juce::var v;
    REQUIRE (juce::JSON::parse (out, v).wasOk());
    CHECK (v.getProperty ("values", {}).hasProperty ("sub_tune"));
}
