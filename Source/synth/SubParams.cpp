#include "SubParams.h"

namespace kf
{
namespace
{
using PID = juce::ParameterID;
using Range = juce::NormalisableRange<float>;

std::unique_ptr<juce::AudioParameterFloat> mkFloat (const char* id, const char* name, Range r, float def,
                                                    const char* label = "")
{
    return std::make_unique<juce::AudioParameterFloat> (PID { id, 1 }, name, r, def,
                                                        juce::AudioParameterFloatAttributes().withLabel (label));
}
}

/** -60 dB (= off) at the far left, about -12 dB at the middle of the arc, 0 dB near the right. */
static Range harmGainRange()
{
    Range r (-60.0f, 6.0f, 0.01f);
    r.setSkewForCentre (-12.0f);
    return r;
}

void addSubParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add (mkFloat ("sub_gain", "Sub Level", Range (-60.0f, 6.0f, 0.01f, 2.5f), 0.0f, "dB"));
    layout.add (std::make_unique<juce::AudioParameterBool> (PID { "sub_mute", 1 }, "Sub Mute", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        PID { "sub_wave", 1 }, "Sub Wave",
        juce::StringArray { "Sine", "Triangle", "Saw", "Square", "Soft Sine" }, 0));
    layout.add (mkFloat ("sub_phase", "Sub Start Phase", Range (0.0f, 360.0f, 0.1f), 0.0f, "deg"));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        PID { "sub_pitchmode", 1 }, "Pitch Curve Units", juce::StringArray { "Semitones", "Hz" }, 0));
    layout.add (mkFloat ("sub_root", "Sub Root Note", Range (12.0f, 96.0f, 1.0f), 33.0f, "note"));
    layout.add (mkFloat ("sub_tune", "Sub Tune", Range (-24.0f, 24.0f, 0.01f), 0.0f, "st"));
    layout.add (mkFloat ("sub_fine", "Sub Fine Tune", Range (-100.0f, 100.0f, 0.1f), 0.0f, "ct"));
    layout.add (std::make_unique<juce::AudioParameterBool> (PID { "sub_keytrack", 1 }, "Sub Key Tracking", true));
    layout.add (mkFloat ("sub_length", "Sub Length", Range (20.0f, 4000.0f, 1.0f, 0.4f), 500.0f, "ms"));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        PID { "sub_gate", 1 }, "Sub Gate", juce::StringArray { "One-shot", "Gate" }, 0));
    layout.add (mkFloat ("sub_release", "Sub Release", Range (1.0f, 2000.0f, 1.0f, 0.4f), 50.0f, "ms"));
    layout.add (mkFloat ("sub_drive", "Sub Drive", Range (0.0f, 1.0f, 0.001f), 0.0f));
    layout.add (std::make_unique<juce::AudioParameterBool> (PID { "sub_lock", 1 }, "Sub Phase Lock", false));
    layout.add (mkFloat ("sub_lockpos", "Sub Phase Lock Position", Range (0.0f, 1.0f, 0.001f), 0.3f));
    layout.add (mkFloat ("sub_delay", "Sub Start Offset", Range (0.0f, 200.0f, 0.01f, 0.5f), 0.0f, "ms"));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        PID { "sub_bus", 1 }, "Sub FX Bus", juce::StringArray { "Master", "Bus A", "Bus B" }, 0));
    layout.add (mkFloat ("sub_pan", "Sub Pan", Range (-1.0f, 1.0f, 0.001f), 0.0f));
    for (int i = 0; i < SubSettings::kNumHarmonics; ++i)
    {
        const auto n = juce::String (i + 1);
        const SubSettings def;
        layout.add (mkFloat (("sub_h" + n + "_semi").toRawUTF8(), ("Harmonic " + n + " Pitch").toRawUTF8(),
                             Range (-24.0f, 60.0f, 0.01f), def.harm[(size_t) i].semis, "st"));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            PID { ("sub_h" + n + "_gain").toRawUTF8(), 1 }, ("Harmonic " + n + " Gain").toRawUTF8(), harmGainRange(), -60.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel ("dB")
                .withStringFromValueFunction ([] (float v, int) { return v <= -59.99f ? juce::String ("Off") : juce::String (v, 1) + " dB"; })
                .withValueFromStringFunction ([] (const juce::String& t) { return t.trim().startsWithIgnoreCase ("off") ? -60.0f : t.getFloatValue(); })));
        layout.add (mkFloat (("sub_h" + n + "_decay").toRawUTF8(), ("Harmonic " + n + " Decay").toRawUTF8(),
                             Range (5.0f, 5000.0f, 1.0f, 0.4f), 5000.0f, "ms"));
    }
}

void SubParamPtrs::bind (juce::AudioProcessorValueTreeState& a)
{
    gain = a.getRawParameterValue ("sub_gain");
    mute = a.getRawParameterValue ("sub_mute");
    wave = a.getRawParameterValue ("sub_wave");
    phase = a.getRawParameterValue ("sub_phase");
    pitchMode = a.getRawParameterValue ("sub_pitchmode");
    root = a.getRawParameterValue ("sub_root");
    tune = a.getRawParameterValue ("sub_tune");
    fine = a.getRawParameterValue ("sub_fine");
    keyTrack = a.getRawParameterValue ("sub_keytrack");
    length = a.getRawParameterValue ("sub_length");
    gate = a.getRawParameterValue ("sub_gate");
    release = a.getRawParameterValue ("sub_release");
    drive = a.getRawParameterValue ("sub_drive");
    lock = a.getRawParameterValue ("sub_lock");
    lockPos = a.getRawParameterValue ("sub_lockpos");
    delay = a.getRawParameterValue ("sub_delay");
    bus = a.getRawParameterValue ("sub_bus");
    pan = a.getRawParameterValue ("sub_pan");
    for (int i = 0; i < SubSettings::kNumHarmonics; ++i)
    {
        const auto n = juce::String (i + 1);
        hSemi[(size_t) i] = a.getRawParameterValue ("sub_h" + n + "_semi");
        hGain[(size_t) i] = a.getRawParameterValue ("sub_h" + n + "_gain");
        hDecay[(size_t) i] = a.getRawParameterValue ("sub_h" + n + "_decay");
    }
    bound = true;
}

void SubParamPtrs::read (SubSettings& s) const noexcept
{
    if (! bound) return;
    s.gainDb = gain->load();
    s.mute = mute->load() > 0.5f;
    s.wave = (SubWave) juce::jlimit (0, (int) SubWave::Count - 1, (int) (wave->load() + 0.5f));
    s.startPhaseDeg = phase->load();
    s.pitchMode = pitchMode->load() > 0.5f ? PitchMode::Hz : PitchMode::Semitones;
    s.rootNote = root->load();
    s.tuneSemis = tune->load();
    s.fineCents = fine->load();
    s.keyTrack = keyTrack->load() > 0.5f;
    s.lengthMs = length->load();
    s.gate = gate->load() > 0.5f ? GateMode::Gate : GateMode::OneShot;
    s.releaseMs = release->load();
    s.drive = drive->load();
    s.phaseLock = lock->load() > 0.5f;
    s.lockPos = lockPos->load();
    s.delayMs = delay->load();
    s.bus = juce::jlimit (0, 2, (int) (bus->load() + 0.5f));
    s.pan = pan->load();
    for (size_t i = 0; i < (size_t) SubSettings::kNumHarmonics; ++i)
    {
        s.harm[i].semis = hSemi[i]->load();
        s.harm[i].gainDb = hGain[i]->load();
        s.harm[i].decayMs = hDecay[i]->load();
    }
}
}
