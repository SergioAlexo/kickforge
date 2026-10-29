#pragma once
#include "Analyser.h"
#include "Controls.h"
#include "Views.h"
#include "click/ClickLayer.h"
#include <array>
#include <functional>

namespace kf::ui
{
/** Rounded panel background shared by all tab pages. */
class PanelBase : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        g.setColour (col::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    }
protected:
    static void heading (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r)
    {
        g.setColour (col::accent);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText (t, r, juce::Justification::centredLeft);
    }
};

class SubPanel : public PanelBase
{
public:
    explicit SubPanel (APVTS&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    Knob level, phase, root, tune, fine, length, release, drive, lockPos, delay, pan;
    Choice wave, pitchMode, gate, bus;
    Toggle keyTrack, mute, lock;
};

/** Sub additive harmonics: 9 columns of pitch / gain / decay (sub_hN_*). */
class HarmonicsPanel : public PanelBase
{
public:
    explicit HarmonicsPanel (APVTS&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    static constexpr int kN = 9;
    std::vector<std::unique_ptr<Knob>> knobs; // [i*3 + {0 semi,1 gain,2 decay}]
};

/** One click/sample layer column: generator, drop zone / sample status, knobs. (C-08) */
class LayerStrip : public PanelBase, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    LayerStrip (APVTS&, ClickLayer&, int layer, std::function<void (int)> onSelectEnvelope);
    ~LayerStrip() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

    /** Loads a file into this layer and switches the generator to "Sample". */
    void loadFile (const juce::File&);
    static bool isAudioFile (const juce::File&);
    static double sessionRate; // set by the editor from the processor's sample rate
    juce::String statusText() const;

private:
    void timerCallback() override;
    APVTS& apvts;
    ClickLayer& click;
    int index;
    bool dragOver = false;
    juce::String lastStatus;
    juce::Rectangle<int> dropZone;
    std::unique_ptr<juce::FileChooser> chooser;

    Choice source, bus, filter;
    Toggle mute, solo, flip, key;
    juce::TextButton envBtn { "Env" };
    Knob tone, decay, color, start, delay, length, pitch, fine, gain, pan, cutoff, res;
};

class ClickPanel : public PanelBase
{
public:
    ClickPanel (APVTS&, ClickLayer&, std::function<void (int)> onSelectEnvelope);
    void resized() override;
    LayerStrip& getStrip (int i) { return *strips[(size_t) i]; }
private:
    std::array<std::unique_ptr<LayerStrip>, kNumClickLayers> strips;
};

class FxSlotPanel : public PanelBase, private juce::Timer
{
public:
    FxSlotPanel (APVTS&, int slot);
    ~FxSlotPanel() override { stopTimer(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshLabels();
private:
    void timerCallback() override { refreshLabels(); }
    APVTS& apvts;
    int slot;
    int shownType = -1;
    Choice type;
    Toggle bypass;
    Knob a, b, c, mix;
};

class FxPanel : public PanelBase
{
public:
    explicit FxPanel (APVTS&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    std::array<std::unique_ptr<FxSlotPanel>, 4> slots;
    Choice quality;
    struct MacroRow
    {
        MacroRow (APVTS&, int i);
        Knob amount, value;
        Choice target;
    };
    std::array<std::unique_ptr<MacroRow>, 4> macros;
    friend struct MacroLayout;
};

class EqPanel : public PanelBase
{
public:
    EqPanel (APVTS&, Analyser&);
    void resized() override;
private:
    SpectrumView spectrum;
    Toggle bypass;
    Knob lc, lsF, lsG, p1F, p1G, p1Q, p2F, p2G, p2Q, hsF, hsG;
};

class DynPanel : public PanelBase
{
public:
    explicit DynPanel (APVTS&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    Toggle compByp, limByp;
    Knob thr, ratio, att, rel, makeup, cmix, lin, ceil, sat, out;
    Choice limRel, limLook;
};

class GlobalPanel : public PanelBase
{
public:
    explicit GlobalPanel (APVTS&);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    Knob master, clickMaster, a4, bend, glide, vel;
    Toggle legato;
    Toggle trigEnable, trigDyn, moEnable, moFollow;
    Knob trigHp, trigLp, trigThresh, trigOff, trigRetrig, trigNote, trigVel, moNote, moChan, moLen;
};
} // namespace kf::ui
