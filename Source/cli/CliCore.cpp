#include "CliCore.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include "PluginProcessor.h"
#include "analysis/KickImport.h"
#include "analysis/KickMetrics.h"
#include "presets/KickRenderer.h"

namespace kf::cli
{
using namespace kf::analysis;

namespace
{
struct Err : std::runtime_error { using std::runtime_error::runtime_error; };

// ------------------------------------------------------------------ JSON helpers
struct Obj
{
    juce::DynamicObject::Ptr p = new juce::DynamicObject;
    Obj& add (const char* k, const juce::var& v) { p->setProperty (juce::Identifier (k), v); return *this; }
    operator juce::var() const { return juce::var (p.get()); }
};

juce::var N (double v, int dp = 2)
{
    if (! std::isfinite (v)) return {};
    const double m = std::pow (10.0, dp);
    return juce::var (std::round (v * m) / m);
}
template <typename C> juce::var A (const C& c, int dp = 2)
{
    juce::Array<juce::var> a;
    for (auto v : c) a.add (N ((double) v, dp));
    return a;
}
juce::var S (const std::string& s) { return juce::var (juce::String::fromUTF8 (s.c_str())); }
juce::var strArr (const std::vector<std::string>& v)
{
    juce::Array<juce::var> a;
    for (auto& s : v) a.add (S (s));
    return a;
}
juce::var timesJson() { return A (kTimesMs, 0); }

std::string toJson (const juce::var& v, bool pretty = false)
{
    return juce::JSON::toString (v, ! pretty, 4).toStdString();
}
} // namespace

// ------------------------------------------------------------------ files
juce::File liveDir()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("KICKFORGE_LIVE_DIR", {});
    if (env.isNotEmpty()) return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Kickforge").getChildFile ("Live");
}

bool writeFileAtomic (const juce::File& dest, const juce::String& text)
{
    dest.getParentDirectory().createDirectory();
    juce::File tmp (dest.getFullPathName() + ".tmp");
    tmp.deleteFile();
    {
        juce::FileOutputStream os (tmp);
        if (os.failedToOpen()) return false;
        os.setPosition (0);
        os.truncate();
        os.write (text.toRawUTF8(), text.getNumBytesAsUTF8());
        os.flush();
    }
    std::error_code ec;
    std::filesystem::rename (tmp.getFullPathName().toWideCharPointer(), dest.getFullPathName().toWideCharPointer(), ec);
    if (! ec) return true;
    return tmp.moveFileTo (dest);
}

namespace
{
// ------------------------------------------------------------------ args
struct Args
{
    std::vector<std::string> pos;
    std::map<std::string, std::string> opt;
    bool has (const char* k) const { return opt.count (k) > 0; }
    juce::String get (const char* k, const juce::String& d = {}) const
    {
        auto it = opt.find (k);
        return it == opt.end() ? d : juce::String::fromUTF8 (it->second.c_str());
    }
    double num (const char* k, double d) const { return has (k) ? get (k).getDoubleValue() : d; }
};

Args parseArgs (const std::vector<std::string>& a, size_t from)
{
    static const std::set<std::string> flags = { "full", "text", "apply", "changed", "help" };
    Args r;
    for (size_t i = from; i < a.size(); ++i)
    {
        const auto& t = a[i];
        if (t.size() > 2 && t[0] == '-' && t[1] == '-')
        {
            auto k = t.substr (2);
            const auto eq = k.find ('=');
            if (eq != std::string::npos) { r.opt[k.substr (0, eq)] = k.substr (eq + 1); continue; }
            if (flags.count (k)) { r.opt[k] = "1"; continue; }
            if (i + 1 >= a.size()) throw Err ("option --" + k + " needs a value");
            r.opt[k] = a[++i];
        }
        else
            r.pos.push_back (t);
    }
    return r;
}

// ------------------------------------------------------------------ session (headless plugin state)
const char* const kMeta[] = { "kfName", "kfCategory", "kfScope", "kfVersion" };

const std::map<std::string, SubCurveId>& subCurveIds()
{
    static const std::map<std::string, SubCurveId> m = { { "pitch", SubCurveId::Pitch }, { "amp", SubCurveId::Amp },
        { "drive", SubCurveId::Drive }, { "harm", SubCurveId::HarmAmount }, { "keytrack", SubCurveId::KeyTrack } };
    return m;
}

struct Session
{
    KickforgeProcessor proc;

    Session()
    {
        proc.setPlayConfigDetails (0, 2, 48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
    }

    void loadXml (const juce::XmlElement& src)
    {
        if (! src.hasTagName (proc.apvts.state.getType())) throw Err ("not a Kickforge state file (root tag " + src.getTagName().toStdString() + ")");
        juce::XmlElement x (src);
        for (auto* a : kMeta) x.removeAttribute (a);
        proc.apvts.replaceState (juce::ValueTree::fromXml (x));
        proc.getClickLayer().loadState (proc.apvts.state.getChildWithName ("click"));
        proc.getSubCurves().loadFromState();
        proc.getClickLayer().waitForLoads (15000);
    }
    void loadFile (const juce::File& f)
    {
        if (! f.existsAsFile()) throw Err ("state file not found: " + f.getFullPathName().toStdString());
        auto x = juce::XmlDocument::parse (f);
        if (x == nullptr) throw Err ("state file is not valid XML: " + f.getFullPathName().toStdString());
        loadXml (*x);
    }
    std::unique_ptr<juce::XmlElement> toXml (const juce::String& name = "AI")
    {
        auto st = proc.apvts.copyState();
        st.removeChild (st.getChildWithName ("click"), nullptr);
        st.appendChild (proc.getClickLayer().saveState(), nullptr);
        auto x = st.createXml();
        x->setAttribute ("kfName", name);
        x->setAttribute ("kfCategory", "AI");
        x->setAttribute ("kfScope", "full");
        x->setAttribute ("kfVersion", 1);
        return x;
    }
    juce::String toText() { return toXml()->toString(); }

    juce::RangedAudioParameter* param (const juce::String& id)
    {
        auto* p = proc.apvts.getParameter (id);
        if (p == nullptr) throw Err ("unknown parameter: " + id.toStdString() + " (see `params --filter <prefix>`)");
        return p;
    }
    double value (const juce::String& id) { return (double) param (id)->convertFrom0to1 (param (id)->getValue()); }
    void setValue (const juce::String& id, double v)
    {
        auto* p = param (id);
        const auto r = p->getNormalisableRange();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) std::clamp (v, (double) r.start, (double) r.end)));
    }

    Curve getCurve (const std::string& name)
    {
        auto it = subCurveIds().find (name);
        if (it != subCurveIds().end()) return proc.getSubCurves().getCurve (it->second);
        const int k = clickIndex (name);
        if (k >= 0) return proc.getClickLayer().getEnvelope (k);
        throw Err ("unknown curve: " + name + " (pitch, amp, drive, harm, keytrack, click1..click4)");
    }
    void setCurve (const std::string& name, const Curve& c)
    {
        auto it = subCurveIds().find (name);
        if (it != subCurveIds().end()) { proc.getSubCurves().setCurve (it->second, c); return; }
        const int k = clickIndex (name);
        if (k < 0) throw Err ("unknown curve: " + name);
        proc.getClickLayer().setEnvelope (k, c);
    }
    static int clickIndex (const std::string& n)
    {
        if (n.size() == 6 && n.rfind ("click", 0) == 0 && n[5] >= '1' && n[5] < '1' + kNumClickLayers) return n[5] - '1';
        return -1;
    }
    double subLengthMs() { return value ("sub_length"); }
};

// ------------------------------------------------------------------ live folder
struct LiveFiles
{
    juce::File dir = liveDir();
    juce::File state() const { return dir.getChildFile ("state.kfpreset"); }
    juce::File reference() const { return dir.getChildFile ("reference.json"); }
    juce::File apply() const { return dir.getChildFile ("apply.kfpreset"); }
    juce::File status() const { return dir.getChildFile ("status.json"); }
};

juce::File stateFile (const Args& a, const LiveFiles& live) { return a.has ("state") ? juce::File (a.get ("state")) : live.state(); }

Session& loadSession (Session& s, const Args& a, const LiveFiles& live, bool allowMissing = false)
{
    const auto f = stateFile (a, live);
    if (! f.existsAsFile() && allowMissing) return s;
    if (! f.existsAsFile())
        throw Err ("no state file at " + f.getFullPathName().toStdString()
                   + " (enable Live link in the plugin, or pass --state <file.kfpreset>)");
    s.loadFile (f);
    return s;
}

// ------------------------------------------------------------------ reference handling
struct RefSpec
{
    juce::File file;
    int rs = -1, re = -1;
    int note = -1;
    bool fromJson = false;
};

RefSpec refSpec (const Args& a, const LiveFiles& live)
{
    RefSpec r;
    if (a.has ("ref"))
    {
        r.file = juce::File (a.get ("ref"));
        if (a.has ("region"))
        {
            auto p = juce::StringArray::fromTokens (a.get ("region"), ":", "");
            if (p.size() != 2) throw Err ("--region expects start:end in samples");
            r.rs = p[0].getIntValue();
            r.re = p[1].getIntValue();
        }
    }
    else
    {
        if (! live.reference().existsAsFile())
            throw Err ("no reference: pass --ref <wav> or load a Reference in the plugin (missing " + live.reference().getFullPathName().toStdString() + ")");
        auto v = juce::JSON::parse (live.reference());
        if (! v.isObject()) throw Err ("reference.json is not valid JSON");
        r.file = juce::File (v.getProperty ("path", "").toString());
        r.rs = (int) v.getProperty ("regionStart", 0);
        r.re = (int) v.getProperty ("regionEnd", 0);
        if (r.rs == 0 && r.re == 0) r.rs = -1;
        r.note = (int) v.getProperty ("note", -1);
        r.fromJson = true;
    }
    if (a.has ("note")) r.note = (int) a.num ("note", -1);
    if (! r.file.existsAsFile()) throw Err ("reference audio not found: " + r.file.getFullPathName().toStdString());
    return r;
}

struct RefCache
{
    RefSpec spec;
    std::vector<float> x;
    double sr = 48000.0;
    int start = 0;
    KickFeatures f;
    int regionStart = 0, regionEnd = 0;
};

RefCache loadRef (const Args& a, const LiveFiles& live)
{
    RefCache c;
    c.spec = refSpec (a, live);
    std::vector<float> mono;
    juce::String error;
    if (! loadAudioMono (c.spec.file, mono, c.sr, error)) throw Err ("cannot read reference: " + error.toStdString());
    int rs = c.spec.rs, re = c.spec.re;
    if (rs < 0)
    {
        auto hits = detectHits (mono.data(), (int) mono.size(), c.sr);
        const int h = strongestHit (hits);
        if (h < 0) throw Err ("reference is silent");
        rs = hits[(size_t) h].start;
        re = hits[(size_t) h].end;
    }
    if (re <= 0 || re > (int) mono.size()) re = (int) mono.size();
    rs = std::clamp (rs, 0, re);
    c.regionStart = rs;
    c.regionEnd = re;
    c.x.assign (mono.begin() + rs, mono.begin() + re);
    c.start = findOnset (c.x.data(), (int) c.x.size());
    c.f = analyseKick (c.x.data(), (int) c.x.size(), c.sr, c.start);
    if (! c.f.ok) throw Err ("reference region is silent or shorter than 10 ms");
    return c;
}

std::vector<float> renderSynth (Session& s, int note, double sr, double maxSeconds)
{
    RenderOptions o;
    o.note = note;
    o.sampleRate = sr;
    o.maxSeconds = maxSeconds;
    auto b = renderKick (s.proc, o);
    return std::vector<float> (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
}

struct Eval
{
    std::vector<float> syn;
    int synStart = 0;
    KickFeatures sf;
    Comparison c;
};

Eval evaluate (const RefCache& r, std::vector<float> syn, double subLengthMs)
{
    Eval e;
    e.syn = std::move (syn);
    const int on = findOnset (e.syn.data(), (int) e.syn.size());
    const int lag = alignLag (r.x.data(), (int) r.x.size(), r.start, e.syn.data(), (int) e.syn.size(), on, r.sr);
    e.synStart = std::max (0, on + lag);
    e.sf = analyseKick (e.syn.data(), (int) e.syn.size(), r.sr, e.synStart);
    HintContext hc;
    hc.subLengthMs = subLengthMs;
    e.c = compareKicks (r.f, e.sf, hc);
    return e;
}

double renderSeconds (const RefCache& r) { return std::clamp ((double) r.x.size() / r.sr * 1.5 + 0.3, 0.6, 6.0); }
int noteOf (Session& s, const RefCache& r) { return r.spec.note >= 0 ? r.spec.note : (int) std::lround (s.value ("sub_root")); }

Eval evalState (Session& s, const RefCache& r)
{
    return evaluate (r, renderSynth (s, noteOf (s, r), r.sr, renderSeconds (r)), s.subLengthMs());
}

// ------------------------------------------------------------------ feature / compare JSON
juce::var featuresJson (const KickFeatures& f, bool full)
{
    std::vector<double> st;
    for (auto h : f.pitchHz) st.push_back (h > 0 && f.tailHz > 0 ? 12.0 * std::log2 (h / f.tailHz) : std::nan (""));
    std::vector<double> hz;
    for (auto h : f.pitchHz) hz.push_back (h > 0 ? h : std::nan (""));
    Obj o;
    o.add ("length_ms", N (f.lengthMs, 0)).add ("peak_db", N (f.peakDb, 1)).add ("rms_db", N (f.rmsDb, 1))
        .add ("tail_hz", N (f.tailHz, 1)).add ("tail_note", S (noteName (f.tailHz)))
        .add ("transient_pct", N (f.transientPct, 1))
        .add ("t_ms", timesJson()).add ("pitch_hz", A (hz, 1)).add ("pitch_st_vs_tail", A (st, 2)).add ("env_db", A (f.envDb, 1))
        .add ("band_hz", A (kBandCentersHz, 1)).add ("band_early_db", A (f.bandEarlyDb, 1)).add ("band_late_db", A (f.bandLateDb, 1));
    if (full) o.add ("env_peak_db", N (f.envPeakDb, 1)).add ("start_sample", f.start);
    return o;
}

juce::var compareJson (const RefCache& r, const Eval& e, bool full)
{
    const auto& c = e.c;
    Obj parts;
    parts.add ("pitch", N (c.pitchScore, 0)).add ("envelope", N (c.envScore, 0)).add ("spectrum", N (c.spectrumScore, 0))
        .add ("tail", N (c.tailScore, 0)).add ("transient", N (c.transientScore, 0)).add ("length", N (c.lengthScore, 0));
    Obj o;
    o.add ("score", N (c.score, 1)).add ("parts", parts)
        .add ("ref", Obj().add ("file", S (r.spec.file.getFileName().toStdString())).add ("tail_hz", N (r.f.tailHz, 1))
                           .add ("tail_note", S (noteName (r.f.tailHz))).add ("length_ms", N (r.f.lengthMs, 0)).add ("sr", N (r.sr, 0)))
        .add ("synth", Obj().add ("tail_hz", N (e.sf.tailHz, 1)).add ("tail_note", S (noteName (e.sf.tailHz)))
                            .add ("length_ms", N (e.sf.lengthMs, 0)))
        .add ("t_ms", timesJson()).add ("pitch_err_st", A (c.pitchErrSt, 2)).add ("env_err_db", A (c.envErrDb, 1))
        .add ("band_hz", A (kBandCentersHz, 1)).add ("band_err_early_db", A (c.bandErrEarlyDb, 1)).add ("band_err_late_db", A (c.bandErrLateDb, 1))
        .add ("tail_err_cents", N (c.tailCents, 0)).add ("length_ratio", N (c.lengthRatio, 2))
        .add ("transient_diff_pct", N (c.transientDiffPct, 1))
        .add ("hints", strArr (c.hints));
    if (full)
        o.add ("loss", N (c.loss, 4)).add ("ref_features", featuresJson (r.f, true)).add ("synth_features", featuresJson (e.sf, true));
    return o;
}

// ------------------------------------------------------------------ parameters
juce::var paramRow (Session& s, juce::RangedAudioParameter* p, bool verbose)
{
    const auto r = p->getNormalisableRange();
    const double v = (double) p->convertFrom0to1 (p->getValue());
    const double d = (double) p->convertFrom0to1 (p->getDefaultValue());
    Obj o;
    o.add ("id", p->getParameterID().toStdString().c_str()).add ("value", N (v, 4));
    if (p->isDiscrete())
    {
        auto choices = p->getAllValueStrings();
        juce::Array<juce::var> ch;
        for (auto& c : choices) ch.add (c);
        o.add ("label", p->getText (p->getValue(), 32)).add ("default", N (d, 0));
        if (verbose || choices.size() <= 6) o.add ("choices", ch);
    }
    else
    {
        o.add ("default", N (d, 4)).add ("min", N (r.start, 4)).add ("max", N (r.end, 4));
        if (p->getLabel().isNotEmpty()) o.add ("unit", p->getLabel());
        if (verbose && r.interval > 0) o.add ("step", N (r.interval, 4));
    }
    if (verbose) o.add ("name", p->getName (64));
    juce::ignoreUnused (s);
    return o;
}

double parseParamValue (juce::RangedAudioParameter* p, const juce::String& text)
{
    const auto t = text.trim();
    if (p->isDiscrete())
    {
        auto choices = p->getAllValueStrings();
        for (int i = 0; i < choices.size(); ++i)
            if (choices[i].equalsIgnoreCase (t)) return i;
        if (t.equalsIgnoreCase ("true") || t.equalsIgnoreCase ("on")) return 1;
        if (t.equalsIgnoreCase ("false") || t.equalsIgnoreCase ("off")) return 0;
        if (t.containsOnly ("0123456789-")) return t.getIntValue();
        throw Err ("bad value '" + t.toStdString() + "' for " + p->getParameterID().toStdString() + ", choices: " + choices.joinIntoString (", ").toStdString());
    }
    if (t.equalsIgnoreCase ("off") && p->getParameterID().endsWith ("_gain")) return p->getNormalisableRange().start;
    if (! t.containsOnly ("0123456789+-.eE")) throw Err ("not a number: " + t.toStdString());
    return t.getDoubleValue();
}

juce::var curveJson (Session& s, const std::string& name)
{
    const auto c = s.getCurve (name);
    juce::Array<juce::var> pts;
    for (auto& p : c.getPoints()) pts.add (Obj().add ("t", N (p.time, 4)).add ("v", N (p.value, 4)).add ("tension", N (p.tension, 3)));
    std::string unit = "linear 0..1";
    if (name == "pitch") unit = s.value ("sub_pitchmode") > 0.5 ? "Hz" : "semitones relative to the base pitch";
    return Obj().add ("curve", S (name)).add ("time_unit", "normalized 0..1 over sub_length (click1..4: over the click layer length)")
        .add ("value_unit", S (unit)).add ("sub_length_ms", N (s.subLengthMs(), 0)).add ("points", pts);
}

Curve curveFromVar (const juce::var& v)
{
    juce::var pts = v;
    if (v.isObject()) pts = v.getProperty ("points", {});
    if (! pts.isArray() || pts.size() < 2) throw Err ("curve needs {\"points\":[{t,v,tension},...]} with at least 2 points");
    std::vector<Curve::Point> out;
    for (auto& p : *pts.getArray())
    {
        Curve::Point q;
        q.time = std::clamp ((double) (p.hasProperty ("t") ? p.getProperty ("t", 0.0) : p.getProperty ("time", 0.0)), 0.0, 1.0);
        q.value = (double) (p.hasProperty ("v") ? p.getProperty ("v", 0.0) : p.getProperty ("value", 0.0));
        q.tension = std::clamp ((double) p.getProperty ("tension", 0.0), -1.0, 1.0);
        out.push_back (q);
    }
    return Curve (out);
}

// ------------------------------------------------------------------ apply (live folder)
int readCounter (const LiveFiles& live)
{
    auto v = juce::JSON::parse (live.status());
    return v.isObject() ? (int) v.getProperty ("applied", 0) : -1;
}

juce::var pushApply (Session& s, const LiveFiles& live, double timeoutS)
{
    const int before = readCounter (live);
    const bool pluginSeen = live.status().existsAsFile();
    if (! writeFileAtomic (live.apply(), s.toText())) throw Err ("cannot write " + live.apply().getFullPathName().toStdString());
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    while ((juce::Time::getMillisecondCounterHiRes() - t0) < timeoutS * 1000.0)
    {
        const int now = readCounter (live);
        if (now > before && now >= 0)
        {
            auto st = juce::JSON::parse (live.status());
            return Obj().add ("applied", true).add ("counter", now).add ("lastApplyOk", st.getProperty ("lastApplyOk", true))
                .add ("error", st.getProperty ("error", ""));
        }
        juce::Thread::sleep (50);
    }
    Obj o;
    o.add ("applied", false).add ("timeout_s", N (timeoutS, 1));
    if (! pluginSeen && ! live.status().existsAsFile())
    {
        live.apply().deleteFile(); // nobody is listening: do not leave a stale state for a later session
        o.add ("note", "no plugin is linked (status.json missing) - nothing was applied; enable Live link in Kickforge and retry");
    }
    else
        o.add ("note", "plugin did not confirm in time; apply.kfpreset is left in place, check `status` (plugin busy, link off, or another instance linked)");
    return o;
}

/** Shared tail for set / curve set / fit / import: honour --out and --apply. */
void finishWrite (Session& s, const Args& a, const LiveFiles& live, Obj& result)
{
    if (a.has ("out"))
    {
        const juce::File f (a.get ("out"));
        if (! writeFileAtomic (f, s.toText())) throw Err ("cannot write " + f.getFullPathName().toStdString());
        result.add ("written", f.getFullPathName());
    }
    if (a.has ("apply"))
        result.add ("apply", pushApply (s, live, a.num ("timeout", 3.0)));
    else if (! a.has ("out"))
        result.add ("note", "dry run: nothing written. Use --out <file> to save and/or --apply to push to the plugin");
}

// ------------------------------------------------------------------ plot
struct Panel
{
    juce::Rectangle<float> r;
    double x0, x1, y0, y1;
    bool logY = false, logX = false;
    float X (double x) const
    {
        const double u = logX ? std::log (x / x0) / std::log (x1 / x0) : (x - x0) / (x1 - x0);
        return r.getX() + (float) std::clamp (u, 0.0, 1.0) * r.getWidth();
    }
    float Y (double y) const
    {
        const double u = logY ? std::log (y / y0) / std::log (y1 / y0) : (y - y0) / (y1 - y0);
        return r.getBottom() - (float) std::clamp (u, 0.0, 1.0) * r.getHeight();
    }
};

const juce::Colour kBg (0xfffbfbfc), kGrid (0xffdcdee3), kText (0xff1d2025), kRef (0xff1f6feb), kSyn (0xffe8590c);

void drawAxes (juce::Graphics& g, const Panel& p, const juce::String& title, const juce::String& xl, const juce::String& yl,
               const std::vector<double>& xt, const std::vector<double>& yt, int yDecimals = 0)
{
    g.setColour (kGrid.withAlpha (0.35f));
    g.fillRect (p.r);
    g.setFont (juce::Font (juce::FontOptions (15.0f)));
    for (double x : xt)
    {
        const float px = p.X (x);
        g.setColour (kGrid);
        g.drawLine (px, p.r.getY(), px, p.r.getBottom(), 1.0f);
        g.setColour (kText);
        g.drawText (juce::String (x >= 1000 ? juce::String (x / 1000.0, x >= 10000 ? 0 : 1) + "k" : juce::String (x, 0)), (int) px - 30, (int) p.r.getBottom() + 3, 60, 18, juce::Justification::centred);
    }
    for (double y : yt)
    {
        const float py = p.Y (y);
        g.setColour (kGrid);
        g.drawLine (p.r.getX(), py, p.r.getRight(), py, 1.0f);
        g.setColour (kText);
        g.drawText (juce::String (y, yDecimals), (int) p.r.getX() - 52, (int) py - 9, 48, 18, juce::Justification::centredRight);
    }
    g.setColour (kText);
    g.drawRect (p.r, 1.0f);
    g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    g.drawText (title + "   [" + yl + "]", (int) p.r.getX(), (int) p.r.getY() - 26, (int) p.r.getWidth(), 22, juce::Justification::centredLeft);
    g.setFont (juce::Font (juce::FontOptions (14.0f)));
    g.drawText (xl, (int) p.r.getX(), (int) p.r.getBottom() + 20, (int) p.r.getWidth(), 18, juce::Justification::centred);

}

void polyline (juce::Graphics& g, const Panel& p, const std::vector<std::pair<double, double>>& pts, juce::Colour c, float w, bool dots = false)
{
    g.setColour (c);
    juce::Path path;
    bool pen = false;
    for (auto& q : pts)
    {
        if (! std::isfinite (q.second)) { pen = false; continue; }
        const float x = p.X (q.first), y = p.Y (q.second);
        if (! pen) { path.startNewSubPath (x, y); pen = true; }
        else path.lineTo (x, y);
        if (dots) g.fillEllipse (x - 3, y - 3, 6, 6);
    }
    g.strokePath (path, juce::PathStrokeType (w, juce::PathStrokeType::curved));
}

void waveEnvelope (juce::Graphics& g, const Panel& p, const float* x, int n, int start, double sr, juce::Colour c)
{
    // per-pixel min/max drawn as vertical strokes (never aliases away the waveform)
    const int w = (int) p.r.getWidth();
    g.setColour (c);
    juce::Path top, bot;
    for (int px = 0; px < w; ++px)
    {
        const double t0 = p.x0 + (p.x1 - p.x0) * px / w, t1 = p.x0 + (p.x1 - p.x0) * (px + 1) / w;
        int i0 = start + (int) (t0 * sr / 1000.0), i1 = std::max (i0 + 1, start + (int) (t1 * sr / 1000.0));
        if (i0 >= n) break;
        float lo = 1e9f, hi = -1e9f;
        for (int i = i0; i < std::min (i1, n); ++i) { lo = std::min (lo, x[i]); hi = std::max (hi, x[i]); }
        const float X = p.r.getX() + (float) px;
        if (px == 0) { top.startNewSubPath (X, p.Y (hi)); bot.startNewSubPath (X, p.Y (lo)); }
        else { top.lineTo (X, p.Y (hi)); bot.lineTo (X, p.Y (lo)); }
    }
    g.strokePath (top, juce::PathStrokeType (1.4f));
    g.strokePath (bot, juce::PathStrokeType (1.4f));
}

void drawLegend (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    g.setColour (kRef);
    g.fillRect (r.getX(), r.getY() + 6, 26, 5);
    g.drawText ("reference", r.getX() + 32, r.getY(), 90, 18, juce::Justification::centredLeft);
    g.setColour (kSyn);
    g.fillRect (r.getX() + 130, r.getY() + 6, 26, 5);
    g.drawText ("synth", r.getX() + 162, r.getY(), 70, 18, juce::Justification::centredLeft);
}

bool drawPlot (const RefCache& r, const Eval& e, const juce::File& out)
{
    constexpr int W = 1200, H = 800;
    juce::Image img (juce::Image::ARGB, W, H, true, juce::SoftwareImageType());
    juce::Graphics g (img);
    g.fillAll (kBg);

    g.setColour (kText);
    g.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    g.drawText (juce::String ("Kickforge: reference vs synth   score ") + juce::String (e.c.score, 1) + "/100   tail "
                    + juce::String (r.f.tailHz, 1) + " Hz (ref) vs " + juce::String (e.sf.tailHz, 1) + " Hz (synth)",
                14, 6, W - 28, 26, juce::Justification::centredLeft);
    drawLegend (g, { W - 250, 10, 240, 20 });

    const float mx = 70.0f, mt = 62.0f;
    const float pw = (W - 2 * mx - 40.0f) / 2.0f, ph = (H - mt - 2 * 60.0f - 20.0f) / 2.0f;
    auto rect = [&] (int col, int row) {
        return juce::Rectangle<float> (mx + col * (pw + 40.0f), mt + 20.0f + row * (ph + 70.0f), pw, ph);
    };
    Tracker tr (r.x.data(), (int) r.x.size(), r.sr, r.start), ts (e.syn.data(), (int) e.syn.size(), r.sr, e.synStart);
    const float* rx = r.x.data();
    const float* sx = e.syn.data();

    // 1: waveform
    {
        float pk = 0.01f;
        for (int i = r.start; i < std::min ((int) r.x.size(), r.start + (int) (0.3 * r.sr)); ++i) pk = std::max (pk, std::abs (rx[i]));
        for (int i = e.synStart; i < std::min ((int) e.syn.size(), e.synStart + (int) (0.3 * r.sr)); ++i) pk = std::max (pk, std::abs (sx[i]));
        Panel p { rect (0, 0), 0, 300, -pk * 1.08, pk * 1.08 };
        drawAxes (g, p, "Waveform (first 300 ms, aligned at onset)", "time (ms)", "amplitude",
                  { 0, 50, 100, 150, 200, 250, 300 }, { -pk, 0.0, pk }, 2);
        waveEnvelope (g, p, rx, (int) r.x.size(), r.start, r.sr, kRef);
        waveEnvelope (g, p, sx, (int) e.syn.size(), e.synStart, r.sr, kSyn.withAlpha (0.85f));
    }
    // 2: pitch track
    {
        Panel p { rect (1, 0), 0, 400, 25, 400 };
        p.logY = true;
        drawAxes (g, p, "Fundamental pitch track", "time (ms)", "Hz (log)", { 0, 50, 100, 200, 300, 400 }, { 25, 40, 60, 100, 150, 250, 400 });
        std::vector<std::pair<double, double>> a, b;
        for (double t = 0; t <= 400.0; t += (t < 60 ? 3.0 : 10.0))
        {
            const double h1 = tr.hzAt (t), h2 = ts.hzAt (t);
            a.push_back ({ t, h1 > 0 ? std::clamp (h1, 25.0, 400.0) : std::nan ("") });
            b.push_back ({ t, h2 > 0 ? std::clamp (h2, 25.0, 400.0) : std::nan ("") });
        }
        polyline (g, p, a, kRef, 2.4f, true);
        polyline (g, p, b, kSyn, 2.4f, true);
    }
    // 3: envelope
    {
        Panel p { rect (0, 1), 0, 800, -80, 0 };
        drawAxes (g, p, "Amplitude envelope (windowed RMS)", "time (ms)", "dBFS", { 0, 100, 200, 300, 400, 500, 600, 700, 800 }, { -80, -60, -40, -20, 0 });
        std::vector<std::pair<double, double>> a, b;
        for (double t = 0; t <= 800.0; t += (t < 60 ? 2.0 : 8.0))
        {
            a.push_back ({ t, std::max (-80.0, tr.rmsDbAt (t)) });
            b.push_back ({ t, std::max (-80.0, ts.rmsDbAt (t)) });
        }
        polyline (g, p, a, kRef, 2.4f);
        polyline (g, p, b, kSyn, 2.4f);
    }
    // 4: spectrum
    {
        const int pts = 260;
        auto sa = logSpectrumDb (rx, (int) r.x.size(), r.sr, r.start, 400.0, pts, 20.0, 20000.0);
        auto sb = logSpectrumDb (sx, (int) e.syn.size(), r.sr, e.synStart, 400.0, pts, 20.0, 20000.0);
        double top = -200;
        for (int i = 0; i < pts; ++i) top = std::max ({ top, sa[(size_t) i], sb[(size_t) i] });
        top = std::ceil (top / 10.0) * 10.0;
        Panel p { rect (1, 1), 20, 20000, top - 90, top };
        p.logX = true;
        std::vector<double> yt;
        for (double y = top - 90; y <= top + 0.1; y += 15) yt.push_back (y);
        drawAxes (g, p, "Spectrum (first 400 ms)", "frequency (Hz, log)", "dB (relative)", { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 }, yt);
        std::vector<std::pair<double, double>> a, b;
        for (int i = 0; i < pts; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, (double) i / (pts - 1));
            a.push_back ({ f, sa[(size_t) i] });
            b.push_back ({ f, sb[(size_t) i] });
        }
        polyline (g, p, a, kRef, 2.2f);
        polyline (g, p, b, kSyn, 2.2f);
    }

    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream os (out);
    if (os.failedToOpen()) return false;
    juce::PNGImageFormat png;
    return png.writeImageToStream (img, os);
}

// ------------------------------------------------------------------ fit
struct Var
{
    std::string label;
    double lo = 0, hi = 1, step = 0.1, minStep = 0.001;
    std::function<double()> get;
    std::function<void (double)> set;
};

std::vector<std::string> splitList (const juce::String& s)
{
    std::vector<std::string> out;
    for (auto& t : juce::StringArray::fromTokens (s, ",; ", "")) if (t.isNotEmpty()) out.push_back (t.toStdString());
    return out;
}

Var paramVar (Session& s, const std::string& id)
{
    auto* p = s.param (id);
    if (p->isDiscrete()) throw Err ("fit --params: " + id + " is a switch/choice, only continuous parameters can be fitted");
    const auto r = p->getNormalisableRange();
    const double init = s.value (id);
    Var v;
    v.label = id;
    v.lo = r.start; v.hi = r.end;
    v.step = 0.1 * (r.end - r.start);
    if (id == "sub_tune")   { v.lo = std::max<double> (r.start, init - 12); v.hi = std::min<double> (r.end, init + 12); v.step = 0.5; }
    else if (id == "sub_fine")   { v.step = 15; }
    else if (id == "sub_length") { v.lo = std::max<double> (r.start, init * 0.4); v.hi = std::min<double> (r.end, init * 2.5); v.step = std::max (5.0, 0.12 * init); }
    else if (id == "sub_gain")   { v.lo = std::max<double> (r.start, init - 12); v.hi = std::min<double> (r.end, init + 12); v.step = 1.5; }
    v.minStep = (v.hi - v.lo) * 0.002;
    v.get = [&s, id] { return s.value (id); };
    v.set = [&s, id] (double x) { s.setValue (id, x); };
    return v;
}

void curveVars (Session& s, const std::string& name, std::vector<Var>& out)
{
    const auto c = s.getCurve (name);
    const bool hz = name == "pitch" && s.value ("sub_pitchmode") > 0.5;
    for (int i = 0; i < c.size(); ++i)
    {
        const auto pt = c.getPoints()[(size_t) i];
        auto mk = [&] (bool tension) {
            Var v;
            v.label = name + "[" + std::to_string (i) + "]." + (tension ? "tension" : "v");
            if (tension)
            {
                if (i == c.size() - 1) return; // unused on the last point
                v.lo = -1; v.hi = 1; v.step = 0.25;
            }
            else if (name == "pitch" && ! hz) { v.lo = pt.value - 12; v.hi = pt.value + 12; v.step = 1.5; }
            else if (hz) { v.lo = std::max (20.0, pt.value * 0.5); v.hi = pt.value * 2.0; v.step = std::max (2.0, pt.value * 0.1); }
            else { v.lo = 0; v.hi = 1; v.step = 0.1; }
            v.minStep = (v.hi - v.lo) * 0.002;
            auto* sp = &s;
            v.get = [sp, name, i, tension] {
                const auto cc = sp->getCurve (name);
                return tension ? cc.getPoints()[(size_t) i].tension : cc.getPoints()[(size_t) i].value;
            };
            v.set = [sp, name, i, tension] (double x) {
                auto pts = sp->getCurve (name).getPoints();
                if (tension) pts[(size_t) i].tension = x; else pts[(size_t) i].value = x;
                sp->setCurve (name, Curve (pts));
            };
            out.push_back (std::move (v));
        };
        mk (false);
        mk (true);
    }
}

// ------------------------------------------------------------------ help
const std::map<std::string, std::string>& helpTexts()
{
    static const std::map<std::string, std::string> m = {
        { "status", "status\n  Live folder, linked plugin instance (status.json), reference.json and state.kfpreset age.\n" },
        { "params", "params [--filter prefix] [--changed] [--full] [--state f]\n  Every parameter: id, value (real units), default, min, max, unit, choices.\n  No filter = summary of prefixes + changed values. Prefixes: glob_ sub_ click1_.. fx_ trig_ ...\n" },
        { "get", "get <id>... [--state f]\n  Read parameter values (real units; choices return the index and a label).\n" },
        { "set", "set <id>=<value>... [--out file] [--apply] [--state f] [--timeout s]\n  Set parameters (real units; choices by index or label; bool 0/1/on/off).\n  Nothing is written unless --out and/or --apply is given. --apply pushes to the plugin and waits for confirmation.\n" },
        { "curve", "curve get <name> [--state f]\ncurve set <name> <json|@file> [--out file] [--apply]\n  Names: pitch amp drive harm keytrack click1..click4. JSON: {\"points\":[{\"t\":0,\"v\":31,\"tension\":-0.4},...]}\n  t is normalized 0..1 over sub_length; pitch v is semitones (or Hz when sub_pitchmode=Hz); amp v is linear 0..1.\n  Tension -1..1 shapes the segment leaving the point (<0 fast start/slow end, >0 slow start/fast end).\n" },
        { "render", "render [--note n] [--sr hz] [--out wav] [--state f]\n  Offline render of the state with the plugin engine (vel 1.0, default 48 kHz, note = sub_root). JSON summary of the result.\n" },
        { "analyze", "analyze <wav> [--region a:b] [--full]\n  Features of a kick: length, peak/rms, tail Hz + note, fundamental track (pitch_hz, pitch_st_vs_tail) and envelope (env_db)\n  at fixed t_ms = 0,2,5,10,20,35,50,75,100,150,200,300,400,600,800 after onset, octave band levels (early 0-50 ms / late 50-350 ms),\n  transient_pct (share of the first 400 ms energy in the first 10 ms). Region = samples; default = strongest hit.\n" },
        { "compare", "compare [--ref wav] [--region a:b] [--note n] [--state f] [--full]\n  Renders the state at the reference's sample rate, aligns both at onset (cross-correlation of the first 30 ms, +-3 ms),\n  and returns errors (synth - ref): pitch_err_st and env_err_db per t_ms, band_err_*_db per octave band, tail_err_cents,\n  length_ratio, transient_diff_pct, plus plain-language hints naming the params/curves to change.\n  SCORE 0..100 = weighted mean of part scores, each 100*exp(-meanError/scale):\n    pitch 30%  (mean |st| error while the reference is within 40 dB of its peak; scale 1.5 st)\n    envelope 25% (mean |dB| error at the sample times; scale 4 dB)\n    spectrum 20% (mean |dB| error of octave bands early+late, bands within 50 dB of the loudest; scale 5 dB)\n    tail 10%   (tail fundamental error in semitones; scale 0.5 st)\n    transient 10% (difference in first-10-ms energy share, percentage points; scale 8)\n    length 5%  (|ln(length ratio)| to -50 dB; scale 0.3)\n  100 = identical features. --full adds both feature sets and the optimiser loss.\n" },
        { "plot", "plot [--ref wav] --out png [--state f]\n  1200x800 PNG: waveform overlay (first 300 ms), pitch track, envelope dB, spectrum. Reference = blue, synth = orange.\n" },
        { "import", "import [--ref wav] [--region a:b] [--out file] [--apply]\n  First-pass fit using the importer analysis: sub params, pitch/amp curves, harmonics and a top-end click sample.\n  Starts from --state (or the Init state if no state file exists).\n" },
        { "fit", "fit [--ref wav] [--params id,id] [--curves pitch,amp] [--iters 200] [--out file] [--apply]\n  Bounded coordinate search on the compare loss. Default params: sub_tune,sub_fine,sub_length,sub_gain; default curves: pitch,amp\n  (every point value and tension). Budget = --iters renders (default 200). Prints before/after score and changed values.\n  Never touches the live state unless --apply.\n" },
        { "apply", "apply [--state f] [--timeout 3]\n  Writes Live/apply.kfpreset (via .tmp + rename) and waits for status.json `applied` to increase.\n  {\"applied\":true} on confirmation; {\"applied\":false,...} if no plugin answers (not an error).\n" },
    };
    return m;
}

std::string helpAll()
{
    return std::string (kVersion) + "\nUsage: kickforge-cli <command> [options]   (JSON on stdout; --text pretty-prints; errors: {\"error\":...} + exit code 1)\n"
        "Commands: status params get set curve render analyze compare plot import fit apply help\n"
        "Common: --state <file> (default Live/state.kfpreset)   --ref <wav> (default Live/reference.json)   --full (more output)\n"
        "Live dir: %APPDATA%\\Kickforge\\Live or env KICKFORGE_LIVE_DIR. `kickforge-cli help <command>` for details.\n";
}

// ------------------------------------------------------------------ commands
juce::var cmdStatus (const Args&, const LiveFiles& live)
{
    Obj o;
    o.add ("live_dir", live.dir.getFullPathName()).add ("live_dir_exists", live.dir.isDirectory());
    Obj st;
    st.add ("path", live.state().getFullPathName()).add ("exists", live.state().existsAsFile());
    if (live.state().existsAsFile()) st.add ("age_s", N ((juce::Time::getCurrentTime() - live.state().getLastModificationTime()).inSeconds(), 1));
    o.add ("state", st);
    auto ref = juce::JSON::parse (live.reference());
    o.add ("reference", ref.isObject() ? ref : juce::var());
    auto stat = juce::JSON::parse (live.status());
    o.add ("plugin", stat.isObject() ? stat : juce::var());
    o.add ("linked", stat.isObject());
    o.add ("apply_pending", live.apply().existsAsFile());
    return o;
}

juce::var cmdParams (Session& s, const Args& a)
{
    const auto filter = a.get ("filter");
    const bool changed = a.has ("changed"), full = a.has ("full");
    juce::Array<juce::var> rows;
    std::map<std::string, int> groups;
    int total = 0, nChanged = 0;
    bool truncated = false;
    for (auto* base : s.proc.getParameters())
    {
        auto* p = dynamic_cast<juce::RangedAudioParameter*> (base);
        if (p == nullptr) continue;
        const auto id = p->getParameterID();
        ++total;
        const auto dot = id.indexOfChar ('_');
        ++groups[(dot > 0 ? id.substring (0, dot + 1) : id).toStdString()];
        const bool isChanged = std::abs (p->getValue() - p->getDefaultValue()) > 1.0e-6f;
        nChanged += isChanged ? 1 : 0;
        if (filter.isNotEmpty() ? ! id.startsWith (filter) : (! changed && ! full)) continue;
        if (changed && ! isChanged) continue;
        if (! full && rows.size() >= 60) { truncated = true; continue; }
        rows.add (paramRow (s, p, full));
    }
    Obj o;
    o.add ("total", total).add ("changed_from_default", nChanged);
    if (filter.isEmpty() && ! changed && ! full)
    {
        Obj g;
        for (auto& kv : groups) g.add (kv.first.c_str(), kv.second);
        o.add ("groups", g).add ("hint", "use --filter <prefix> for details (e.g. sub_), --changed for non-default values, --full for everything");
        return o;
    }
    o.add ("params", rows);
    if (truncated) o.add ("truncated", true).add ("hint", "more rows exist: narrow --filter or use --full");
    return o;
}

juce::var cmdGet (Session& s, const Args& a)
{
    if (a.pos.size() < 2) throw Err ("usage: get <id>...");
    Obj v, labels;
    for (size_t i = 1; i < a.pos.size(); ++i)
    {
        auto* p = s.param (a.pos[i]);
        v.add (a.pos[i].c_str(), N (s.value (a.pos[i]), 4));
        if (p->isDiscrete()) labels.add (a.pos[i].c_str(), p->getText (p->getValue(), 32));
    }
    Obj o;
    o.add ("values", v);
    if (labels.p->getProperties().size() > 0) o.add ("labels", labels);
    return o;
}

juce::var cmdSet (Session& s, const Args& a, const LiveFiles& live)
{
    if (a.pos.size() < 2) throw Err ("usage: set <id>=<value>... [--out f] [--apply]");
    Obj changed;
    for (size_t i = 1; i < a.pos.size(); ++i)
    {
        const auto eq = a.pos[i].find ('=');
        if (eq == std::string::npos) throw Err ("expected id=value, got '" + a.pos[i] + "'");
        const auto id = a.pos[i].substr (0, eq);
        auto* p = s.param (id);
        s.setValue (id, parseParamValue (p, juce::String::fromUTF8 (a.pos[i].c_str() + eq + 1)));
        changed.add (id.c_str(), N (s.value (id), 4));
    }
    Obj o;
    o.add ("set", changed);
    finishWrite (s, a, live, o);
    return o;
}

juce::var cmdCurve (Session& s, const Args& a, const LiveFiles& live)
{
    if (a.pos.size() < 3) throw Err ("usage: curve get <name> | curve set <name> <json|@file>");
    if (a.pos[1] == "get") return curveJson (s, a.pos[2]);
    if (a.pos[1] != "set") throw Err ("curve: expected get or set");
    if (a.pos.size() < 4) throw Err ("curve set needs a JSON argument or @file");
    juce::String txt = juce::String::fromUTF8 (a.pos[3].c_str());
    if (txt.startsWith ("@"))
    {
        juce::File f (txt.substring (1));
        if (! f.existsAsFile()) throw Err ("curve file not found: " + f.getFullPathName().toStdString());
        txt = f.loadFileAsString();
    }
    juce::var v;
    if (juce::JSON::parse (txt, v).failed()) throw Err ("curve JSON does not parse");
    s.setCurve (a.pos[2], curveFromVar (v));
    Obj o;
    o.add ("curve", curveJson (s, a.pos[2]));
    finishWrite (s, a, live, o);
    return o;
}

juce::var cmdRender (Session& s, const Args& a)
{
    const double sr = a.num ("sr", 48000.0);
    const int note = a.has ("note") ? (int) a.num ("note", 33) : (int) std::lround (s.value ("sub_root"));
    auto syn = renderSynth (s, note, sr, a.num ("seconds", 4.0));
    const int on = findOnset (syn.data(), (int) syn.size());
    auto f = analyseKick (syn.data(), (int) syn.size(), sr, on);
    Obj o;
    o.add ("note", note).add ("sr", N (sr, 0)).add ("samples", (int) syn.size()).add ("onset_sample", on);
    if (a.has ("out"))
    {
        juce::AudioBuffer<float> b (1, (int) syn.size());
        b.copyFrom (0, 0, syn.data(), (int) syn.size());
        const juce::File out (a.get ("out"));
        if (! writeWav24 (out, b, sr)) throw Err ("cannot write " + out.getFullPathName().toStdString());
        o.add ("wav", out.getFullPathName());
    }
    o.add ("features", featuresJson (f, a.has ("full")));
    return o;
}

juce::var cmdAnalyze (const Args& a)
{
    if (a.pos.size() < 2) throw Err ("usage: analyze <wav> [--region a:b]");
    Args b = a;
    b.opt["ref"] = a.pos[1];
    LiveFiles live;
    auto r = loadRef (b, live);
    Obj o;
    o.add ("file", r.spec.file.getFileName()).add ("sr", N (r.sr, 0)).add ("region", Obj().add ("start", r.regionStart).add ("end", r.regionEnd))
        .add ("onset_ms", N (1000.0 * r.start / r.sr, 2)).add ("features", featuresJson (r.f, a.has ("full")));
    return o;
}

juce::var cmdCompare (Session& s, const Args& a, const LiveFiles& live)
{
    auto r = loadRef (a, live);
    auto e = evalState (s, r);
    return compareJson (r, e, a.has ("full"));
}

juce::var cmdPlot (Session& s, const Args& a, const LiveFiles& live)
{
    if (! a.has ("out")) throw Err ("usage: plot [--ref wav] --out file.png");
    auto r = loadRef (a, live);
    auto e = evalState (s, r);
    const juce::File out (a.get ("out"));
    if (! drawPlot (r, e, out)) throw Err ("cannot write " + out.getFullPathName().toStdString());
    return Obj().add ("png", out.getFullPathName()).add ("width", 1200).add ("height", 800).add ("score", N (e.c.score, 1))
        .add ("legend", "blue = reference, orange = synth").add ("hints", strArr (e.c.hints));
}

juce::var cmdImport (Session& s, const Args& a, const LiveFiles& live)
{
    auto spec = refSpec (a, live);
    ImportOptions io;
    io.regionStart = spec.rs;
    io.regionEnd = spec.re;
    double a4 = s.value ("glob_a4");
    auto sum = analyseFile (spec.file, io, a4);
    if (! sum.ok) throw Err ("import failed: " + sum.message.toStdString());
    applyImport (s.proc, sum, io);
    s.proc.getClickLayer().waitForLoads (15000);
    Obj o;
    o.add ("imported", true).add ("message", sum.message).add ("tail_hz", N (sum.result.tailHz, 1)).add ("start_hz", N (sum.result.startHz, 1))
        .add ("sub_length_ms", N (sum.result.sub.lengthMs, 0)).add ("harmonics", sum.result.numHarmonicsUsed)
        .add ("top_end_sample", sum.topEndFile.getFullPathName());
    finishWrite (s, a, live, o);
    return o;
}

juce::var cmdFit (Session& s, const Args& a, const LiveFiles& live)
{
    auto r = loadRef (a, live);
    const int budget = std::max (2, (int) a.num ("iters", 200));
    std::vector<Var> vars;
    const auto pids = a.has ("params") ? splitList (a.get ("params")) : std::vector<std::string> { "sub_tune", "sub_fine", "sub_length", "sub_gain" };
    for (auto& id : pids) vars.push_back (paramVar (s, id));
    for (auto& cn : a.has ("curves") ? splitList (a.get ("curves")) : std::vector<std::string> { "pitch", "amp" })
        curveVars (s, cn, vars);
    if (vars.empty()) throw Err ("nothing to fit (empty --params and --curves)");

    int renders = 0;
    auto loss = [&] { ++renders; return evalState (s, r); };
    const auto initState = s.toText();
    auto before = loss();

    std::vector<double> x, init, step;
    for (auto& v : vars) { x.push_back (v.get()); init.push_back (v.get()); step.push_back (v.step); }
    double best = before.c.loss;
    Eval bestEval = before;
    while (renders < budget)
    {
        bool anyBig = false, improvedAny = false;
        for (size_t i = 0; i < vars.size() && renders < budget; ++i)
        {
            bool improved = false;
            for (int dir : { +1, -1 })
            {
                if (renders >= budget) break;
                const double cand = std::clamp (x[i] + dir * step[i], vars[i].lo, vars[i].hi);
                if (std::abs (cand - x[i]) < 1.0e-9) continue;
                vars[i].set (cand);
                auto ev = loss();
                if (ev.c.loss < best - 1.0e-6)
                {
                    best = ev.c.loss;
                    bestEval = std::move (ev);
                    x[i] = cand;
                    step[i] = std::min (step[i] * 1.6, (vars[i].hi - vars[i].lo) * 0.5);
                    improved = true;
                    break;
                }
                vars[i].set (x[i]);
            }
            if (! improved) step[i] *= 0.5;
            improvedAny = improvedAny || improved;
            anyBig = anyBig || step[i] > vars[i].minStep;
        }
        if (! anyBig && ! improvedAny) break;
    }
    for (size_t i = 0; i < vars.size(); ++i) vars[i].set (x[i]);

    juce::Array<juce::var> changed;
    for (size_t i = 0; i < vars.size() && changed.size() < 40; ++i)
        if (std::abs (x[i] - init[i]) > 1.0e-6)
            changed.add (Obj().add ("id", S (vars[i].label)).add ("from", N (init[i], 4)).add ("to", N (x[i], 4)));
    juce::ignoreUnused (initState);
    Obj o;
    o.add ("renders", renders).add ("budget", budget)
        .add ("before", Obj().add ("score", N (before.c.score, 1)).add ("loss", N (before.c.loss, 4)))
        .add ("after", Obj().add ("score", N (bestEval.c.score, 1)).add ("loss", N (bestEval.c.loss, 4)))
        .add ("changed", changed).add ("hints_after", strArr (bestEval.c.hints));
    finishWrite (s, a, live, o);
    return o;
}

juce::var cmdApply (Session& s, const Args& a, const LiveFiles& live)
{
    loadSession (s, a, live); // validates the file is a real state before pushing it
    return pushApply (s, live, a.num ("timeout", 3.0));
}
} // namespace

// ------------------------------------------------------------------ entry
std::string run (const std::vector<std::string>& argv, int& exitCode)
{
    exitCode = 0;
    try
    {
        if (argv.empty()) return helpAll();
        if (argv[0] == "--version" || argv[0] == "version") return kVersion;
        if (argv[0] == "help" || argv[0] == "--help" || argv[0] == "-h")
        {
            if (argv.size() > 1)
            {
                auto it = helpTexts().find (argv[1]);
                if (it == helpTexts().end()) throw Err ("no help for '" + argv[1] + "'");
                return it->second;
            }
            return helpAll();
        }
        const Args a = parseArgs (argv, 0);
        if (a.pos.empty()) return helpAll();
        const std::string cmd = a.pos[0];
        if (a.has ("help")) { auto it = helpTexts().find (cmd); return it == helpTexts().end() ? helpAll() : it->second; }
        LiveFiles live;
        juce::var result;
        if (cmd == "status") result = cmdStatus (a, live);
        else if (cmd == "analyze") result = cmdAnalyze (a);
        else
        {
            static const std::set<std::string> known = { "params", "get", "set", "curve", "render", "compare", "plot", "import", "fit", "apply" };
            if (! known.count (cmd)) throw Err ("unknown command '" + cmd + "' (try `kickforge-cli help`)");
            auto s = std::make_unique<Session>();
            loadSession (*s, a, live, cmd == "import");
            if (cmd == "params") result = cmdParams (*s, a);
            else if (cmd == "get") result = cmdGet (*s, a);
            else if (cmd == "set") result = cmdSet (*s, a, live);
            else if (cmd == "curve") result = cmdCurve (*s, a, live);
            else if (cmd == "render") result = cmdRender (*s, a);
            else if (cmd == "compare") result = cmdCompare (*s, a, live);
            else if (cmd == "plot") result = cmdPlot (*s, a, live);
            else if (cmd == "import") result = cmdImport (*s, a, live);
            else if (cmd == "fit") result = cmdFit (*s, a, live);
            else if (cmd == "apply") result = cmdApply (*s, a, live);
        }
        return toJson (result, a.has ("text"));
    }
    catch (const std::exception& e)
    {
        exitCode = 1;
        return toJson (Obj().add ("error", juce::String::fromUTF8 (e.what())));
    }
}
} // namespace kf::cli
