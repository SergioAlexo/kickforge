#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "KickRenderer.h"
#include "PluginProcessor.h"

namespace kf
{
/** Small grip: drag it onto a DAW track to drop the current kick as a 24-bit WAV (P-05).
    The WAV is rendered offline from the current state on drag start. */
class KickDragSource : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit KickDragSource (KickforgeProcessor& p, std::function<juce::String()> nameProvider = {})
        : proc (p), getName_ (std::move (nameProvider))
    {
        setTooltip ("Drag the current kick into your DAW as a WAV file");
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.setFont (juce::FontOptions (juce::jmin (13.0f, r.getHeight() * 0.6f), juce::Font::bold));
        g.drawText ("WAV", r, juce::Justification::centred);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 4) return;
        dragging = true;
        const auto file = renderToTemp();
        if (file.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles (
                { file.getFullPathName() }, false, this, [this] { dragging = false; });
        else
            dragging = false;
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

    /** Renders the current kick to <temp>/Kickforge/<name>.wav; returns the file. */
    juce::File renderToTemp() const
    {
        auto name = getName_ ? getName_() : juce::String ("Kickforge Kick");
        if (name.isEmpty()) name = "Kickforge Kick";
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Kickforge");
        auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".wav");
        RenderOptions o;
        const auto buf = renderKick (proc, o);
        return writeWav24 (file, buf, o.sampleRate) ? file : juce::File();
    }

private:
    KickforgeProcessor& proc;
    std::function<juce::String()> getName_;
    bool dragging = false;
};
}
