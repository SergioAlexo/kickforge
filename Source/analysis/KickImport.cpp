#include "KickImport.h"
#include "PluginProcessor.h"
#include "presets/KickRenderer.h"
#include "core/Tuning.h"

namespace kf
{
juce::File defaultImportsDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                 .getChildFile ("Kickforge").getChildFile ("Imports");
    d.createDirectory();
    return d;
}

bool loadAudioMono (const juce::File& f, std::vector<float>& mono, double& sr, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr || rd->lengthInSamples <= 0) { error = "Cannot read " + f.getFileName(); return false; }
    const int n = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) rd->sampleRate * 600);
    juce::AudioBuffer<float> b ((int) rd->numChannels, n);
    rd->read (&b, 0, n, 0, true, true);
    mono.assign ((size_t) n, 0.0f);
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < n; ++i) mono[(size_t) i] += b.getSample (c, i) / (float) b.getNumChannels();
    sr = rd->sampleRate;
    return true;
}

ImportSummary analyseFile (const juce::File& source, const ImportOptions& o, double a4)
{
    ImportSummary s;
    std::vector<float> mono;
    double sr = 0;
    if (! loadAudioMono (source, mono, sr, s.message)) return s;

    const auto hits = analysis::detectHits (mono.data(), (int) mono.size(), sr);
    s.numHits = (int) hits.size();
    analysis::Options ao;
    ao.harmonics = o.harmonics;
    ao.a4 = a4;
    s.result = analysis::analyse (mono.data(), (int) mono.size(), sr, o.regionStart, o.regionEnd, ao);
    if (! s.result.ok) { s.message = s.result.error; return s; }
    for (size_t i = 0; i < hits.size(); ++i)
        if (hits[i].start == s.result.regionStart) s.hitIndex = (int) i;

    if (! s.result.residual.empty())
    {
        auto dir = o.importsDir != juce::File() ? o.importsDir : defaultImportsDir();
        dir.createDirectory();
        const auto name = juce::File::createLegalFileName (source.getFileNameWithoutExtension()) + "_top_"
                          + juce::String::toHexString (source.getFullPathName().hashCode() ^ s.result.regionStart) + ".wav";
        s.topEndFile = dir.getChildFile (name);
        juce::AudioBuffer<float> b (1, (int) s.result.residual.size());
        b.copyFrom (0, 0, s.result.residual.data(), b.getNumSamples());
        if (! writeWav24 (s.topEndFile, b, sr)) { s.topEndFile = juce::File(); s.message = "Could not write the top-end sample"; }
    }
    const auto note = tuning::hzToNote (s.result.tailHz, a4);
    s.ok = true;
    s.message = juce::String ("Imported: tail ") + juce::String (s.result.tailHz, 1) + " Hz (" + note.name
                + "), start " + juce::String (s.result.startHz, 0) + " Hz, length "
                + juce::String ((int) s.result.sub.lengthMs) + " ms, " + juce::String (s.result.numHarmonicsUsed)
                + " harmonics" + (s.numHits > 1 ? ", hit " + juce::String (s.hitIndex + 1) + " of " + juce::String (s.numHits) : juce::String());
    return s;
}

namespace
{
void setPlain (juce::AudioProcessorValueTreeState& a, const juce::String& id, float v)
{
    if (auto* p = a.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}
}

void replaceTopEndSample (KickforgeProcessor& p, const juce::File& sample, int layer)
{
    setPlain (p.apvts, clickParamId (layer, CP::source), (float) srcSample);
    p.getClickLayer().loadSample (layer, sample);
}

void applyImport (KickforgeProcessor& p, const ImportSummary& s, const ImportOptions& o)
{
    if (! s.ok) return;
    const auto& r = s.result;
    auto& a = p.apvts;
    const auto& S = r.sub;

    setPlain (a, "sub_mute", 0.0f);
    setPlain (a, "sub_wave", (float) SubWave::Sine);
    setPlain (a, "sub_pitchmode", (float) PitchMode::Semitones);
    setPlain (a, "sub_root", S.rootNote);
    setPlain (a, "sub_tune", S.tuneSemis);
    setPlain (a, "sub_fine", 0.0f);
    setPlain (a, "sub_length", S.lengthMs);
    setPlain (a, "sub_gain", S.gainDb);
    setPlain (a, "sub_phase", S.startPhaseDeg);
    setPlain (a, "sub_delay", 0.0f);
    setPlain (a, "sub_drive", 0.0f);
    setPlain (a, "sub_lock", 0.0f);
    for (int i = 0; i < SubSettings::kNumHarmonics; ++i)
    {
        const auto n = juce::String (i + 1);
        setPlain (a, "sub_h" + n + "_semi", S.harm[(size_t) i].semis);
        setPlain (a, "sub_h" + n + "_gain", S.harm[(size_t) i].gainDb);
        setPlain (a, "sub_h" + n + "_decay", S.harm[(size_t) i].decayMs);
    }
    p.getSubCurves().setCurve (SubCurveId::Pitch, r.pitch);
    p.getSubCurves().setCurve (SubCurveId::Amp, r.amp);
    if (r.numHarmonicsUsed > 0)
        p.getSubCurves().setCurve (SubCurveId::HarmAmount, r.harmAmount);

    if (s.topEndFile.existsAsFile())
    {
        const int L = o.clickLayer;
        setPlain (a, clickParamId (L, CP::mute), 0.0f);
        setPlain (a, clickParamId (L, CP::gain), 0.0f);
        setPlain (a, clickParamId (L, CP::start), 0.0f);
        setPlain (a, clickParamId (L, CP::delay), 0.0f);
        setPlain (a, clickParamId (L, CP::length), clickSpec (CP::length).hi);
        setPlain (a, clickParamId (L, CP::pitch), 0.0f);
        setPlain (a, clickParamId (L, CP::fine), 0.0f);
        setPlain (a, clickParamId (L, CP::keyTrack), 0.0f);
        setPlain (a, clickParamId (L, CP::filterType), (float) filtOff);
        setPlain (a, clickParamId (L, CP::flip), 0.0f);
        p.getClickLayer().setEnvelope (L, Curve ({ { 0.0, 1.0, 0.0 }, { 1.0, 1.0, 0.0 } }));
        replaceTopEndSample (p, s.topEndFile, L);
    }
}

// ---------------------------------------------------------------- background worker
struct KickImporter::Worker : juce::Thread
{
    Worker (juce::File src, ImportOptions op, double a4v, std::function<void (ImportSummary)> d)
        : juce::Thread ("Kickforge import"), source (std::move (src)), opts (std::move (op)), a4 (a4v), done (std::move (d)) {}
    void run() override { done (analyseFile (source, opts, a4)); }
    juce::File source;
    ImportOptions opts;
    double a4;
    std::function<void (ImportSummary)> done;
};

KickImporter::KickImporter (KickforgeProcessor& p) : proc (p) {}

KickImporter::~KickImporter()
{
    alive->store (false);
    if (thread != nullptr) thread->stopThread (20000);
}

void KickImporter::start (const juce::File& source, ImportOptions o, std::function<void (const ImportSummary&)> done)
{
    if (busy->load()) return;
    if (thread != nullptr) thread->stopThread (20000);
    busy->store (true);
    double a4 = 440.0;
    if (auto* v = proc.apvts.getRawParameterValue ("glob_a4")) a4 = (double) v->load();
    auto aliveFlag = alive;
    auto busyFlag = busy;
    auto* procPtr = &proc;
    thread = std::make_unique<Worker> (source, o, a4, [aliveFlag, busyFlag, procPtr, o, done] (ImportSummary s) {
        juce::MessageManager::callAsync ([aliveFlag, busyFlag, procPtr, o, done, s = std::move (s)] {
            busyFlag->store (false);
            if (! aliveFlag->load()) return;
            applyImport (*procPtr, s, o);
            if (done) done (s);
        });
    });
    thread->startThread();
}
}
