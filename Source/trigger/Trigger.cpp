#include "Trigger.h"
#include <cmath>

namespace kf
{
void addTriggerParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using PID = juce::ParameterID;
    using Range = juce::NormalisableRange<float>;
    auto fl = [&] (const char* id, const char* name, Range r, float def, const char* unit)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (PID { id, 1 }, name, r, def,
                                                                 juce::AudioParameterFloatAttributes().withLabel (unit)));
    };
    layout.add (std::make_unique<juce::AudioParameterBool> (PID { "trig_enable", 1 }, "Trigger Enable", false));
    fl ("trig_hp", "Trigger High-pass", Range (20.0f, 2000.0f, 1.0f, 0.4f), 40.0f, "Hz");
    fl ("trig_lp", "Trigger Low-pass", Range (200.0f, 20000.0f, 1.0f, 0.4f), 8000.0f, "Hz");
    fl ("trig_thresh", "Trigger Threshold", Range (-60.0f, 0.0f, 0.1f), -24.0f, "dB");
    fl ("trig_offret", "Trigger Off Return", Range (0.0f, 40.0f, 0.1f), 6.0f, "dB");
    fl ("trig_retrig", "Trigger Retrigger Return", Range (0.0f, 40.0f, 0.1f), 3.0f, "dB");
    fl ("trig_note", "Trigger Note", Range (0.0f, 127.0f, 1.0f), 36.0f, "note");
    fl ("trig_vel", "Trigger Velocity", Range (0.0f, 1.0f, 0.001f), 1.0f, "");
    layout.add (std::make_unique<juce::AudioParameterBool> (PID { "trig_dyn", 1 }, "Trigger Dynamic Velocity", false));

    layout.add (std::make_unique<juce::AudioParameterBool> (PID { "midiout_enable", 1 }, "MIDI Out Enable", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (PID { "midiout_follow", 1 }, "MIDI Out Follow Note", false));
    fl ("midiout_note", "MIDI Out Note", Range (0.0f, 127.0f, 1.0f), 36.0f, "note");
    fl ("midiout_chan", "MIDI Out Channel", Range (1.0f, 16.0f, 1.0f), 1.0f, "");
    fl ("midiout_len", "MIDI Out Length", Range (5.0f, 2000.0f, 1.0f, 0.4f), 100.0f, "ms");
}

void TriggerParamPtrs::bind (juce::AudioProcessorValueTreeState& a)
{
    enable = a.getRawParameterValue ("trig_enable"); hp = a.getRawParameterValue ("trig_hp");
    lp = a.getRawParameterValue ("trig_lp"); thresh = a.getRawParameterValue ("trig_thresh");
    off = a.getRawParameterValue ("trig_offret"); retrig = a.getRawParameterValue ("trig_retrig");
    note = a.getRawParameterValue ("trig_note"); vel = a.getRawParameterValue ("trig_vel");
    dyn = a.getRawParameterValue ("trig_dyn");
}

void TriggerParamPtrs::read (TriggerSettings& s) const noexcept
{
    if (enable == nullptr) return;
    s.enable = enable->load() > 0.5f; s.hpHz = hp->load(); s.lpHz = lp->load();
    s.threshDb = thresh->load(); s.offReturnDb = off->load(); s.retrigReturnDb = retrig->load();
    s.note = juce::jlimit (0, 127, (int) (note->load() + 0.5f)); s.velocity = vel->load();
    s.dynamic = dyn->load() > 0.5f;
}

void TriggerDetector::prepare (double sampleRate)
{
    sr = sampleRate;
    hpKey = lpKey = -1;
    reset();
}

void TriggerDetector::reset()
{
    hp.reset(); lp.reset();
    env = 0.0f; armed = true; held = false;
}

int TriggerDetector::process (const TriggerSettings& s, const float* l, const float* r, int n,
                              Event* out, int maxEvents) noexcept
{
    if (hpKey != s.hpHz) { hp.setHighpass (sr, s.hpHz, 0.7071); hpKey = s.hpHz; }
    if (lpKey != s.lpHz) { lp.setLowpass (sr, s.lpHz, 0.7071); lpKey = s.lpHz; }
    const float thr = juce::Decibels::decibelsToGain (s.threshDb);
    const float offLvl = juce::Decibels::decibelsToGain (s.threshDb - s.offReturnDb);
    const float rearm = juce::Decibels::decibelsToGain (s.threshDb - s.retrigReturnDb);
    const float rel = (float) std::exp (-1.0 / (0.02 * sr)); // 20 ms peak decay
    const int win = std::max (1, (int) (0.001 * sr));
    int count = 0;

    for (int i = 0; i < n; ++i)
    {
        float x = r != nullptr ? 0.5f * (l[i] + r[i]) : l[i];
        if (! std::isfinite (x)) x = 0.0f;
        const float a = std::abs (lp.process (hp.process (x)));
        env = a > env ? a : env * rel; // instant attack, exponential release
        if (env < 1.0e-9f) env = 0.0f;

        if (armed && env >= thr)
        {
            armed = false;
            if (held && count < maxEvents) out[count++] = { i, false, heldNote, 0.0f }; // retrigger: close the old note
            float vel = s.velocity;
            if (s.dynamic)
            {
                float pk = 0.0f;
                for (int k = i; k < n && k < i + win; ++k)
                {
                    const float v = r != nullptr ? 0.5f * (l[k] + r[k]) : l[k];
                    pk = std::max (pk, std::abs (v));
                }
                vel = juce::jlimit (0.05f, 1.0f, pk);
            }
            if (count < maxEvents) out[count++] = { i, true, s.note, vel };
            held = true;
            heldNote = s.note;
        }
        else if (! armed && env < rearm)
            armed = true;

        if (held && env < offLvl)
        {
            held = false;
            if (count < maxEvents) out[count++] = { i, false, heldNote, 0.0f };
        }
    }
    return count;
}
}
