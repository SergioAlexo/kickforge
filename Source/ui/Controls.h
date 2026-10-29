#pragma once
#include "KfLookAndFeel.h"
#include <memory>

namespace kf::ui
{
using APVTS = juce::AudioProcessorValueTreeState;

/** Compact numeric text for knob read-outs. */
inline juce::String formatKnobValue (double v, const juce::String& unit)
{
    if (std::abs (v) < 0.005) v = 0.0; // avoid "-0.00"
    const double a = std::abs (v);
    juce::String s;
    if (a >= 10000.0) s = juce::String (v / 1000.0, 1) + "k";
    else if (a >= 1000.0) s = juce::String (v / 1000.0, 2) + "k";
    else if (a >= 100.0) s = juce::String (v, 0);
    else if (a >= 10.0) s = juce::String (v, 1);
    else s = juce::String (v, 2);
    return unit.isEmpty() ? s : s + " " + unit;
}

/** Rotary knob + caption, bound to a float parameter through a SliderAttachment. */
class Knob : public juce::Component
{
public:
    Knob (APVTS& state, const juce::String& paramId, const juce::String& caption)
    {
        addAndMakeVisible (title);
        addAndMakeVisible (slider);
        title.setText (caption, juce::dontSendNotification);
        title.setJustificationType (juce::Justification::centred);
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 14);
        slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
        attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, slider);
        if (auto* p = state.getParameter (paramId))
        {
            const juce::String unit = p->getLabel();
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
            const bool offAtMin = unit == "dB" && p->convertFrom0to1 (0.0f) <= -59.99f;
            if (unit == "dB") slider.getProperties().set ("unipolar", true); // level knobs fill from the minimum, not from 0 dB
            slider.textFromValueFunction = [unit, offAtMin] (double v) { return offAtMin && v <= -59.99 ? juce::String ("Off") : formatKnobValue (v, unit); };
            slider.valueFromTextFunction = [unit] (const juce::String& t)
            {
                auto s = t.trim();
                const bool k = s.containsIgnoreCase ("k") && ! unit.containsIgnoreCase ("k");
                return (double) s.getFloatValue() * (k ? 1000.0 : 1.0);
            };
            slider.updateText();
        }
        slider.setName (paramId);
    }

    void setCaption (const juce::String& c) { title.setText (c, juce::dontSendNotification); }
    void setActive (bool a) { slider.setEnabled (a); title.setEnabled (a); title.setAlpha (a ? 1.0f : 0.4f); }

    void resized() override
    {
        auto r = getLocalBounds();
        title.setBounds (r.removeFromTop (14));
        slider.setBounds (r);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, std::max (40, getWidth() - 6), 14);
    }

    juce::Label title;
    juce::Slider slider;

private:
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

/** Drop-down bound to a choice parameter. */
class Choice : public juce::ComboBox
{
public:
    Choice (APVTS& state, const juce::String& paramId)
    {
        setName (paramId);
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
            addItemList (p->choices, 1);
        attachment = std::make_unique<APVTS::ComboBoxAttachment> (state, paramId, *this);
    }

private:
    std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
};

/** Toggle button bound to a bool parameter. */
class Toggle : public juce::ToggleButton
{
public:
    Toggle (APVTS& state, const juce::String& paramId, const juce::String& text)
    {
        setName (paramId);
        setButtonText (text);
        attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, *this);
    }

private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

/** Places items left-to-right in fixed cells, wrapping at the area's right edge. nullptr = empty cell. */
inline void flowCells (juce::Rectangle<int> area, std::initializer_list<juce::Component*> items, int cellW, int cellH)
{
    int x = area.getX(), y = area.getY();
    for (auto* c : items)
    {
        if (x + cellW > area.getRight() + 1) { x = area.getX(); y += cellH; }
        if (c != nullptr) c->setBounds (x, y, cellW, cellH);
        x += cellW;
    }
}
} // namespace kf::ui
