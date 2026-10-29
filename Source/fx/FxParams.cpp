#include "FxParams.h"

namespace kf
{
namespace
{
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

void addFloat (Layout& l, const juce::String& id, const juce::String& name, float lo, float hi, float def,
               float skewCentre = 0.0f, const juce::String& suffix = {})
{
    juce::NormalisableRange<float> r (lo, hi);
    if (skewCentre > 0.0f) r.setSkewForCentre (skewCentre);
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def,
                                                        juce::AudioParameterFloatAttributes().withLabel (suffix)));
}
void addChoice (Layout& l, const juce::String& id, const juce::String& name, const juce::StringArray& s, int def)
{
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, s, def));
}
void addBool (Layout& l, const juce::String& id, const juce::String& name, bool def)
{
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
}
}

const char* const kFxSlotNames[4] = { "a1", "a2", "b1", "b2" };

juce::StringArray fxTypeNames()
{
    return { "Off", "Clipper", "Tube", "Wave", "Tape", "Drive", "Bit Crush", "Complex", "Brassify",
             "Ring Mod", "Delay", "Reverb", "Filter", "EQ", "Compressor", "FET Comp" };
}

void addFxParams (juce::AudioProcessorValueTreeState::ParameterLayout& l)
{
    // F-01/F-03: two buses (A, B) x two swappable slots. All effect knobs are generic 0..1 (see FxSlot.h).
    juce::StringArray macroTargets { "None" };
    for (int s = 0; s < 4; ++s)
    {
        const juce::String p = juce::String ("fx_") + kFxSlotNames[s];
        const juce::String nm = juce::String (s < 2 ? "Bus A" : "Bus B") + " " + juce::String (s % 2 + 1) + " ";
        addChoice (l, p + "_type", nm + "Type", fxTypeNames(), 0);
        addFloat (l, p + "_a", nm + "Param A", 0.0f, 1.0f, 0.5f);
        addFloat (l, p + "_b", nm + "Param B", 0.0f, 1.0f, 0.5f);
        addFloat (l, p + "_c", nm + "Param C", 0.0f, 1.0f, 0.5f);
        addFloat (l, p + "_mix", nm + "Mix", 0.0f, 1.0f, 1.0f);
        addBool (l, p + "_byp", nm + "Bypass", false);
        for (const char* k : { "A", "B", "C", "Mix" }) macroTargets.add (nm + k);
    }
    addChoice (l, "fx_quality", "Oversampling", { "Off", "2x", "4x", "8x" }, 0); // F-25

    // F-19: 4 macros, each driving one slot parameter (offset added to the knob, clamped 0..1).
    for (int i = 1; i <= 4; ++i)
    {
        const juce::String p = "fx_macro" + juce::String (i), nm = "Macro " + juce::String (i);
        addFloat (l, p, nm, 0.0f, 1.0f, 0.0f);
        addChoice (l, p + "_target", nm + " Target", macroTargets, 0);
        addFloat (l, p + "_amt", nm + " Amount", -1.0f, 1.0f, 1.0f);
    }

    // F-20: master EQ (low cut, low shelf, 2 peaks, high shelf)
    addBool (l, "fx_eq_byp", "EQ Bypass", true);
    addFloat (l, "fx_eq_lc", "EQ Low Cut", 20.0f, 500.0f, 20.0f, 80.0f, "Hz");
    addFloat (l, "fx_eq_ls_f", "EQ Low Shelf Freq", 30.0f, 500.0f, 100.0f, 120.0f, "Hz");
    addFloat (l, "fx_eq_ls_g", "EQ Low Shelf Gain", -18.0f, 18.0f, 0.0f, 0.0f, "dB");
    addFloat (l, "fx_eq_p1_f", "EQ Peak 1 Freq", 40.0f, 2000.0f, 250.0f, 300.0f, "Hz");
    addFloat (l, "fx_eq_p1_g", "EQ Peak 1 Gain", -18.0f, 18.0f, 0.0f, 0.0f, "dB");
    addFloat (l, "fx_eq_p1_q", "EQ Peak 1 Q", 0.3f, 8.0f, 1.0f, 1.0f);
    addFloat (l, "fx_eq_p2_f", "EQ Peak 2 Freq", 300.0f, 12000.0f, 2500.0f, 2000.0f, "Hz");
    addFloat (l, "fx_eq_p2_g", "EQ Peak 2 Gain", -18.0f, 18.0f, 0.0f, 0.0f, "dB");
    addFloat (l, "fx_eq_p2_q", "EQ Peak 2 Q", 0.3f, 8.0f, 1.0f, 1.0f);
    addFloat (l, "fx_eq_hs_f", "EQ High Shelf Freq", 2000.0f, 18000.0f, 8000.0f, 6000.0f, "Hz");
    addFloat (l, "fx_eq_hs_g", "EQ High Shelf Gain", -18.0f, 18.0f, 0.0f, 0.0f, "dB");

    // master compressor
    addBool (l, "fx_comp_byp", "Comp Bypass", true);
    addFloat (l, "fx_comp_thr", "Comp Threshold", -48.0f, 0.0f, -18.0f, 0.0f, "dB");
    addFloat (l, "fx_comp_ratio", "Comp Ratio", 1.0f, 20.0f, 3.0f, 4.0f);
    addFloat (l, "fx_comp_att", "Comp Attack", 0.1f, 100.0f, 10.0f, 10.0f, "ms");
    addFloat (l, "fx_comp_rel", "Comp Release", 10.0f, 1000.0f, 120.0f, 150.0f, "ms");
    addFloat (l, "fx_comp_makeup", "Comp Makeup", 0.0f, 24.0f, 0.0f, 0.0f, "dB");
    addFloat (l, "fx_comp_mix", "Comp Mix", 0.0f, 1.0f, 1.0f);

    // F-22/23/24: master limiter
    addBool (l, "fx_lim_byp", "Limiter Bypass", false);
    addFloat (l, "fx_lim_in", "Limiter Input", 0.0f, 24.0f, 0.0f, 0.0f, "dB");
    addFloat (l, "fx_lim_ceil", "Limiter Ceiling", -12.0f, 0.0f, -0.3f, 0.0f, "dB");
    addChoice (l, "fx_lim_rel", "Limiter Release", { "Fast", "Medium", "Slow" }, 1);
    addChoice (l, "fx_lim_look", "Limiter Lookahead", { "Short", "Medium", "Long" }, 1);
    addFloat (l, "fx_lim_sat", "Limiter Saturation", 0.0f, 1.0f, 0.0f);

    // F-26: master output gain (before the limiter so the ceiling holds)
    addFloat (l, "fx_out", "FX Output Gain", -24.0f, 12.0f, 0.0f, 0.0f, "dB");
}
}
