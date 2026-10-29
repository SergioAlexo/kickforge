#include "KfLookAndFeel.h"

namespace kf::ui
{
KfLookAndFeel::KfLookAndFeel()
{
    using namespace col;
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::Label::textColourId, dim);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, panelHi);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.4f));
    setColour (juce::ComboBox::backgroundColourId, panelHi);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, line);
    setColour (juce::ComboBox::arrowColourId, dim);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.8f));
    setColour (juce::PopupMenu::highlightedTextColourId, bg);
    setColour (juce::TextButton::buttonColourId, panelHi);
    setColour (juce::TextButton::buttonOnColourId, accent);
    setColour (juce::TextButton::textColourOffId, dim);
    setColour (juce::TextButton::textColourOnId, bg);
    setColour (juce::ToggleButton::textColourId, dim);
}

void KfLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                      float startAngle, float endAngle, juce::Slider& s)
{
    using namespace col;
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float d = std::min (b.getWidth(), b.getHeight()), r = d * 0.5f;
    const auto c = b.getCentre();
    const float arcR = r - 2.0f, ang = startAngle + pos * (endAngle - startAngle);
    const bool en = s.isEnabled();

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0, startAngle, endAngle, true);
    g.setColour (line);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float from = bipolar ? startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle) : startAngle;
    juce::Path val;
    val.addCentredArc (c.x, c.y, arcR, arcR, 0, std::min (from, ang), std::max (from, ang), true);
    g.setColour (en ? accent : dim.withAlpha (0.4f));
    g.strokePath (val, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (panelHi);
    g.fillEllipse (c.x - arcR + 5, c.y - arcR + 5, (arcR - 5) * 2, (arcR - 5) * 2);
    const float pr = arcR - 5;
    g.setColour (en ? text : dim);
    g.drawLine (c.x + std::sin (ang) * pr * 0.35f, c.y - std::cos (ang) * pr * 0.35f,
                c.x + std::sin (ang) * pr * 0.95f, c.y - std::cos (ang) * pr * 0.95f, 2.0f);
}

void KfLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    using namespace col;
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? accent : (over ? panelHi.brighter (0.1f) : panelHi));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? bg : dim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (b.getButtonText(), r.toNearestInt(), juce::Justification::centred);
}

void KfLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    using namespace col;
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    juce::Colour c = b.getToggleState() ? accent : panelHi;
    if (over) c = c.brighter (0.12f);
    if (down) c = c.brighter (0.25f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
}

void KfLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    using namespace col;
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (panelHi);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path p;
    const float cx = (float) w - 11.0f, cy = (float) h * 0.5f;
    p.addTriangle (cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
    g.setColour (box.isEnabled() ? dim : line);
    g.fillPath (p);
}
} // namespace kf::ui
