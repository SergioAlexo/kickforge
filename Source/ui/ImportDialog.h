#pragma once
#include "KfLookAndFeel.h"
#include "analysis/KickImport.h"
#include "core/PreviewPlayer.h"
#include <atomic>
#include <functional>
#include <memory>

namespace kf::ui
{
/** Region (sample indices of the source file) -> ImportOptions (regionStart/End clamped to the file). */
ImportOptions importOptionsForRegion (int start, int end, int totalSamples);

/** Modal-free import panel (P-08 UI): shows the loaded file's waveform with detected hits marked, the
    strongest pre-selected. Click a hit to select it, drag to highlight a custom region, audition the
    selection (through the plugin's own output, see PreviewPlayer), then Import (ImportOptions with regionStart/End) or Cancel.
    File decoding + hit detection run on a background thread; the panel shows a progress bar meanwhile
    (and again while the owner runs the import itself, see setBusy). */
class ImportDialog : public juce::Component, private juce::Timer
{
public:
    ImportDialog (const juce::File& source, std::function<void (const ImportOptions&)> onImport,
                  std::function<void()> onCancel, PreviewPlayer* preview = nullptr);
    ~ImportDialog() override;

    /** Relabels the confirm button (e.g. "Use region" when the dialog only picks a region). */
    void setActionLabel (const juce::String& text, const juce::String& tooltip = {})
    {
        importBtn.setButtonText (text);
        if (tooltip.isNotEmpty()) importBtn.setTooltip (tooltip);
    }

    /** Owner shows analysis progress after Import was pressed (disables the buttons). */
    void setBusy (bool busy, const juce::String& message = {});

    /** Moves a finished background load into the dialog (the timer does this too). Test hook: waits for it. */
    void pollLoader();
    bool waitUntilLoaded (int timeoutMs);
    bool isLoaded() const noexcept { return loaded; }
    const juce::String& getError() const noexcept { return error; }
    int getNumHits() const noexcept { return (int) hits.size(); }
    int getSelectedHit() const noexcept { return selectedHit; }
    int getTotalSamples() const noexcept { return (int) mono.size(); }
    std::pair<int, int> getRegion() const noexcept { return { regionStart, regionEnd }; }
    ImportOptions getOptions() const { return importOptionsForRegion (regionStart, regionEnd, (int) mono.size()); }
    void selectHit (int index);
    void setCustomRegion (int start, int end);
    juce::Rectangle<int> getWaveArea() const noexcept { return waveArea; }
    int sampleToX (int s) const;
    int xToSample (int x) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int preferredW = 720, preferredH = 330;

private:
    struct Loader;

    void timerCallback() override;
    void onLoaded (std::vector<float> data, double rate, std::vector<analysis::Hit> h, juce::String err);
    void rebuildPeaks();
    void updateButtons();
    void toggleAudition();
    void stopAudition();
    juce::String regionText() const;

    juce::File file;
    std::function<void (const ImportOptions&)> importCb;
    std::function<void()> cancelCb;

    std::shared_ptr<std::atomic<bool>> alive { std::make_shared<std::atomic<bool>> (true) };
    std::unique_ptr<Loader> loader;
    PreviewPlayer* player = nullptr; // plays through the plugin's own output (null = no audition)
    bool loaded = false, busy = false, dragging = false, dragInWave = false;
    juce::String error, busyText;

    std::vector<float> mono;
    double sampleRate = 44100.0;
    std::vector<analysis::Hit> hits;
    int selectedHit = -1, regionStart = 0, regionEnd = 0;
    int dragStartX = 0;

    std::vector<std::pair<float, float>> peaks; // per pixel column of waveArea
    int peaksWidth = 0;
    juce::Rectangle<int> waveArea;

    double progress = -1.0; // indeterminate
    juce::ProgressBar progressBar { progress };
    juce::TextButton auditionBtn { "Audition" }, importBtn { "Import" }, cancelBtn { "Cancel" };
};
} // namespace kf::ui
