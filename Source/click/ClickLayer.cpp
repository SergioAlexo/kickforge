#include "ClickLayer.h"
#include <algorithm>
#include <cmath>

namespace kf
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr size_t kMaxSourceSamples = 48000u * 60u; // refuse to hold more than ~60 s of mono audio

inline float hermite (const std::vector<float>& d, double pos) noexcept
{
    const long n = (long) d.size();
    const long i = (long) pos;
    const float f = (float) (pos - (double) i);
    auto at = [&] (long k) { return (k >= 0 && k < n) ? d[(size_t) k] : 0.0f; };
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + c0;
}

std::shared_ptr<const std::vector<float>> resampleMono (const std::vector<float>& in, double srcRate, double dstRate)
{
    if (in.empty() || srcRate <= 0 || dstRate <= 0)
        return std::make_shared<const std::vector<float>>();
    if (std::abs (srcRate - dstRate) < 1.0e-6)
        return std::make_shared<const std::vector<float>> (in);
    const auto outN = (size_t) std::max (1.0, std::floor ((double) in.size() * dstRate / srcRate));
    auto out = std::make_shared<std::vector<float>> (outN);
    juce::LagrangeInterpolator interp;
    interp.process (srcRate / dstRate, in.data(), out->data(), (int) outN, (int) in.size(), 0);
    return out;
}
}

// ---------------------------------------------------------------- filter
void ClickLayer::Svf::set (double sr, double fc, double q) noexcept
{
    const double g = std::tan (kPi * std::clamp (fc, 20.0, sr * 0.45) / sr);
    k = 1.0 / std::max (0.3, q);
    a1 = 1.0 / (1.0 + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

float ClickLayer::Svf::process (float x, int mode) noexcept
{
    const double v3 = (double) x - ic2;
    const double v1 = a1 * ic1 + a2 * v3;
    const double v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0 * v1 - ic1;
    ic2 = 2.0 * v2 - ic2;
    switch (mode)
    {
        case filtLP: return (float) v2;
        case filtHP: return (float) ((double) x - k * v1 - v2);
        default:     return (float) v1 * (float) k; // band-pass, unity-ish peak gain
    }
}

// ---------------------------------------------------------------- lifecycle
ClickLayer::ClickLayer()
{
    formats.registerBasicFormats(); // WAV, AIFF, FLAC, OGG, (MP3 where enabled)
    for (int l = 0; l < kNumClickLayers; ++l)
    {
        for (int i = 0; i < (int) CP::count; ++i)
        {
            own[(size_t) l][(size_t) i].store (clickDefault (l, (CP) i));
            ptrs[(size_t) l][(size_t) i] = &own[(size_t) l][(size_t) i];
        }
        auto& s = slots[(size_t) l];
        s.curve = Curve ({ { 0.0, 1.0, 0.0 }, { 1.0, 1.0, 0.0 } });
        for (auto& r : s.retired) r.store (nullptr);
        s.current = makeSnapshot (nullptr, s.curve).release();
    }
    masterPtr = &ownMaster;
}

ClickLayer::~ClickLayer()
{
    pool.removeAllJobs (true, 10000);
    for (auto& s : slots)
    {
        delete s.current;
        delete s.pending.exchange (nullptr);
        for (auto& r : s.retired) delete r.exchange (nullptr);
    }
}

void ClickLayer::bindParams (juce::AudioProcessorValueTreeState& apvts)
{
    for (int l = 0; l < kNumClickLayers; ++l)
        for (int i = 0; i < (int) CP::count; ++i)
            if (auto* p = apvts.getRawParameterValue (clickParamId (l, (CP) i)))
                ptrs[(size_t) l][(size_t) i] = p;
    if (auto* m = apvts.getRawParameterValue ("click_gain"))
        masterPtr = m;
}

void ClickLayer::setParam (int layer, CP p, float value) noexcept
{
    if (layer >= 0 && layer < kNumClickLayers)
        own[(size_t) layer][(size_t) p].store (value);
}

int ClickLayer::getLayerBus (int layer) const noexcept
{
    return layer >= 0 && layer < kNumClickLayers ? (int) std::lround (param (layer, CP::bus)) : 0;
}

void ClickLayer::prepare (double sr, int)
{
    const bool changed = std::abs (sampleRate.load() - sr) > 1.0e-6;
    sampleRate.store (sr);
    for (int l = 0; l < kNumClickLayers; ++l)
    {
        auto& v = slots[(size_t) l].voice;
        v.active = false;
        v.gen.prepare (sr);
        v.gain.reset (sr, 0.005);
        if (changed)
        {
            bool has;
            { const juce::ScopedLock sl (stateLock); has = slots[(size_t) l].original != nullptr; }
            if (has)
            {
                const int g = slots[(size_t) l].generation.load();
                ++pendingJobs;
                pool.addJob ([this, l, g] { resampleAndPublish (l, g); --pendingJobs; });
            }
        }
    }
}

// ---------------------------------------------------------------- snapshots
std::unique_ptr<ClickLayer::Snapshot> ClickLayer::makeSnapshot (std::shared_ptr<const std::vector<float>> smp, const Curve& c)
{
    auto s = std::make_unique<Snapshot>();
    s->sample = std::move (smp);
    for (int i = 0; i < envTableSize; ++i)
    {
        const double v = c.empty() ? 1.0 : c.eval ((double) i / (envTableSize - 1));
        s->env[(size_t) i] = (float) std::clamp (v, 0.0, 2.0);
    }
    return s;
}

void ClickLayer::sweepRetired (Slot& s)
{
    for (auto& r : s.retired) delete r.exchange (nullptr);
}

void ClickLayer::retire (Slot& s, Snapshot* old) noexcept
{
    for (auto& r : s.retired)
    {
        Snapshot* expected = nullptr;
        if (r.compare_exchange_strong (expected, old)) return;
    }
    // ring full (message thread stalled): leak rather than free on the audio thread
}

void ClickLayer::publish (int layer)
{
    auto& s = slots[(size_t) layer];
    std::shared_ptr<const std::vector<float>> smp;
    Curve c;
    {
        const juce::ScopedLock sl (stateLock);
        c = s.curve;
        smp = s.currentSample;
    }
    sweepRetired (s);
    delete s.pending.exchange (makeSnapshot (std::move (smp), c).release());
}

void ClickLayer::setEnvelope (int layer, const Curve& c)
{
    if (layer < 0 || layer >= kNumClickLayers) return;
    { const juce::ScopedLock sl (stateLock); slots[(size_t) layer].curve = c; }
    ++stateVer;
    publish (layer);
}

Curve ClickLayer::getEnvelope (int layer) const
{
    const juce::ScopedLock sl (stateLock);
    return slots[(size_t) std::clamp (layer, 0, kNumClickLayers - 1)].curve;
}

// ---------------------------------------------------------------- loading
ClickLayer::SampleStatus ClickLayer::getSampleStatus (int layer) const noexcept
{
    return (SampleStatus) slots[(size_t) std::clamp (layer, 0, kNumClickLayers - 1)].status.load();
}

juce::String ClickLayer::getSamplePath (int layer) const
{
    const juce::ScopedLock sl (stateLock);
    return slots[(size_t) std::clamp (layer, 0, kNumClickLayers - 1)].path;
}

int ClickLayer::getSampleLength (int layer) const
{
    return slots[(size_t) std::clamp (layer, 0, kNumClickLayers - 1)].length.load();
}

bool ClickLayer::waitForLoads (int timeoutMs) const
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (pendingJobs.load() > 0)
    {
        if (juce::Time::getMillisecondCounter() > end) return false;
        juce::Thread::sleep (2);
    }
    return true;
}

void ClickLayer::resampleAndPublish (int layer, int gen)
{
    auto& s = slots[(size_t) layer];
    std::shared_ptr<const std::vector<float>> orig;
    double rate;
    {
        const juce::ScopedLock sl (stateLock);
        orig = s.original;
        rate = s.originalRate;
    }
    if (! orig) return;
    auto rs = resampleMono (*orig, rate, sampleRate.load());
    if (s.generation.load() != gen) return; // superseded
    {
        const juce::ScopedLock sl (stateLock);
        s.currentSample = rs;
    }
    s.length.store ((int) rs->size());
    s.status.store ((int) SampleStatus::loaded);
    publish (layer);
}

void ClickLayer::startLoadJob (int layer, juce::File file, int gen)
{
    ++pendingJobs;
    pool.addJob ([this, layer, file, gen]
    {
        auto& s = slots[(size_t) layer];
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        bool ok = false;
        if (reader != nullptr && reader->lengthInSamples > 0 && reader->numChannels > 0
            && (size_t) reader->lengthInSamples <= kMaxSourceSamples * 4)
        {
            const auto n = (int) reader->lengthInSamples;
            juce::AudioBuffer<float> tmp ((int) std::min<unsigned> (reader->numChannels, 2u), n);
            reader->read (&tmp, 0, n, 0, true, tmp.getNumChannels() > 1);
            auto mono = std::make_shared<std::vector<float>> ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                float v = tmp.getSample (0, i);
                if (tmp.getNumChannels() > 1) v = 0.5f * (v + tmp.getSample (1, i));
                (*mono)[(size_t) i] = std::isfinite (v) ? v : 0.0f;
            }
            if (s.generation.load() == gen)
            {
                {
                    const juce::ScopedLock sl (stateLock);
                    s.original = mono;
                    s.originalRate = reader->sampleRate;
                }
                resampleAndPublish (layer, gen);
                ok = true;
            }
            else
                ok = true; // superseded, nothing to report
        }
        if (! ok && s.generation.load() == gen)
            s.status.store ((int) SampleStatus::failed);
        --pendingJobs;
    });
}

void ClickLayer::loadSample (int layer, const juce::File& file)
{
    if (layer < 0 || layer >= kNumClickLayers) return;
    auto& s = slots[(size_t) layer];
    const int gen = ++s.generation;
    ++stateVer;
    {
        const juce::ScopedLock sl (stateLock);
        s.path = file.getFullPathName();
        s.original.reset();
        s.currentSample.reset();
    }
    if (! file.existsAsFile())
    {
        s.status.store ((int) SampleStatus::missing);
        s.length.store (0);
        publish (layer);
        return;
    }
    s.status.store ((int) SampleStatus::loading);
    startLoadJob (layer, file, gen);
}

void ClickLayer::loadSampleFromMemory (int layer, std::vector<float> mono, double rate, const juce::String& label)
{
    if (layer < 0 || layer >= kNumClickLayers) return;
    auto& s = slots[(size_t) layer];
    const int gen = ++s.generation;
    ++stateVer;
    {
        const juce::ScopedLock sl (stateLock);
        s.path = label;
        s.original = std::make_shared<const std::vector<float>> (std::move (mono));
        s.originalRate = rate;
    }
    resampleAndPublish (layer, gen);
}

std::shared_ptr<const std::vector<float>> ClickLayer::getSourceSample (int layer, double& rate) const
{
    rate = 0.0;
    if (layer < 0 || layer >= kNumClickLayers) return {};
    const juce::ScopedLock sl (stateLock);
    rate = slots[(size_t) layer].originalRate;
    return slots[(size_t) layer].original;
}

void ClickLayer::clearSample (int layer)
{
    if (layer < 0 || layer >= kNumClickLayers) return;
    auto& s = slots[(size_t) layer];
    ++s.generation;
    ++stateVer;
    {
        const juce::ScopedLock sl (stateLock);
        s.path.clear();
        s.original.reset();
        s.currentSample.reset();
    }
    s.length.store (0);
    s.status.store ((int) SampleStatus::empty);
    publish (layer);
}

// ---------------------------------------------------------------- state
juce::ValueTree ClickLayer::saveState() const
{
    juce::ValueTree t ("click");
    const juce::ScopedLock sl (stateLock);
    for (int l = 0; l < kNumClickLayers; ++l)
    {
        const auto& s = slots[(size_t) l];
        juce::ValueTree v ("layer");
        v.setProperty ("index", l, nullptr);
        v.setProperty ("path", s.path, nullptr);
        auto c = s.curve.toValueTree();
        c.setProperty ("role", "amp", nullptr);
        v.appendChild (c, nullptr);
        t.appendChild (v, nullptr);
    }
    return t;
}

void ClickLayer::loadState (const juce::ValueTree& t)
{
    if (! t.isValid()) return;
    for (const auto& v : t)
    {
        const int l = v.getProperty ("index", -1);
        if (l < 0 || l >= kNumClickLayers) continue;
        auto c = v.getChildWithProperty ("role", "amp");
        if (c.isValid())
            setEnvelope (l, Curve::fromValueTree (c));
        const juce::String path = v.getProperty ("path").toString();
        if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
            loadSample (l, juce::File (path)); // sets status = missing if the file is gone
        else if (path.isEmpty())
            clearSample (l);
    }
}

// ---------------------------------------------------------------- audio
void ClickLayer::noteOn (int note, float velocity)
{
    currentNote = note;
    const double sr = sampleRate.load();
    for (int l = 0; l < kNumClickLayers; ++l)
    {
        auto& s = slots[(size_t) l];
        auto& v = s.voice;
        v.active = false;

        // pick up a freshly published snapshot
        if (s.pending.load (std::memory_order_acquire) != nullptr)
            if (auto* n = s.pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                retire (s, s.current);
                s.current = n;
            }

        const int src = (int) std::lround (param (l, CP::source));
        if (src == srcOff) continue;

        const double semis = param (l, CP::pitch) + param (l, CP::fine) / 100.0
                             + (param (l, CP::keyTrack) > 0.5f ? (double) (note - 36) : 0.0);
        const double ratio = std::exp2 (semis / 12.0);
        const double lenSamples = param (l, CP::length) * 0.001 * sr;

        v.src = src;
        v.vel = std::clamp (velocity, 0.0f, 1.0f);
        v.age = 0;
        v.delayLeft = (long) std::lround (param (l, CP::delay) * 0.001 * sr);
        v.filter.reset();
        v.cutoffSm = param (l, CP::cutoff);
        v.fresh = true; // gain jumps straight to its target on the first block (no fade-in on the transient)

        if (src == srcSample)
        {
            const auto* data = s.current->sample.get();
            if (data == nullptr || data->empty()) continue; // missing / not loaded: silent
            v.pos = param (l, CP::start) * 0.001 * sr;
            v.rate = ratio;
            const double avail = ((double) data->size() - v.pos) / ratio;
            if (avail < 2.0) continue;
            v.length = (long) std::min (lenSamples, avail);
        }
        else
        {
            ClickSettings cs;
            cs.type = (ClickType) (src - srcNoise);
            cs.toneHz = (float) (param (l, CP::tone) * ratio);
            cs.decayMs = param (l, CP::decay);
            cs.color = param (l, CP::color);
            v.gen.prepare (sr);
            v.gen.trigger (cs);
            v.length = (long) lenSamples;
        }
        if (v.length < 2) continue;
        v.active = true;
    }
}

void ClickLayer::noteOff() {} // clicks are one-shots

void ClickLayer::process (juce::AudioBuffer<float>& buffer, int start, int num)
{
    if (num <= 0 || buffer.getNumChannels() < 1) return;
    float* out = buffer.getWritePointer (0) + start;
    for (int l = 0; l < kNumClickLayers; ++l)
        processLayer (l, out, num);
}

float ClickLayer::getLayerPan (int layer) const noexcept
{
    return layer >= 0 && layer < kNumClickLayers ? param (layer, CP::pan) : 0.0f;
}

void ClickLayer::processLayer (int l, float* out, int num)
{
    if (num <= 0 || l < 0 || l >= kNumClickLayers) return;
    const double sr = sampleRate.load();
    const float master = juce::Decibels::decibelsToGain (masterPtr->load (std::memory_order_relaxed), -60.0f);
    const long fadeN = std::max<long> (4, (long) (0.002 * sr));

    bool anySolo = false;
    for (int k = 0; k < kNumClickLayers; ++k)
        if (param (k, CP::solo) > 0.5f && (int) std::lround (param (k, CP::source)) != srcOff)
            anySolo = true;

    {
        auto& s = slots[(size_t) l];
        auto& v = s.voice;
        // swap in new snapshots even mid-note (envelope edits / sample reloads)
        if (s.pending.load (std::memory_order_acquire) != nullptr)
            if (auto* n = s.pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                retire (s, s.current);
                s.current = n;
            }
        if (! v.active) return;

        const bool audible = param (l, CP::mute) < 0.5f && (! anySolo || param (l, CP::solo) > 0.5f);
        const float sign = param (l, CP::flip) > 0.5f ? -1.0f : 1.0f;
        const float g = audible ? juce::Decibels::decibelsToGain (param (l, CP::gain), -60.0f) * sign * master : 0.0f;
        if (v.fresh) { v.gain.setCurrentAndTargetValue (g); v.fresh = false; }
        else v.gain.setTargetValue (g);

        const int fmode = (int) std::lround (param (l, CP::filterType));
        const float cutTarget = param (l, CP::cutoff);
        const double q = 0.7 + (double) param (l, CP::resonance) * param (l, CP::resonance) * 12.0;
        const auto* data = s.current->sample.get();
        const auto& env = s.current->env;

        for (int i = 0; i < num; ++i)
        {
            if (v.delayLeft > 0) { --v.delayLeft; continue; }
            if (v.age >= v.length) { v.active = false; break; }

            float x;
            if (v.src == srcSample)
            {
                if (data == nullptr || v.pos >= (double) data->size()) { v.active = false; break; }
                x = hermite (*data, v.pos);
                v.pos += v.rate;
            }
            else
                x = v.gen.next();

            // envelope (normalized over the layer length) + short tail fade
            const double u = (double) v.age / (double) (v.length - 1) * (envTableSize - 1);
            const int i0 = std::min ((int) u, envTableSize - 2);
            const float fr = (float) (u - i0);
            float e = env[(size_t) i0] + fr * (env[(size_t) i0 + 1] - env[(size_t) i0]);
            const long remaining = v.length - v.age;
            if (remaining < fadeN) e *= (float) remaining / (float) fadeN;

            if (fmode != filtOff)
            {
                if ((v.age & 15) == 0)
                {
                    v.cutoffSm += 0.2f * (cutTarget - v.cutoffSm); // one-pole glide, avoids zipper noise
                    v.filter.set (sr, v.cutoffSm, q);
                }
                x = v.filter.process (x, fmode);
            }
            const float y = x * e * v.vel * v.gain.getNextValue();
            out[i] += std::isfinite (y) ? y : 0.0f;
            ++v.age;
        }
    }
}
}
