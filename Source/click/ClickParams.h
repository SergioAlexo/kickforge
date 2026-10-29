#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace kf
{
/** Number of click/sample layer slots (SPEC S-01: 5 layers total, one is the sub). */
constexpr int kNumClickLayers = 4;

/** Per-layer parameters. ID = "click_<layer 1..4>_<suffix>"; master gain is "click_gain". */
enum class CP
{
    source, tone, decay, color, start, delay, length, pitch, fine, keyTrack,
    gain, pan, filterType, cutoff, resonance, flip, mute, solo, bus, count
};

/** source choice indices */
enum SourceChoice { srcOff = 0, srcSample, srcNoise, srcImpulse, srcChirp, srcBeater, srcCount };
enum FilterChoice { filtOff = 0, filtLP, filtHP, filtBP };
enum BusChoice { busMaster = 0, busA, busB };

enum class CKind { floatP, choiceP, boolP };

struct CPSpec
{
    const char* suffix;
    const char* name;
    CKind kind;
    float lo, hi, def, skewMid; // skewMid > 0: log-style midpoint for float params
    const char* label;
};

const CPSpec& clickSpec (CP p) noexcept;
/** Default value for layer (0-based): layer 0 starts as a Beater click, others off. */
float clickDefault (int layer, CP p) noexcept;
juce::String clickParamId (int layer, CP p);

/** Registers click_* parameters. Owned by the click module. */
void addClickParams (juce::AudioProcessorValueTreeState::ParameterLayout&);
}
