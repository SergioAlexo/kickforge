#pragma once
#include "CurveSlot.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

namespace kf
{
/** The editable envelope curves of the sub layer. Curve time is NORMALIZED 0..1 over the layer
    length (sub_length). Values:
      Pitch: semitones relative to the base pitch (pitch mode "Semitones") or Hz (mode "Hz").
      Amp:   linear gain 0..1.
      Drive / HarmAmount: 0..1 amounts. */
enum class SubCurveId { Pitch = 0, Amp, Drive, HarmAmount, Count };

const char* subCurveName (SubCurveId) noexcept;

/** Curve defaults that already sound like a modern kick. */
Curve defaultPitchCurveSemitones();
Curve defaultPitchCurveHz();
Curve defaultAmpCurve();
Curve defaultDriveCurve();
Curve defaultHarmAmountCurve();

/** Owns the swappable curve snapshots and mirrors them into the plugin state:
    apvts.state / "CURVES" / "CURVE"(id=...) / "POINT"(t,v,k).
    Message-thread API: setCurve / getCurve / loadFromState.
    Audio-thread API: slot(id).acquire()/release(). */
class SubCurves
{
public:
    explicit SubCurves (juce::AudioProcessorValueTreeState& apvtsToUse);

    void setCurve (SubCurveId, Curve, juce::UndoManager* um = nullptr);
    Curve getCurve (SubCurveId) const;
    void resetToDefault (SubCurveId);
    void resetAll();

    /** Call after the state tree was replaced (setStateInformation / preset load). Missing curves
        fall back to their defaults. */
    void loadFromState();

    CurveSlot& slot (SubCurveId id) noexcept { return *slots[(size_t) id]; }

private:
    static Curve defaultFor (SubCurveId);
    void writeToState (SubCurveId, const Curve&, juce::UndoManager*);

    juce::AudioProcessorValueTreeState& apvts;
    std::array<std::unique_ptr<CurveSlot>, (size_t) SubCurveId::Count> slots;
};
} // namespace kf
