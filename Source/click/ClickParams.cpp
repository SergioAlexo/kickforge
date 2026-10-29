#include "ClickParams.h"

namespace kf
{
namespace
{
// order must match enum CP
const CPSpec kSpecs[] = {
    { "source",  "Source",      CKind::choiceP, 0, (float) (srcCount - 1), (float) srcOff, 0, "" },
    { "tone",    "Tone",        CKind::floatP, 20.0f, 20000.0f, 2500.0f, 1000.0f, "Hz" },
    { "decay",   "Decay",       CKind::floatP, 0.5f, 500.0f, 15.0f, 40.0f, "ms" },
    { "color",   "Color",       CKind::floatP, 0.0f, 1.0f, 0.5f, 0, "" },
    { "start",   "Start",       CKind::floatP, 0.0f, 1000.0f, 0.0f, 0, "ms" },
    { "delay",   "Delay",       CKind::floatP, 0.0f, 200.0f, 0.0f, 0, "ms" },
    { "length",  "Length",      CKind::floatP, 1.0f, 2000.0f, 2000.0f, 200.0f, "ms" },
    { "pitch",   "Pitch",       CKind::floatP, -24.0f, 24.0f, 0.0f, 0, "st" },
    { "fine",    "Fine",        CKind::floatP, -100.0f, 100.0f, 0.0f, 0, "ct" },
    { "keytrack","Key Track",   CKind::boolP,  0, 1, 0, 0, "" },
    { "gain",    "Gain",        CKind::floatP, -48.0f, 12.0f, -6.0f, 0, "dB" },
    { "pan",     "Pan",         CKind::floatP, -1.0f, 1.0f, 0.0f, 0, "" },
    { "filter",  "Filter Type", CKind::choiceP, 0, 3, (float) filtOff, 0, "" },
    { "cutoff",  "Cutoff",      CKind::floatP, 20.0f, 20000.0f, 8000.0f, 1000.0f, "Hz" },
    { "res",     "Resonance",   CKind::floatP, 0.0f, 1.0f, 0.2f, 0, "" },
    { "flip",    "Phase Flip",  CKind::boolP,  0, 1, 0, 0, "" },
    { "mute",    "Mute",        CKind::boolP,  0, 1, 0, 0, "" },
    { "solo",    "Solo",        CKind::boolP,  0, 1, 0, 0, "" },
    { "bus",     "FX Bus",      CKind::choiceP, 0, 2, (float) busMaster, 0, "" },
};
static_assert (sizeof (kSpecs) / sizeof (kSpecs[0]) == (size_t) CP::count);

juce::StringArray choices (CP p)
{
    switch (p)
    {
        case CP::source:     return { "Off", "Sample", "Noise Burst", "Impulse", "Chirp", "Beater" };
        case CP::filterType: return { "Off", "Low-pass", "High-pass", "Band-pass" };
        case CP::bus:        return { "Master", "Bus A", "Bus B" };
        default:             return {};
    }
}
}

const CPSpec& clickSpec (CP p) noexcept { return kSpecs[(int) p]; }

float clickDefault (int layer, CP p) noexcept
{
    if (p == CP::source) return layer == 0 ? (float) srcBeater : (float) srcOff;
    return clickSpec (p).def;
}

juce::String clickParamId (int layer, CP p)
{
    return "click_" + juce::String (layer + 1) + "_" + clickSpec (p).suffix;
}

void addClickParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "click_gain", 1 }, "Click Master Gain",
        juce::NormalisableRange<float> (-24.0f, 6.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    for (int l = 0; l < kNumClickLayers; ++l)
        for (int i = 0; i < (int) CP::count; ++i)
        {
            const auto p = (CP) i;
            const auto& s = clickSpec (p);
            const juce::ParameterID id { clickParamId (l, p), 1 };
            const auto name = "Click " + juce::String (l + 1) + " " + s.name;
            const float def = clickDefault (l, p);
            switch (s.kind)
            {
                case CKind::floatP:
                {
                    juce::NormalisableRange<float> r (s.lo, s.hi, 0.001f);
                    if (s.skewMid > 0.0f) r.setSkewForCentre (s.skewMid);
                    layout.add (std::make_unique<juce::AudioParameterFloat> (
                        id, name, r, def, juce::AudioParameterFloatAttributes().withLabel (s.label)));
                    break;
                }
                case CKind::choiceP:
                    layout.add (std::make_unique<juce::AudioParameterChoice> (id, name, choices (p), (int) def));
                    break;
                case CKind::boolP:
                    layout.add (std::make_unique<juce::AudioParameterBool> (id, name, def > 0.5f));
                    break;
            }
        }
}
}
