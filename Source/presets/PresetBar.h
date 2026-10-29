#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PresetManager.h"
#include "KickDragSource.h"

namespace kf
{
/** Self-contained preset header strip: [<] [preset name*] [>] [Save] [menu] [WAV drag handle].
    Owns its PresetManager (accessible for the editor). Needs ~360x28 px minimum. */
class PresetBar : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit PresetBar (KickforgeProcessor&);
    ~PresetBar() override;

    PresetManager& getManager() noexcept { return manager; }
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { updateName(); }
    void timerCallback() override { updateName(); }
    void updateName();
    void showMenu();
    void showSearch();
    void promptName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)>);
    void saveAs (PresetScope);
    void exportWav();

    KickforgeProcessor& proc;
    PresetManager manager;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "Save" }, menuButton { "=" };
    juce::TextButton nameButton;
    KickDragSource dragSource;
    std::unique_ptr<juce::FileChooser> chooser;
    bool showFavouritesOnly = false;
};
}
