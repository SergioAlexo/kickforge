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
        default: break;
    }
    return "";
}

// Tension: <0 fast start / slow end, >0 slow start / fast end.
Curve defaultPitchCurveSemitones()
{
    // ~ +31 st (about 6x) at the hit, thump-fall to +6 st by 8% of the length (40 ms at 500 ms),
    // settle at the base pitch.
    return Curve ({ { 0.0, 31.0, -0.3 }, { 0.08, 6.0, -0.4 }, { 0.30, 0.0, 0.0 }, { 1.0, 0.0, 0.0 } });
}

Curve defaultPitchCurveHz()
{
    // Reference: 55 Hz base (sub_root A1). Same shape as above, in Hz.
    return Curve ({ { 0.0, 330.0, -0.3 }, { 0.08, 78.0, -0.4 }, { 0.30, 55.0, 0.0 }, { 1.0, 55.0, 0.0 } });
}

Curve defaultAmpCurve()
{
    return Curve ({ { 0.0, 1.0, 0.35 }, { 0.30, 0.82, -0.6 }, { 1.0, 0.0, 0.0 } });
}

Curve defaultDriveCurve()
{
    // Drive hardest on the transient, relaxing toward the tail.
    return Curve ({ { 0.0, 1.0, -0.6 }, { 0.25, 0.25, 0.0 }, { 1.0, 0.0, 0.0 } });
}

Curve defaultHarmAmountCurve()
{
    return Curve ({ { 0.0, 1.0, -0.5 }, { 0.5, 0.35, 0.0 }, { 1.0, 0.0, 0.0 } });
}

Curve SubCurves::defaultFor (SubCurveId id)
{
    switch (id)
    {
        case SubCurveId::Pitch:      return defaultPitchCurveSemitones();
        case SubCurveId::Amp:        return defaultAmpCurve();
        case SubCurveId::Drive:      return defaultDriveCurve();
        case SubCurveId::HarmAmount: return defaultHarmAmountCurve();
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
