#pragma once

namespace kf::ui
{
/** Knob captions per FxSlot type (index = FxSlot::Type, see fx/FxSlot.h).
    nullptr = control unused by that type. */
struct FxKnobLabels { const char *a, *b, *c, *mix; };

inline FxKnobLabels fxKnobLabels (int type)
{
    static const FxKnobLabels t[] = {
        { nullptr, nullptr, nullptr, nullptr },       // Off
        { "Drive", "Tone", "Ceiling", "Mix" },        // Clipper
        { "Drive", "Tone", "Asymmetry", "Mix" },      // Tube
        { "Drive", "Tone", "Fold", "Mix" },           // Wave
        { "Drive", "Tone", "Head Bump", "Mix" },      // Tape
        { "Drive", "Tone", "Mid Boost", "Mix" },      // Drive
        { "Bits", "Downsample", nullptr, "Mix" },     // BitCrush
        { "Drive", "Stages", "Tone", "Mix" },         // Complex
        { "Amount", "Corner", "Even/Odd", "Mix" },    // Brassify
        { "Freq", "Depth", "Square", "Mix" },         // RingMod
        { "Time", "Feedback", "Damping", "Send" },    // Delay
        { "Room", "Damping", "Width", "Send" },       // Reverb
        { "Cutoff", "Resonance", "LP/HP/BP", "Mix" }, // Filter
        { "Low", "Mid", "High", "Mix" },              // Eq
        { "Threshold", "Ratio", "Character", "Mix" }, // Comp
        { "Input", "Output", "Ratio", "Mix" },        // FetComp
    };
    constexpr int n = (int) (sizeof (t) / sizeof (t[0]));
    return t[type < 0 ? 0 : (type >= n ? 0 : type)];
}
} // namespace kf::ui
