#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

namespace kf::ui
{
namespace col
{
inline const juce::Colour bg { 0xff0e1014 }, panel { 0xff161a21 }, panelHi { 0xff1f242e }, line { 0xff2c3340 },
    text { 0xffe4e7ee }, dim { 0xff8a92a3 }, accent { 0xffff7a2f }, accent2 { 0xff35d0c0 };
}

/** Original dark "forge" look: charcoal panels, ember-orange accent, teal for analysis. */
class KfLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KfLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle,
                           float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::Font (juce::FontOptions (13.0f)); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (13.0f)); }
    juce::Font getLabelFont (juce::Label&) override { return juce::Font (juce::FontOptions (12.0f)); }
};
} // namespace kf::ui
