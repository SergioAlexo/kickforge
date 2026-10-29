#pragma once
#include "ImportDialog.h"
#include "KfLookAndFeel.h"
#include "PluginProcessor.h"
#include <array>

namespace kf::ui
{
/** Header "Live link" toggle: a pill with a status dot (grey = off, green = linked, red = last apply failed,
    amber = another instance took the link). The tooltip names the folder. */
class LiveToggle : public juce::Button
{
public:
    explicit LiveToggle (KickforgeProcessor&);
    /** Re-reads the link status (message thread; called from the editor timer). */
    void refresh();
    juce::Colour getDotColour() const noexcept { return dot; }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void clicked() override;

private:
    KickforgeProcessor& proc;
    juce::Colour dot;
};

/** AI-link Reference strip: load button + drag-and-drop, region pick (ImportDialog's waveform/hit picker),
    clear, gain, and the Synth / Reference / Both control. Settings live in KickforgeProcessor::getReference(). */
class ReferenceStrip : public juce::Component, public juce::FileDragAndDropTarget, private juce::ChangeListener
{
public:
    explicit ReferenceStrip (KickforgeProcessor&);
    ~ReferenceStrip() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

    bool loadFile (const juce::File&);
    void chooseFile();
    void openRegionPicker();
    void setModeIndex (int index);
    /** Syncs the controls with the Reference (also done on every change message). */
    void refresh();
    ImportDialog* getRegionDialog() noexcept { return regionDialog.get(); }
    const juce::String& getSummary() const noexcept { return summary; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    KickforgeProcessor& proc;
    juce::TextButton loadBtn { "Load..." }, regionBtn { "Region..." }, clearBtn { "X" };
    std::array<juce::TextButton, 3> modeBtns;
    juce::Slider gain;
    juce::String summary;
    juce::Rectangle<int> summaryArea;
    bool dragOver = false;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<ImportDialog> regionDialog;
};
} // namespace kf::ui
