#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "presets/PresetBar.h"
#include "ui/Analyser.h"
#include "ui/CurveEditor.h"
#include "ui/KfLookAndFeel.h"
#include "ui/Panels.h"
#include "ui/Views.h"

/** Kickforge editor. Laid out on a fixed 1120x740 design canvas that is scaled to the window,
    so resizing keeps the aspect ratio (U-01). */
class KickforgeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int designW = 1120, designH = 740;

    explicit KickforgeEditor (KickforgeProcessor&);
    ~KickforgeEditor() override;
    void resized() override;
    void paint (juce::Graphics&) override {}

    /** Selects the curve shown in the editor: 0..3 = sub Pitch/Amp/Drive/Harm, 4..7 = click layer 1..4 envelope. */
    void selectCurve (int index);
    void selectTab (int index);
    int getCurveIndex() const noexcept { return curveIndex; }
    kf::ui::CurveEditor& getCurveEditor() noexcept { return curveEditor; }

private:
    struct Content : juce::Component
    {
        void paint (juce::Graphics&) override;
    };
    struct HoldButton : juce::TextButton
    {
        std::function<void (bool)> onHold;
        void mouseDown (const juce::MouseEvent& e) override { juce::TextButton::mouseDown (e); if (onHold) onHold (true); }
        void mouseUp (const juce::MouseEvent& e) override { juce::TextButton::mouseUp (e); if (onHold) onHold (false); }
    };

    void timerCallback() override;
    kf::ui::CurveEditor::Source makeSource (int index);
    juce::String fundamentalText() const;

    KickforgeProcessor& proc;
    kf::ui::KfLookAndFeel lnf;
    kf::ui::Analyser analyser;
    Content content;

    kf::PresetBar presetBar;   // header preset area (Source/presets)
    juce::Label fundamental;
    HoldButton audition;

    std::array<juce::TextButton, 8> curveButtons;
    kf::ui::CurveEditor curveEditor;
    kf::ui::WaveformView waveform;
    kf::ui::SpectrumView spectrum;

    std::array<juce::TextButton, 6> tabButtons;
    kf::ui::SubPanel subPanel;
    kf::ui::ClickPanel clickPanel;
    kf::ui::FxPanel fxPanel;
    kf::ui::EqPanel eqPanel;
    kf::ui::DynPanel dynPanel;
    kf::ui::GlobalPanel globalPanel;

    int curveIndex = 0;
    bool lastHzMode = false;
    int lastKickVersion = -1;
    double lastRate = 0.0;
};
