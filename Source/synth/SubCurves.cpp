#include "SubCurves.h"

namespace kf
{
const char* subCurveName (SubCurveId id) noexcept
{
    switch (id)
    {
        case SubCurveId::Pitch:       return "sub_pitch";
        case SubCurveId::Amp:         return "sub_amp";
        case SubCurveId::Drive:       return "sub_drive";
        case SubCurveId::HarmAmount:  return "sub_harm";
        case SubCurveId::KeyTrack:    return "sub_kt";
        default: break;
    }
    return "";
}

// Shapes are written as chains of near-linear segments sampled from exponentials: a single
// segment with negative tension starts with a vertical tangent, which looks (and sounds) like a
// cliff, so the fast-then-slow decays here use several nodes with tension 0 instead.
Curve defaultPitchCurveSemitones()
{
    // ~ +31 st (about 6x) at the hit, thump-fall to +6 st by 8% of the length (40 ms at 500 ms),
    // then a gentle glide down to the base pitch.
    return Curve ({ { 0.0, 31.0, 0.0 }, { 0.01, 22.0, 0.0 }, { 0.03, 12.0, 0.0 }, { 0.08, 6.0, 0.0 },
                    { 0.16, 2.0, 0.0 }, { 0.30, 0.0, 0.0 }, { 1.0, 0.0, 0.0 } });
}

Curve defaultPitchCurveHz()
{
    // Reference: 55 Hz base (sub_root A1). Same shape as above, in Hz.
    return Curve ({ { 0.0, 330.0, 0.0 }, { 0.01, 196.0, 0.0 }, { 0.03, 110.0, 0.0 }, { 0.08, 78.0, 0.0 },
                    { 0.16, 62.0, 0.0 }, { 0.30, 55.0, 0.0 }, { 1.0, 55.0, 0.0 } });
}

Curve defaultAmpCurve()
{
    // Full-level hold for the punch (10% of the length), then a smooth exponential decay
    // (~ -44 dB over the remaining 90%) that reaches silence at the end of the layer.
    return Curve ({ { 0.0, 1.0, 0.0 }, { 0.10, 1.0, 0.0 }, { 0.20, 0.574, 0.0 }, { 0.35, 0.249, 0.0 },
                    { 0.55, 0.082, 0.0 }, { 0.75, 0.027, 0.0 }, { 1.0, 0.0, 0.0 } });
}

Curve defaultDriveCurve()
{
    // Drive hardest on the transient, relaxing exponentially toward the tail.
    return Curve ({ { 0.0, 1.0, 0.0 }, { 0.05, 0.62, 0.0 }, { 0.12, 0.38, 0.0 }, { 0.25, 0.2, 0.0 },
                    { 0.5, 0.07, 0.0 }, { 1.0, 0.0, 0.0 } });
}

Curve defaultHarmAmountCurve()
{
    return Curve ({ { 0.0, 1.0, 0.0 }, { 0.1, 0.7, 0.0 }, { 0.25, 0.5, 0.0 }, { 0.5, 0.3, 0.0 },
                    { 0.75, 0.12, 0.0 }, { 1.0, 0.0, 0.0 } });
}

/** Selective key tracking (S-15): per-time weight 0..1 of how much the MIDI note transposes the pitch. */
Curve defaultKeyTrackCurve()
{
    return Curve ({ { 0.0, 1.0, 0.0 }, { 1.0, 1.0, 0.0 } });
}

Curve SubCurves::defaultFor (SubCurveId id)
{
    switch (id)
    {
        case SubCurveId::Pitch:      return defaultPitchCurveSemitones();
        case SubCurveId::Amp:        return defaultAmpCurve();
        case SubCurveId::Drive:      return defaultDriveCurve();
        case SubCurveId::HarmAmount: return defaultHarmAmountCurve();
        case SubCurveId::KeyTrack:   return defaultKeyTrackCurve();
        default: break;
    }
    return {};
}

SubCurves::SubCurves (juce::AudioProcessorValueTreeState& a) : apvts (a)
{
    for (size_t i = 0; i < slots.size(); ++i)
        slots[i] = std::make_unique<CurveSlot> (defaultFor ((SubCurveId) i));
}

void SubCurves::writeToState (SubCurveId id, const Curve& c, juce::UndoManager* um)
{
    auto root = apvts.state;
    auto curves = root.getOrCreateChildWithName ("CURVES", um);
    juce::ValueTree v = c.toValueTree();
    v.setProperty ("id", subCurveName (id), nullptr);
    for (int i = 0; i < curves.getNumChildren(); ++i)
        if (curves.getChild (i).getProperty ("id").toString() == subCurveName (id))
        {
            curves.removeChild (i, um);
            break;
        }
    curves.appendChild (v, um);
}

void SubCurves::setCurve (SubCurveId id, Curve c, juce::UndoManager* um)
{
    slots[(size_t) id]->set (c);
    writeToState (id, c, um);
}

Curve SubCurves::getCurve (SubCurveId id) const { return slots[(size_t) id]->get(); }

void SubCurves::resetToDefault (SubCurveId id) { setCurve (id, defaultFor (id)); }

void SubCurves::resetAll()
{
    for (size_t i = 0; i < slots.size(); ++i)
        resetToDefault ((SubCurveId) i);
}

void SubCurves::loadFromState()
{
    auto curves = apvts.state.getChildWithName ("CURVES");
    for (size_t i = 0; i < slots.size(); ++i)
    {
        const auto id = (SubCurveId) i;
        Curve c = defaultFor (id);
        for (auto child : curves)
            if (child.getProperty ("id").toString() == subCurveName (id))
            {
                auto loaded = Curve::fromValueTree (child);
                if (! loaded.empty()) c = std::move (loaded);
                break;
            }
        if (! (c == slots[i]->get()))
            slots[i]->set (std::move (c));
    }
}
} // namespace kf
