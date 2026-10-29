#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PresetManager.h"
#include "KickDragSource.h"
#include "analysis/KickImport.h"
#include "ui/ImportDialog.h"

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

    /** Opens the import region panel for ile on the editor (top-level component). Returns it, or nullptr. */
    ui::ImportDialog* openImportDialog (const juce::File& file);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { updateName(); }
    void timerCallback() override { updateName(); }
    void updateName();
    void showMenu();
    void showSearch();
    void promptName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)>);
    void saveAs (PresetScope);
    void exportWav();
    void importKick();

    KickforgeProcessor& proc;
    PresetManager manager;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "Save" }, menuButton { "=" };
    juce::TextButton nameButton;
    KickDragSource dragSource;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<KickImporter> importer;
    std::unique_ptr<ui::ImportDialog> importDialog;
    bool showFavouritesOnly = false;
};
}
