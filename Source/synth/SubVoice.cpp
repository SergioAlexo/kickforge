#include "SubVoice.h"
#include <algorithm>
#include <cmath>

namespace kf
{
namespace
{
constexpr double twoPi = 6.283185307179586476925286766559;

inline double blep (double t, double dt) noexcept
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}
inline double wrap01 (double x) noexcept { return x - std::floor (x); }
}

SubVoice::~SubVoice()
{
    stopLayer (cur);
    stopLayer (old);
}

void SubVoice::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    stScratch.assign ((size_t) maxBlock, 0.0);
    tmp.assign ((size_t) maxBlock, 0.0f);
    smoothCoef = 1.0 - std::exp (-1.0 / (0.005 * sr)); // 5 ms
    declickSamples = std::max (8.0, 0.0015 * sr);       // 1.5 ms
    stopLayer (cur);
    stopLayer (old);
    smoothInit = false;
    lastFreq = 0.0;
}

void SubVoice::bind (juce::AudioProcessorValueTreeState& apvts, SubCurves& curves)
{
    subPtrs.bind (apvts);
    globPtrs.bind (apvts);
    pitchSlot = &curves.slot (SubCurveId::Pitch);
    ampSlot = &curves.slot (SubCurveId::Amp);
}

void SubVoice::refresh() noexcept
{
    subPtrs.read (sub);
    globPtrs.read (glob);
    userStTarget = (double) sub.tuneSemis + (double) sub.fineCents * 0.01 + (double) bend * (double) glob.bendRange;
    if (! smoothInit)
    {
        userSt = userStTarget;
        gainSm = sub.mute ? 0.0f : juce::Decibels::decibelsToGain (sub.gainDb);
        smoothInit = true;
    }
}

void SubVoice::setPitchBend (float b) { bend = juce::jlimit (-1.0f, 1.0f, b); }

void SubVoice::stopLayer (Layer& l) noexcept
{
    if (l.pitchSnap != nullptr) CurveSlot::release (l.pitchSnap);
    if (l.ampSnap != nullptr) CurveSlot::release (l.ampSnap);
    l.pitchSnap = l.ampSnap = nullptr;
    l.pitchW.reset();
    l.ampW.reset();
    l.active = false;
    l.releasing = false;
}

void SubVoice::startLayer (Layer& l, int note, float vel) noexcept
{
    stopLayer (l);
    if (pitchSlot == nullptr || ampSlot == nullptr) return;
    l.pitchSnap = pitchSlot->acquire();
    l.ampSnap = ampSlot->acquire();
    l.pitchW.emplace (l.pitchSnap->curve.walker());
    l.ampW.emplace (l.ampSnap->curve.walker());
    l.note = note;
    l.phase = wrap01 ((double) sub.startPhaseDeg / 360.0);
    l.time = 0.0;
    l.length = std::max (0.005, (double) sub.lengthMs * 0.001);
    const float s = juce::jlimit (0.0f, 1.0f, glob.velSens);
    l.velGain = (1.0f - s) + s * juce::jlimit (0.0f, 1.0f, vel);
    l.fade = 0.0f;
    l.fadeStep = (float) (1.0 / declickSamples);
    l.rel = 1.0f;
    l.releasing = false;
    l.active = true;
}

void SubVoice::noteOn (int note, float velocity)
{
    refresh();
    const double target = (double) note;

    // Legato: keep sounding, just glide to the new note.
    if (glob.legato && cur.active && ! cur.releasing)
    {
        noteTarget = target;
        const double glideSamples = (double) glob.glideMs * 0.001 * sr;
        if (glideSamples >= 1.0) noteStep = (target - noteNow) / glideSamples;
        else { noteNow = target; noteStep = 0.0; }
        cur.note = note;
        return;
    }

    // Retrigger: crossfade the old tail out over a few ms, new note in.
    if (cur.active)
    {
        stopLayer (old);
        old = std::move (cur);              // move snapshots over (cur re-initialised below)
        cur = Layer {};
        old.fadeStep = -(float) (1.0 / declickSamples);
        if (old.fade <= 0.0f) stopLayer (old);
    }

    noteTarget = target;
    const double glideSamples = (double) glob.glideMs * 0.001 * sr;
    if (haveNote && glideSamples >= 1.0) noteStep = (target - noteNow) / glideSamples;
    else { noteNow = target; noteStep = 0.0; }
    haveNote = true;

    startLayer (cur, note, velocity);
}

void SubVoice::noteOff()
{
    refresh();
    if (sub.gate == GateMode::Gate && cur.active && ! cur.releasing)
    {
        cur.releasing = true;
        const double n = std::max (1.0, (double) sub.releaseMs * 0.001 * sr);
        cur.relCoef = (float) std::exp (-6.907755278982137 / n); // -60 dB over the release time
    }
}

void SubVoice::noteOff (int note)
{
    if (cur.active && cur.note == note) noteOff();
}

float SubVoice::waveform (double p, double dp) const noexcept
{
    switch (sub.wave)
    {
        case SubWave::Sine:
            return (float) std::sin (twoPi * p);
        case SubWave::Triangle:
            return (float) (4.0 * std::abs (wrap01 (p + 0.75) - 0.5) - 1.0);
        case SubWave::Saw:
        {
            const double q = wrap01 (p + 0.5);
            return (float) (2.0 * q - 1.0 - blep (q, dp));
        }
        case SubWave::Square:
            return (float) ((p < 0.5 ? 1.0 : -1.0) + blep (p, dp) - blep (wrap01 (p + 0.5), dp));
        case SubWave::SoftSine:
        {
            constexpr double k = 2.5;
            static const double norm = 1.0 / std::tanh (k);
            return (float) (std::tanh (k * std::sin (twoPi * p)) * norm);
        }
        default: break;
    }
    return 0.0f;
}

void SubVoice::renderLayer (Layer& l, float* out, int n, const double* stOff) noexcept
{
    const double dt = 1.0 / sr;
    const double a4scale = (double) glob.a4 / 440.0;
    const double maxF = 0.45 * sr;
    const double tailFade = std::min (0.002, l.length * 0.1);

    for (int i = 0; i < n; ++i)
    {
        if (l.time >= l.length) { stopLayer (l); return; }
        const double u = l.time / l.length;
        const double v = l.pitchW->at (u);
        double f;
        if (sub.pitchMode == PitchMode::Semitones)
            f = 440.0 * a4scale * std::exp2 ((((double) sub.rootNote + stOff[i] + v) - 69.0) / 12.0);
        else
            f = std::max (0.0, v) * std::exp2 (stOff[i] / 12.0) * a4scale;
        f = std::clamp (f, 0.5, maxF);
        const double dp = f * dt;
        lastFreq = f;

        double amp = std::max (0.0, l.ampW->at (u));
        amp *= std::min (1.0, (l.length - l.time) / tailFade);

        l.fade += l.fadeStep;
        if (l.fadeStep > 0.0f && l.fade >= 1.0f) { l.fade = 1.0f; l.fadeStep = 0.0f; }
        else if (l.fadeStep < 0.0f && l.fade <= 0.0f) { stopLayer (l); return; }

        if (l.releasing)
        {
            l.rel *= l.relCoef;
            if (l.rel < 1.0e-4f) { stopLayer (l); return; }
        }

        out[i] += waveform (l.phase, dp) * (float) amp * l.fade * l.rel * l.velGain;

        l.phase += dp;
        l.phase -= std::floor (l.phase);
        l.time += dt;
    }
}

void SubVoice::process (juce::AudioBuffer<float>& buffer, int start, int num)
{
    if (! cur.active && ! old.active) return;
    refresh();
    const float gTarget = sub.mute ? 0.0f : juce::Decibels::decibelsToGain (sub.gainDb);

    while (num > 0)
    {
        const int n = std::min (num, maxBlock);
        auto* out = buffer.getWritePointer (0) + start;

        // shared per-sample pitch offset in semitones (glide, tune, bend)
        for (int i = 0; i < n; ++i)
        {
            if (noteStep != 0.0)
            {
                noteNow += noteStep;
                if ((noteStep > 0.0 && noteNow >= noteTarget) || (noteStep < 0.0 && noteNow <= noteTarget))
                { noteNow = noteTarget; noteStep = 0.0; }
            }
            userSt += (userStTarget - userSt) * smoothCoef;
            stScratch[(size_t) i] = (sub.keyTrack ? noteNow - (double) sub.rootNote : 0.0) + userSt;
        }

        // render into a scratch via the output (layers add), then apply smoothed gain to just this layer sum
        // To keep "ADD" semantics, render into a temp region first.
        std::fill_n (tmp.begin(), n, 0.0f);
        if (cur.active) renderLayer (cur, tmp.data(), n, stScratch.data());
        if (old.active) renderLayer (old, tmp.data(), n, stScratch.data());

        for (int i = 0; i < n; ++i)
        {
            gainSm += (gTarget - gainSm) * (float) smoothCoef;
            const float s = tmp[(size_t) i] * gainSm;
            out[i] += std::isfinite (s) ? s : 0.0f;
        }

        start += n;
        num -= n;
    }
    if (! cur.active && ! old.active) lastFreq = 0.0;
}
}
