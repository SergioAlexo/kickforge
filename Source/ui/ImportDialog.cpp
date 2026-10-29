#include "ImportDialog.h"
#include <cmath>

namespace kf::ui
{
ImportOptions importOptionsForRegion (int start, int end, int total)
{
    ImportOptions o;
    total = std::max (0, total);
    int a = std::clamp (std::min (start, end), 0, total), b = std::clamp (std::max (start, end), 0, total);
    o.regionStart = a;
    o.regionEnd = b;
    return o;
}

// ------------------------------------------------------------------ background decode + hit detection
struct ImportDialog::Loader : juce::Thread
{
    explicit Loader (juce::File f) : juce::Thread ("Kickforge import load"), file (std::move (f)) {}
    ~Loader() override { stopThread (10000); }

    void run() override
    {
        if (loadAudioMono (file, data, sr, err))
            hits = analysis::detectHits (data.data(), (int) data.size(), sr);
        finished.store (true);
    }

    juce::File file;
    std::vector<float> data;
    double sr = 0;
    juce::String err;
    std::vector<analysis::Hit> hits;
    std::atomic<bool> finished { false };
};

// ------------------------------------------------------------------ dialog
ImportDialog::ImportDialog (const juce::File& source, std::function<void (const ImportOptions&)> onImport,
                            std::function<void()> onCancel, PreviewPlayer* previewPlayer)
    : file (source), importCb (std::move (onImport)), cancelCb (std::move (onCancel)), player (previewPlayer)
{
    setWantsKeyboardFocus (true);
    for (auto* b : { &auditionBtn, &importBtn, &cancelBtn }) addAndMakeVisible (*b);
    addAndMakeVisible (progressBar);
    auditionBtn.setTooltip ("Play the selected region");
    importBtn.setTooltip ("Analyse the selected region and load it into the synth");
    auditionBtn.onClick = [this] { toggleAudition(); };
    importBtn.onClick = [this]
    {
        if (busy || ! loaded) return;
        stopAudition();
        if (importCb) importCb (getOptions());
    };
    cancelBtn.onClick = [this] { stopAudition(); if (cancelCb) cancelCb(); };
    updateButtons();
    setSize (preferredW, preferredH);
    loader = std::make_unique<Loader> (file);
    loader->startThread();
    startTimerHz (30);
}

ImportDialog::~ImportDialog()
{
    alive->store (false);
    stopTimer();
    loader.reset();
    stopAudition();
}

void ImportDialog::pollLoader()
{
    if (loader != nullptr && loader->finished.load())
    {
        loader->stopThread (5000);
        auto l = std::move (loader);
        onLoaded (std::move (l->data), l->sr, std::move (l->hits), l->err);
    }
}

bool ImportDialog::waitUntilLoaded (int timeoutMs)
{
    for (int t = 0; t < timeoutMs && ! loaded && error.isEmpty(); t += 10)
    {
        juce::Thread::sleep (10);
        pollLoader();
    }
    return loaded;
}

void ImportDialog::setBusy (bool b, const juce::String& message)
{
    busy = b;
    busyText = message;
    updateButtons();
    repaint();
}

void ImportDialog::updateButtons()
{
    const bool ok = loaded && ! busy && regionEnd - regionStart >= 64;
    importBtn.setEnabled (ok);
    auditionBtn.setEnabled (ok && player != nullptr);
    cancelBtn.setEnabled (! busy);
    progressBar.setVisible (! loaded && error.isEmpty() ? true : busy);
}

void ImportDialog::onLoaded (std::vector<float> data, double rate, std::vector<analysis::Hit> h, juce::String err)
{
    mono = std::move(data);
    sampleRate = rate > 0 ? rate : 44100.0;
    hits = std::move (h);
    error = err;
    loaded = error.isEmpty() && ! mono.empty();
    if (loaded && hits.empty()) error = "No hit found (the file looks silent). Drag to select a region manually.";
    peaksWidth = 0;
    if (loaded)
    {
        const int s = analysis::strongestHit (hits);
        if (s >= 0) selectHit (s);
        else setCustomRegion (0, (int) mono.size());
    }
    loader.reset();
    updateButtons();
    repaint();
}

void ImportDialog::selectHit (int index)
{
    if (index < 0 || index >= (int) hits.size()) return;
    stopAudition();
    selectedHit = index;
    regionStart = hits[(size_t) index].start;
    regionEnd = hits[(size_t) index].end;
    updateButtons();
    repaint();
}

void ImportDialog::setCustomRegion (int s, int e)
{
    const int n = (int) mono.size();
    regionStart = std::clamp (std::min (s, e), 0, n);
    regionEnd = std::clamp (std::max (s, e), 0, n);
    selectedHit = -1;
    updateButtons();
    repaint();
}

int ImportDialog::sampleToX (int s) const
{
    const double n = std::max (1.0, (double) mono.size());
    return waveArea.getX() + (int) std::lround ((double) s / n * waveArea.getWidth());
}

int ImportDialog::xToSample (int x) const
{
    const double u = std::clamp ((double) (x - waveArea.getX()) / std::max (1, waveArea.getWidth()), 0.0, 1.0);
    return (int) std::lround (u * (double) mono.size());
}

void ImportDialog::resized()
{
    auto r = getLocalBounds().reduced (14);
    r.removeFromTop (26);
    auto bottom = r.removeFromBottom (28);
    r.removeFromBottom (6);
    r.removeFromBottom (18); // info line
    waveArea = r;
    cancelBtn.setBounds (bottom.removeFromRight (90));
    bottom.removeFromRight (8);
    importBtn.setBounds (bottom.removeFromRight (110));
    bottom.removeFromRight (8);
    auditionBtn.setBounds (bottom.removeFromRight (100));
    progressBar.setBounds (bottom.reduced (0, 4));
    peaksWidth = 0;
}

void ImportDialog::rebuildPeaks()
{
    const int w = waveArea.getWidth();
    peaks.assign ((size_t) std::max (0, w), { 0.0f, 0.0f });
    const size_t n = mono.size();
    if (n == 0 || w <= 0) return;
    for (int px = 0; px < w; ++px)
    {
        const size_t a = (size_t) ((double) px / w * (double) n), b = std::max (a + 1, (size_t) ((double) (px + 1) / w * (double) n));
        const size_t step = std::max<size_t> (1, (b - a) / 256);
        float lo = 0, hi = 0;
        for (size_t i = a; i < std::min (b, n); i += step) { lo = std::min (lo, mono[i]); hi = std::max (hi, mono[i]); }
        peaks[(size_t) px] = { lo, hi };
    }
    peaksWidth = w;
}

juce::String ImportDialog::regionText() const
{
    auto t = [this] (int s) { return juce::String ((double) s / sampleRate, 3) + " s"; };
    const double ms = (double) (regionEnd - regionStart) / sampleRate * 1000.0;
    juce::String head = selectedHit >= 0 ? "Hit " + juce::String (selectedHit + 1) + " of " + juce::String ((int) hits.size())
                                         : juce::String ("Custom region");
    return head + "   " + t (regionStart) + " - " + t (regionEnd) + "  (" + juce::String ((int) ms) + " ms)";
}

void ImportDialog::paint (juce::Graphics& g)
{
    using namespace col;
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (getLocalBounds().toFloat().translated (0, 3), 10.0f);
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2), 8.0f);
    g.setColour (line.brighter (0.2f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2), 8.0f, 1.0f);

    g.setColour (text);
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    g.drawText ("Import kick  -  " + file.getFileName(), 14, 12, getWidth() - 28, 22, juce::Justification::centredLeft);

    g.setColour (bg);
    g.fillRect (waveArea);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));

    if (! loaded)
    {
        g.setColour (error.isNotEmpty() ? juce::Colour (0xffe0507a) : dim);
        g.drawText (error.isNotEmpty() ? error : "Analysing " + file.getFileName() + " ...", waveArea,
                    juce::Justification::centred);
        return;
    }
    if (peaksWidth != waveArea.getWidth()) const_cast<ImportDialog*> (this)->rebuildPeaks();

    // hit regions + selection
    for (size_t i = 0; i < hits.size(); ++i)
    {
        const int x0 = sampleToX (hits[i].start), x1 = std::max (x0 + 2, sampleToX (hits[i].end));
        const bool sel = (int) i == selectedHit;
        g.setColour ((sel ? accent : accent2).withAlpha (sel ? 0.28f : 0.10f));
        g.fillRect (x0, waveArea.getY(), x1 - x0, waveArea.getHeight());
    }
    if (selectedHit < 0 && regionEnd > regionStart)
    {
        const int x0 = sampleToX (regionStart), x1 = std::max (x0 + 2, sampleToX (regionEnd));
        g.setColour (accent.withAlpha (0.28f));
        g.fillRect (x0, waveArea.getY(), x1 - x0, waveArea.getHeight());
        g.setColour (accent);
        g.drawVerticalLine (x0, (float) waveArea.getY(), (float) waveArea.getBottom());
        g.drawVerticalLine (x1, (float) waveArea.getY(), (float) waveArea.getBottom());
    }

    // waveform
    const float midY = (float) waveArea.getCentreY(), half = waveArea.getHeight() * 0.5f - 2.0f;
    float peak = 0.001f;
    for (auto& p : peaks) peak = std::max ({ peak, -p.first, p.second });
    g.setColour (text.withAlpha (0.85f));
    for (int px = 0; px < (int) peaks.size(); ++px)
    {
        const float x = (float) (waveArea.getX() + px);
        g.drawVerticalLine ((int) x, midY - peaks[(size_t) px].second / peak * half,
                            midY - peaks[(size_t) px].first / peak * half + 1.0f);
    }
    g.setColour (line);
    g.drawHorizontalLine ((int) midY, (float) waveArea.getX(), (float) waveArea.getRight());

    // hit markers
    for (size_t i = 0; i < hits.size(); ++i)
    {
        const int x = sampleToX (hits[i].start);
        const bool sel = (int) i == selectedHit;
        g.setColour (sel ? accent : accent2);
        g.drawVerticalLine (x, (float) waveArea.getY(), (float) waveArea.getBottom());
        juce::Path flag;
        flag.addTriangle ((float) x, (float) waveArea.getY(), (float) x + 12.0f, (float) waveArea.getY(),
                          (float) x, (float) waveArea.getY() + 12.0f);
        g.fillPath (flag);
        g.drawText (juce::String ((int) i + 1), x + 3, waveArea.getY() + 10, 20, 14, juce::Justification::centredLeft);
    }
    // audition cursor
    if (player != nullptr && player->isPlaying())
    {
        const int x = sampleToX (regionStart + (int) ((regionEnd - regionStart) * player->getProgress()));
        g.setColour (juce::Colours::white);
        g.drawVerticalLine (x, (float) waveArea.getY(), (float) waveArea.getBottom());
    }

    g.setColour (dim);
    g.drawText (busy ? busyText : regionText(), 14, waveArea.getBottom() + 2, getWidth() - 28, 16, juce::Justification::centredLeft);
    g.drawText (hits.size() > 1 ? "Click a hit to select it, or drag to highlight your own region" : "Drag to highlight a region",
                14, waveArea.getBottom() + 2, getWidth() - 28, 16, juce::Justification::centredRight);
}

void ImportDialog::timerCallback()
{
    if (! loaded || busy || (player != nullptr && player->isPlaying())) repaint();
    if (player != nullptr && ! player->isPlaying() && auditionBtn.getButtonText() != "Audition")
        auditionBtn.setButtonText ("Audition");
}

void ImportDialog::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragging = false;
    dragStartX = e.x;
    dragInWave = waveArea.contains (e.getPosition());
}

void ImportDialog::mouseDrag (const juce::MouseEvent& e)
{
    if (! loaded || busy || ! dragInWave) return;
    if (! dragging && std::abs (e.x - dragStartX) < 5) return;
    dragging = true;
    stopAudition();
    setCustomRegion (xToSample (dragStartX), xToSample (e.x));
}

void ImportDialog::mouseUp (const juce::MouseEvent& e)
{
    if (dragging || ! loaded || busy || ! waveArea.contains (e.getPosition())) { dragging = false; return; }
    const int s = xToSample (e.x);
    int best = -1;
    for (size_t i = 0; i < hits.size(); ++i)
        if (s >= hits[i].start && s < hits[i].end) best = (int) i;
    if (best < 0) // nearest hit start marker within 10 px
    {
        int bd = 11;
        for (size_t i = 0; i < hits.size(); ++i)
            if (const int d = std::abs (sampleToX (hits[i].start) - e.x); d < bd) { bd = d; best = (int) i; }
    }
    if (best >= 0) selectHit (best);
}

bool ImportDialog::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && ! busy) { stopAudition(); if (cancelCb) cancelCb(); return true; }
    if (k == juce::KeyPress::returnKey && importBtn.isEnabled()) { importBtn.triggerClick(); return true; }
    if (k == juce::KeyPress::spaceKey && auditionBtn.isEnabled()) { toggleAudition(); return true; }
    return false;
}

void ImportDialog::toggleAudition()
{
    if (player != nullptr && player->isPlaying()) { stopAudition(); return; }
    if (! loaded || regionEnd - regionStart < 64) return;
    if (player == nullptr) return;
    std::vector<float> region (mono.begin() + regionStart, mono.begin() + regionEnd);
    player->play (std::move (region), sampleRate);
    auditionBtn.setButtonText ("Stop");
}

void ImportDialog::stopAudition()
{
    if (player != nullptr) player->stop();
    auditionBtn.setButtonText ("Audition");
}
} // namespace kf::ui
