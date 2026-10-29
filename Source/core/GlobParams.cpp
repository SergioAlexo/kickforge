#include "GlobParams.h"

namespace kf
{
void addGlobParams (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    using PID = juce::ParameterID;
    using Range = juce::NormalisableRange<float>;
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        PID { "glob_gain", 1 }, "Master Gain",
        Range (-24.0f, 6.0f, 0.01f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        PID { "glob_a4", 1 }, "A4 Reference", Range (400.0f, 480.0f, 0.1f), 440.0f,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        PID { "glob_bend", 1 }, "Pitch Bend Range", Range (0.0f, 24.0f, 1.0f), 2.0f,
        juce::AudioParameterFloatAttributes().withLabel ("st")));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        PID { "glob_glide", 1 }, "Glide", Range (0.0f, 2000.0f, 1.0f, 0.4f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<juce::AudioParameterBool> (PID { "glob_legato", 1 }, "Legato", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        PID { "glob_velsens", 1 }, "Velocity Sensitivity", Range (0.0f, 1.0f, 0.001f), 0.8f));
}

void GlobParamPtrs::bind (juce::AudioProcessorValueTreeState& s)
{
    a4 = s.getRawParameterValue ("glob_a4");
    bend = s.getRawParameterValue ("glob_bend");
    glide = s.getRawParameterValue ("glob_glide");
    legato = s.getRawParameterValue ("glob_legato");
    vel = s.getRawParameterValue ("glob_velsens");
    bound = true;
}

void GlobParamPtrs::read (GlobSettings& g) const noexcept
{
    if (! bound) return;
    g.a4 = a4->load();
    g.bendRange = bend->load();
    g.glideMs = glide->load();
    g.legato = legato->load() > 0.5f;
    g.velSens = vel->load();
}
}
