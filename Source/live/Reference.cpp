#include "Reference.h"
#include "analysis/KickImport.h"
#include <algorithm>
#include <cmath>

namespace kf
{
Reference::~Reference()
{
    delete pending.exchange (nullptr);
    delete retired.exchange (nullptr);
    delete current;
}

// ------------------------------------------------------------------ message thread
void Reference::publish (std::shared_ptr<const std::vector<float>> data, double rate)
{
    collectGarbage();
    Clip* c = nullptr;
    if (data != nullptr && data->size() > 1)
        c = new Clip { data, rate };
    // A nullptr clip is published too (clear): the audio thread drops the old one.
    delete pending.exchange (c);
    clipLoaded.store (c != nullptr);
    if (c == nullptr)
        pendingClear.store (true);
}

bool Reference::decode (const juce::File& f, int rs, int re, bool)
{
    std::vector<float> mono;
    double rate = 0.0;
    juce::String err;
    if (! loadAudioMono (f, mono, rate, err) || mono.size() < 2)
        return false;
    const int n = (int) mono.size();
    const int s = std::clamp (rs, 0, n - 1);
    const int e = re <= 0 ? n : std::clamp (re, s + 1, n);
    if (e - s < 2) return false;
    auto data = std::make_shared<const std::vector<float>> (mono.begin() + s, mono.begin() + e);
    {
        const juce::ScopedLock sl (lock);
        info.path = f.getFullPathName();
        info.regionStart = s;
        info.regionEnd = re <= 0 ? 0 : e;
        info.sampleRate = rate;
        info.totalSamples = n;
        info.loaded = true;
        display = data;
        displayRate = rate;
    }
    publish (data, rate);
    return true;
}

bool Reference::load (const juce::File& f, int rs, int re)
{
    const bool ok = decode (f, rs, re, true);
    if (! ok)
    {
        // keep the settings for the project, but there is nothing to play
        const juce::ScopedLock sl (lock);
        info.path = f.getFullPathName();
        info.regionStart = std::max (0, rs);
        info.regionEnd = std::max (0, re);
        info.loaded = false;
        display.reset();
        publish (nullptr, 0.0);
    }
    ++version;
    sendChangeMessage();
    return ok;
}

void Reference::clear()
{
    {
        const juce::ScopedLock sl (lock);
        const auto keepGain = info.gainDb;
        const auto keepMode = info.mode;
        info = Info();
        info.gainDb = keepGain;
        info.mode = keepMode;
        display.reset();
        displayRate = 0.0;
    }
    publish (nullptr, 0.0);
    ++version;
    sendChangeMessage();
}

bool Reference::setRegion (int rs, int re)
{
    juce::File f;
    {
        const juce::ScopedLock sl (lock);
        if (info.path.isEmpty()) return false;
        f = juce::File (info.path);
    }
    return load (f, rs, re);
}

void Reference::setNote (int note)
{
    {
        const juce::ScopedLock sl (lock);
        info.note = note < 0 ? -1 : std::min (note, 127);
    }
    ++version;
    sendChangeMessage();
}

void Reference::setGainDb (float db)
{
    db = std::clamp (db, -60.0f, 24.0f);
    {
        const juce::ScopedLock sl (lock);
        info.gainDb = db;
    }
    gain.store (juce::Decibels::decibelsToGain (db, -60.0f));
    sendChangeMessage();
}

void Reference::setMode (RefMode m)
{
    {
        const juce::ScopedLock sl (lock);
        info.mode = m;
    }
    mode.store ((int) m);
    sendChangeMessage();
}

bool Reference::hasReference() const
{
    const juce::ScopedLock sl (lock);
    return info.path.isNotEmpty();
}

Reference::Info Reference::getInfo() const
{
    const juce::ScopedLock sl (lock);
    return info;
}

std::shared_ptr<const std::vector<float>> Reference::getDisplay (double& rate) const
{
    const juce::ScopedLock sl (lock);
    rate = displayRate;
    return display;
}

std::unique_ptr<juce::XmlElement> Reference::saveState() const
{
    auto x = std::make_unique<juce::XmlElement> ("kfLive");
    const auto i = getInfo();
    x->setAttribute ("refPath", i.path);
    x->setAttribute ("regionStart", i.regionStart);
    x->setAttribute ("regionEnd", i.regionEnd);
    x->setAttribute ("note", i.note);
    x->setAttribute ("gainDb", (double) i.gainDb);
    x->setAttribute ("mode", (int) i.mode);
    return x;
}

void Reference::restoreState (const juce::XmlElement& x)
{
    setGainDb ((float) x.getDoubleAttribute ("gainDb", 0.0));
    setMode ((RefMode) std::clamp (x.getIntAttribute ("mode", 0), 0, 2));
    const auto path = x.getStringAttribute ("refPath");
    if (path.isEmpty())
    {
        if (hasReference()) clear();
        return;
    }
    load (juce::File (path), x.getIntAttribute ("regionStart", 0), x.getIntAttribute ("regionEnd", 0));
    setNote (x.getIntAttribute ("note", -1));
}

// ------------------------------------------------------------------ audio thread
void Reference::prepare (double sr)
{
    hostRate = sr > 0 ? sr : 44100.0;
}

void Reference::takePending() noexcept
{
    if (pendingClear.load (std::memory_order_relaxed) || pending.load (std::memory_order_relaxed) != nullptr)
    {
        Clip* expected = nullptr;
        // Take the new clip only once the old one can be parked; otherwise retry on the next call.
        if (current == nullptr || retired.compare_exchange_strong (expected, current))
        {
            pendingClear.store (false);
            current = pending.exchange (nullptr);
            pos = 0.0;
            playing = false;
        }
    }
}

void Reference::noteOn (int delaySamples) noexcept
{
    const int at = std::max (0, delaySamples);
    if (nTrig < kMaxTrig) trig[nTrig++] = at;
    else trig[kMaxTrig - 1] = at;
}

void Reference::process (juce::AudioBuffer<float>& out, bool audible) noexcept
{
    takePending();
    const int n = out.getNumSamples(), chans = out.getNumChannels();
    int ti = 0;
    if (current == nullptr)
    {
        nTrig = 0;
        return;
    }

    const auto& d = *current->data;
    const double ratio = current->rate / hostRate;
    const double last = (double) d.size() - 1.0;
    const float g = gain.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        while (ti < nTrig && trig[ti] <= i) { pos = 0.0; playing = true; ++ti; }
        if (! playing) continue;
        if (pos >= last) { playing = false; continue; }
        if (audible)
        {
            const int i0 = (int) pos;
            const float fr = (float) (pos - i0);
            const float v = (d[(size_t) i0] * (1.0f - fr) + d[(size_t) i0 + 1] * fr) * g;
            for (int c = 0; c < chans; ++c)
                out.addSample (c, i, v);
        }
        pos += ratio;
    }
    // starts beyond this block (synth latency > block size) carry over to the next one
    int keep = 0;
    for (; ti < nTrig; ++ti) trig[keep++] = trig[ti] - n;
    nTrig = keep;
}
} // namespace kf
