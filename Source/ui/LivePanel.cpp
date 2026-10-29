#include "LivePanel.h"
#include "core/Tuning.h"

namespace kf::ui
{
// ------------------------------------------------------------------ Live link toggle
LiveToggle::LiveToggle (KickforgeProcessor& p) : juce::Button ("Live link"), proc (p)
{
    setClickingTogglesState (false);
    setTriggeredOnMouseDown (false);
    refresh();
}

void LiveToggle::refresh()
{
    const auto s = proc.getLiveLink().getStatus();
    if (s.enabled) dot = s.lastApplyOk ? juce::Colour (0xff4cd964) : juce::Colour (0xffff4d4d);
    else           dot = s.superseded ? juce::Colour (0xffffc247) : col::dim.withAlpha (0.55f);

    juce::String tip = s.enabled ? "Live link ON" : (s.superseded ? "Live link OFF - another Kickforge instance took the link" : "Live link OFF");
    tip << "\nFolder: " << s.folder.getFullPathName();
    if (s.enabled)
    {
        tip << "\nApplied from the CLI: " << s.applied;
        if (! s.lastApplyOk) tip << "\nLast apply failed: " << s.error;
    }
    else
        tip << "\nClick to let kickforge-cli read this plugin's state and push changes into it";
    if (tip != getTooltip()) setTooltip (tip);
    if (getToggleState() != s.enabled) setToggleState (s.enabled, juce::dontSendNotification);
    repaint();
}

void LiveToggle::clicked()
{
    proc.getLiveLink().setEnabled (! proc.getLiveLink().isEnabled());
    refresh();
}

void LiveToggle::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();
    auto c = on ? col::panelHi.brighter (0.15f) : col::panelHi;
    if (over) c = c.brighter (0.1f);
    if (down) c = c.brighter (0.2f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? dot.withAlpha (0.8f) : col::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    const float d = 10.0f;
    const auto dotR = juce::Rectangle<float> (r.getX() + 10.0f, r.getCentreY() - d * 0.5f, d, d);
    if (on)
    {
        g.setColour (dot.withAlpha (0.25f));
        g.fillEllipse (dotR.expanded (3.0f));
    }
    g.setColour (dot);
    g.fillEllipse (dotR);
    g.setColour (on ? col::text : col::dim);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText ("LIVE LINK", r.withTrimmedLeft (28.0f).toNearestInt(), juce::Justification::centredLeft);
}

// ------------------------------------------------------------------ Reference strip
namespace
{
juce::String timeText (double seconds) { return juce::String (seconds, seconds < 10.0 ? 3 : 2) + " s"; }
}

ReferenceStrip::ReferenceStrip (KickforgeProcessor& p) : proc (p)
{
    for (auto* b : { &loadBtn, &regionBtn, &clearBtn })
        addAndMakeVisible (*b);
    loadBtn.setTooltip ("Load a kick (WAV / AIFF / FLAC) as the reference. You can also drop a file on this strip.");
    regionBtn.setTooltip ("Pick the hit / region of the file to use as the reference");
    clearBtn.setTooltip ("Remove the reference");
    loadBtn.onClick = [this] { chooseFile(); };
    regionBtn.onClick = [this] { openRegionPicker(); };
    clearBtn.onClick = [this] { proc.getReference().clear(); };

    static const char* names[] = { "Synth", "Reference", "Both" };
    static const char* tips[] = { "Hear only the synth", "Hear only the reference (retriggered on every note, not pitch-shifted)",
                                  "Hear synth and reference together" };
    for (size_t i = 0; i < modeBtns.size(); ++i)
    {
        auto& b = modeBtns[i];
        b.setButtonText (names[i]);
        b.setTooltip (tips[i]);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (103);
        b.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i + 1 < modeBtns.size() ? juce::Button::ConnectedOnRight : 0));
        b.onClick = [this, i] { setModeIndex ((int) i); };
        addAndMakeVisible (b);
    }

    gain.setName ("ref_gain"); // not a parameter: the reference level is plugin state, not automation
    gain.setSliderStyle (juce::Slider::LinearHorizontal);
    gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
    gain.setRange (-24.0, 12.0, 0.1);
    gain.setDoubleClickReturnValue (true, 0.0);
    gain.setTextValueSuffix (" dB");
    gain.setColour (juce::Slider::textBoxOutlineColourId, col::line);
    gain.setColour (juce::Slider::thumbColourId, col::accent);
    gain.setColour (juce::Slider::trackColourId, col::accent.withAlpha (0.6f));
    gain.setColour (juce::Slider::backgroundColourId, col::line);
    gain.setTooltip ("Reference level");
    gain.onValueChange = [this] { proc.getReference().setGainDb ((float) gain.getValue()); };
    addAndMakeVisible (gain);

    proc.getReference().addChangeListener (this);
    refresh();
}

ReferenceStrip::~ReferenceStrip()
{
    proc.getReference().removeChangeListener (this);
}

void ReferenceStrip::setModeIndex (int i)
{
    proc.getReference().setMode ((RefMode) juce::jlimit (0, 2, i));
}

void ReferenceStrip::refresh()
{
    auto& ref = proc.getReference();
    const auto i = ref.getInfo();
    const bool has = i.path.isNotEmpty();

    if (! has)
        summary = "No reference - load a kick or drop a file here";
    else
    {
        const juce::File f (i.path);
        summary = f.getFileName();
        if (! i.loaded)
            summary << "   (file missing or unreadable)";
        else
        {
            const int end = i.regionEnd > 0 ? i.regionEnd : i.totalSamples;
            summary << "   " << timeText (i.regionStart / i.sampleRate) << " - " << timeText (end / i.sampleRate);
            const int note = i.note >= 0 ? i.note
                                         : juce::jlimit (0, 127, (int) std::lround (proc.apvts.getRawParameterValue ("sub_root")->load()));
            summary << "   note " << juce::MidiMessage::getMidiNoteName (note, true, true, 4);
        }
    }
    regionBtn.setEnabled (has && i.loaded);
    clearBtn.setEnabled (has);
    modeBtns[(size_t) i.mode].setToggleState (true, juce::dontSendNotification);
    if (std::abs ((float) gain.getValue() - i.gainDb) > 0.05f)
        gain.setValue (i.gainDb, juce::dontSendNotification);
    repaint();
}

bool ReferenceStrip::loadFile (const juce::File& f)
{
    auto& ref = proc.getReference();
    const bool ok = ref.load (f);
    if (ok && ref.getMode() == RefMode::synth)
        ref.setMode (RefMode::both);   // a freshly loaded reference should be audible
    refresh();
    return ok;
}

void ReferenceStrip::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a reference kick", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac");
    juce::Component::SafePointer<ReferenceStrip> self (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [self] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (self != nullptr && f.existsAsFile())
                                  self->loadFile (f);
                          });
}

void ReferenceStrip::openRegionPicker()
{
    const auto i = proc.getReference().getInfo();
    auto* top = getTopLevelComponent();
    if (! i.loaded || top == nullptr || top == this) return;
    juce::Component::SafePointer<ReferenceStrip> self (this);
    regionDialog.reset();
    regionDialog = std::make_unique<ImportDialog> (
        juce::File (i.path),
        [self] (const ImportOptions& o)
        {
            if (self == nullptr) return;
            self->proc.getReference().setRegion (std::max (0, o.regionStart), std::max (0, o.regionEnd));
            juce::MessageManager::callAsync ([self] { if (self != nullptr) self->regionDialog.reset(); });
        },
        [self] { juce::MessageManager::callAsync ([self] { if (self != nullptr) self->regionDialog.reset(); }); },
        &proc.preview);
    regionDialog->setActionLabel ("Use region", "Use the selected region as the reference");
    top->addAndMakeVisible (*regionDialog);
    const int w = std::min (ImportDialog::preferredW, top->getWidth() - 20);
    const int h = std::min (ImportDialog::preferredH, top->getHeight() - 20);
    regionDialog->setBounds (top->getLocalBounds().withSizeKeepingCentre (std::max (200, w), std::max (150, h)));
    regionDialog->toFront (true);
    regionDialog->grabKeyboardFocus();
}

bool ReferenceStrip::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) return true;
    return false;
}

void ReferenceStrip::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) { loadFile (juce::File (f)); break; }
    repaint();
}

void ReferenceStrip::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::panel);
    g.fillRoundedRectangle (b, 6.0f);
    if (dragOver)
    {
        g.setColour (col::accent);
        g.drawRoundedRectangle (b.reduced (1.0f), 6.0f, 2.0f);
    }
    g.setColour (col::dim);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText ("REFERENCE", 12, 0, 80, getHeight(), juce::Justification::centredLeft);

    const bool has = proc.getReference().hasReference();
    g.setColour (has ? col::text : col::dim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (summary, summaryArea, juce::Justification::centredLeft, true);
}

void ReferenceStrip::resized()
{
    auto r = getLocalBounds().reduced (6, 5);
    r.removeFromLeft (86);
    loadBtn.setBounds (r.removeFromLeft (64));
    r.removeFromLeft (6);
    // right side first
    auto right = r.removeFromRight (3 * 76);
    for (size_t i = 0; i < modeBtns.size(); ++i)
        modeBtns[i].setBounds (right.removeFromLeft (76));
    r.removeFromRight (14);
    gain.setBounds (r.removeFromRight (200).withTrimmedTop (-2));
    r.removeFromRight (10);
    clearBtn.setBounds (r.removeFromRight (28));
    r.removeFromRight (4);
    regionBtn.setBounds (r.removeFromRight (74));
    r.removeFromRight (8);
    summaryArea = r;
}
} // namespace kf::ui
