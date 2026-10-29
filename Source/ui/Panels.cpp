#include "Panels.h"
#include "FxLabels.h"
#include "fx/FxParams.h"

namespace kf::ui
{
namespace
{
juce::String choiceText (APVTS& s, const juce::String& id)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (s.getParameter (id)))
        return p->getCurrentChoiceName();
    return {};
}
void setParamValue (APVTS& s, const juce::String& id, float plain)
{
    if (auto* p = s.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}
constexpr int kKnobW = 88, kKnobH = 94;
} // namespace

// ============================================================ Sub
SubPanel::SubPanel (APVTS& s)
    : level (s, "sub_gain", "Level"), phase (s, "sub_phase", "Start Phase"), root (s, "sub_root", "Root Note"),
      tune (s, "sub_tune", "Tune"), fine (s, "sub_fine", "Fine"), length (s, "sub_length", "Length"),
      release (s, "sub_release", "Release"), wave (s, "sub_wave"), pitchMode (s, "sub_pitchmode"), gate (s, "sub_gate"),
      keyTrack (s, "sub_keytrack", "Key track"), mute (s, "sub_mute", "Mute")
{
    for (auto* c : std::initializer_list<juce::Component*> { &level, &phase, &root, &tune, &fine, &length, &release,
                                                             &wave, &pitchMode, &gate, &keyTrack, &mute })
        addAndMakeVisible (c);
}

void SubPanel::paint (juce::Graphics& g)
{
    PanelBase::paint (g);
    heading (g, "SUB LAYER", { 12, 6, 200, 20 });
    g.setColour (col::dim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("Waveform", 12, 150, 140, 14, juce::Justification::centredLeft);
    g.drawText ("Pitch curve units", 160, 150, 140, 14, juce::Justification::centredLeft);
    g.drawText ("Gate mode", 308, 150, 140, 14, juce::Justification::centredLeft);
    g.drawText ("Edit the Pitch / Amp / Drive / Harmonic curves in the editor above.\n"
                "Double-click adds a node, drag moves it, drag a segment to bend it.",
                12, 240, 600, 40, juce::Justification::topLeft);
}

void SubPanel::resized()
{
    flowCells ({ 12, 30, 7 * kKnobW, kKnobH }, { &level, &phase, &root, &tune, &fine, &length, &release }, kKnobW, kKnobH);
    wave.setBounds (12, 168, 140, 26);
    pitchMode.setBounds (160, 168, 140, 26);
    gate.setBounds (308, 168, 140, 26);
    keyTrack.setBounds (470, 168, 100, 26);
    mute.setBounds (576, 168, 70, 26);
}

// ============================================================ Click layers
double LayerStrip::sessionRate = 48000.0;

bool LayerStrip::isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg");
}

LayerStrip::LayerStrip (APVTS& s, ClickLayer& c, int layer, std::function<void (int)> onSelectEnvelope)
    : apvts (s), click (c), index (layer),
      source (s, clickParamId (layer, CP::source)), bus (s, clickParamId (layer, CP::bus)),
      filter (s, clickParamId (layer, CP::filterType)),
      mute (s, clickParamId (layer, CP::mute), "M"), solo (s, clickParamId (layer, CP::solo), "S"),
      flip (s, clickParamId (layer, CP::flip), juce::String::fromUTF8 ("\xc3\x98")), key (s, clickParamId (layer, CP::keyTrack), "Key"),
      tone (s, clickParamId (layer, CP::tone), "Tone"), decay (s, clickParamId (layer, CP::decay), "Decay"),
      color (s, clickParamId (layer, CP::color), "Color"), start (s, clickParamId (layer, CP::start), "Start"),
      delay (s, clickParamId (layer, CP::delay), "Delay"), length (s, clickParamId (layer, CP::length), "Length"),
      pitch (s, clickParamId (layer, CP::pitch), "Pitch"), fine (s, clickParamId (layer, CP::fine), "Fine"),
      gain (s, clickParamId (layer, CP::gain), "Gain"), pan (s, clickParamId (layer, CP::pan), "Pan"),
      cutoff (s, clickParamId (layer, CP::cutoff), "Cutoff"), res (s, clickParamId (layer, CP::resonance), "Reso")
{
    for (auto* c2 : std::initializer_list<juce::Component*> { &source, &bus, &filter, &mute, &solo, &flip, &key, &envBtn, &tone,
                                                              &decay, &color, &start, &delay, &length, &pitch, &fine, &gain,
                                                              &pan, &cutoff, &res })
        addAndMakeVisible (c2);
    envBtn.onClick = [cb = std::move (onSelectEnvelope), layer] { if (cb) cb (layer); };
    envBtn.setTooltip ("Edit this layer's amplitude envelope");
    startTimerHz (4);
}

juce::String LayerStrip::statusText() const
{
    const int src = (int) apvts.getRawParameterValue (clickParamId (index, CP::source))->load();
    if (src != srcSample)
        return src == srcOff ? "Layer off" : "Generator: " + choiceText (apvts, clickParamId (index, CP::source));
    const auto name = juce::File (click.getSamplePath (index)).getFileName();
    switch (click.getSampleStatus (index))
    {
        case ClickLayer::SampleStatus::empty:   return "Drop a sample here, or click to browse";
        case ClickLayer::SampleStatus::loading: return "Loading " + name + "...";
        case ClickLayer::SampleStatus::loaded:
            return name + "  (" + juce::String ((int) (1000.0 * click.getSampleLength (index) / std::max (1.0, sessionRate))) + " ms)";
        case ClickLayer::SampleStatus::missing: return "MISSING: " + name;
        case ClickLayer::SampleStatus::failed:  return "Could not load " + name;
    }
    return {};
}

void LayerStrip::timerCallback()
{
    const auto s = statusText();
    if (s != lastStatus) { lastStatus = s; repaint(); }
}

void LayerStrip::paint (juce::Graphics& g)
{
    PanelBase::paint (g);
    heading (g, "LAYER " + juce::String (index + 1), { 8, 4, 56, 24 });
    const bool sample = (int) apvts.getRawParameterValue (clickParamId (index, CP::source))->load() == srcSample;
    auto r = dropZone.toFloat();
    g.setColour (dragOver ? col::accent.withAlpha (0.25f) : col::bg);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (dragOver ? col::accent : (sample ? col::accent.withAlpha (0.5f) : col::line));
    {
        juce::Path p;
        p.addRoundedRectangle (r.reduced (0.5f), 4.0f);
        juce::Path dashed;
        const float dash[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dash, 2);
        g.fillPath (dashed);
    }
    g.setColour (sample ? col::text : col::dim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (lastStatus.isEmpty() ? statusText() : lastStatus, dropZone.reduced (8, 0), juce::Justification::centred, true);
}

void LayerStrip::resized()
{
    int x = 62;
    envBtn.setBounds (x, 5, 40, 22); x += 44;
    for (auto* b : std::initializer_list<juce::Component*> { &mute, &solo, &flip, &key })
    {
        b->setBounds (x, 5, 34, 22);
        x += 38;
    }
    source.setBounds (6, 32, 100, 24);
    filter.setBounds (110, 32, 82, 24);
    bus.setBounds (196, 32, 68, 24);
    dropZone = { 6, 60, getWidth() - 12, 30 };
    flowCells ({ 6, 94, 4 * 66, 76 }, { &tone, &decay, &color, &start, &delay, &length, &pitch, &fine, &gain, &pan, &cutoff, &res }, 66, 76);
}

void LayerStrip::mouseUp (const juce::MouseEvent& e)
{
    if (! dropZone.contains (e.getPosition()) || e.mods.isPopupMenu() || e.getDistanceFromDragStart() > 4) return;
    chooser = std::make_unique<juce::FileChooser> ("Load sample into layer " + juce::String (index + 1), juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) loadFile (f);
                          });
}

bool LayerStrip::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isAudioFile (juce::File (f))) return true;
    return false;
}

void LayerStrip::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (isAudioFile (juce::File (f))) { loadFile (juce::File (f)); break; }
    repaint();
}

void LayerStrip::loadFile (const juce::File& f)
{
    click.loadSample (index, f);
    setParamValue (apvts, clickParamId (index, CP::source), (float) srcSample);
    lastStatus = statusText();
    repaint();
}

ClickPanel::ClickPanel (APVTS& s, ClickLayer& c, std::function<void (int)> onEnv)
{
    for (int i = 0; i < kNumClickLayers; ++i)
    {
        strips[(size_t) i] = std::make_unique<LayerStrip> (s, c, i, onEnv);
        addAndMakeVisible (*strips[(size_t) i]);
    }
}

void ClickPanel::resized()
{
    const int gap = 6, w = (getWidth() - gap * (kNumClickLayers + 1)) / kNumClickLayers;
    for (int i = 0; i < kNumClickLayers; ++i)
        strips[(size_t) i]->setBounds (gap + i * (w + gap), 0, w, getHeight());
}

// ============================================================ FX
FxSlotPanel::FxSlotPanel (APVTS& s, int sl)
    : apvts (s), slot (sl),
      type (s, juce::String ("fx_") + kFxSlotNames[sl] + "_type"),
      bypass (s, juce::String ("fx_") + kFxSlotNames[sl] + "_byp", "Bypass"),
      a (s, juce::String ("fx_") + kFxSlotNames[sl] + "_a", "A"), b (s, juce::String ("fx_") + kFxSlotNames[sl] + "_b", "B"),
      c (s, juce::String ("fx_") + kFxSlotNames[sl] + "_c", "C"), mix (s, juce::String ("fx_") + kFxSlotNames[sl] + "_mix", "Mix")
{
    for (auto* x : std::initializer_list<juce::Component*> { &type, &bypass, &a, &b, &c, &mix })
        addAndMakeVisible (x);
    refreshLabels();
    startTimerHz (8);
}

void FxSlotPanel::refreshLabels()
{
    const int t = (int) apvts.getRawParameterValue (juce::String ("fx_") + kFxSlotNames[slot] + "_type")->load();
    if (t == shownType) return;
    shownType = t;
    const auto l = fxKnobLabels (t);
    auto set = [] (Knob& k, const char* cap) { k.setCaption (cap != nullptr ? cap : "-"); k.setActive (cap != nullptr); };
    set (a, l.a); set (b, l.b); set (c, l.c); set (mix, l.mix);
}

void FxSlotPanel::paint (juce::Graphics& g)
{
    PanelBase::paint (g);
    heading (g, juce::String (slot < 2 ? "BUS A" : "BUS B") + "  " + juce::String (slot % 2 + 1), { 10, 6, 70, 24 });
}

void FxSlotPanel::resized()
{
    type.setBounds (84, 7, 150, 24);
    bypass.setBounds (242, 7, 70, 24);
    flowCells ({ 6, 40, 4 * 84, 100 }, { &a, &b, &c, &mix }, 84, 100);
}

FxPanel::MacroRow::MacroRow (APVTS& s, int i)
    : amount (s, "fx_macro" + juce::String (i) + "_amt", "Amount"), value (s, "fx_macro" + juce::String (i), "Macro " + juce::String (i)),
      target (s, "fx_macro" + juce::String (i) + "_target")
{
}

FxPanel::FxPanel (APVTS& s) : quality (s, "fx_quality")
{
    for (int i = 0; i < 4; ++i)
    {
        slots[(size_t) i] = std::make_unique<FxSlotPanel> (s, i);
        addAndMakeVisible (*slots[(size_t) i]);
        macros[(size_t) i] = std::make_unique<MacroRow> (s, i + 1);
        addAndMakeVisible (macros[(size_t) i]->amount);
        addAndMakeVisible (macros[(size_t) i]->value);
        addAndMakeVisible (macros[(size_t) i]->target);
    }
    addAndMakeVisible (quality);
}

void FxPanel::paint (juce::Graphics& g)
{
    PanelBase::paint (g);
    heading (g, "MACROS", { 732, 6, 100, 20 });
    g.setColour (col::dim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("Oversampling", 900, 6, 90, 22, juce::Justification::centredRight);
}

void FxPanel::resized()
{
    for (int i = 0; i < 4; ++i)
        slots[(size_t) i]->setBounds (6 + (i % 2) * 352, 6 + (i / 2) * 168, 346, 162);
    quality.setBounds (996, 6, 96, 22);
    for (int i = 0; i < 4; ++i)
    {
        const int y = 34 + i * 75;
        macros[(size_t) i]->value.setBounds (732, y, 74, 74);
        macros[(size_t) i]->target.setBounds (814, y + 28, 170, 24);
        macros[(size_t) i]->amount.setBounds (992, y, 74, 74);
    }
}

// ============================================================ EQ
EqPanel::EqPanel (APVTS& s, Analyser& an)
    : spectrum (an, &s), bypass (s, "fx_eq_byp", "EQ bypass"),
      lc (s, "fx_eq_lc", "Low Cut"), lsF (s, "fx_eq_ls_f", "LowShelf Hz"), lsG (s, "fx_eq_ls_g", "LowShelf dB"),
      p1F (s, "fx_eq_p1_f", "Peak 1 Hz"), p1G (s, "fx_eq_p1_g", "Peak 1 dB"), p1Q (s, "fx_eq_p1_q", "Peak 1 Q"),
      p2F (s, "fx_eq_p2_f", "Peak 2 Hz"), p2G (s, "fx_eq_p2_g", "Peak 2 dB"), p2Q (s, "fx_eq_p2_q", "Peak 2 Q"),
      hsF (s, "fx_eq_hs_f", "HiShelf Hz"), hsG (s, "fx_eq_hs_g", "HiShelf dB")
{
    for (auto* c : std::initializer_list<juce::Component*> { &spectrum, &bypass, &lc, &lsF, &lsG, &p1F, &p1G, &p1Q, &p2F, &p2G, &p2Q, &hsF, &hsG })
        addAndMakeVisible (c);
}

void EqPanel::resized()
{
    spectrum.setBounds (6, 6, 600, getHeight() - 12);
    flowCells ({ 620, 6, 4 * 118, 4 * 80 }, { nullptr, &lc, &lsF, &lsG, &p1F, &p1G, &p1Q, nullptr, &p2F, &p2G, &p2Q, nullptr, &hsF, &hsG }, 118, 80);
    bypass.setBounds (628, 26, 100, 26);
}

// ============================================================ Dynamics
DynPanel::DynPanel (APVTS& s)
    : compByp (s, "fx_comp_byp", "Bypass"), limByp (s, "fx_lim_byp", "Bypass"),
      thr (s, "fx_comp_thr", "Threshold"), ratio (s, "fx_comp_ratio", "Ratio"), att (s, "fx_comp_att", "Attack"),
      rel (s, "fx_comp_rel", "Release"), makeup (s, "fx_comp_makeup", "Makeup"), cmix (s, "fx_comp_mix", "Mix"),
      lin (s, "fx_lim_in", "Input"), ceil (s, "fx_lim_ceil", "Ceiling"), sat (s, "fx_lim_sat", "Saturation"),
      out (s, "fx_out", "FX Output"), limRel (s, "fx_lim_rel"), limLook (s, "fx_lim_look")
{
    for (auto* c : std::initializer_list<juce::Component*> { &compByp, &limByp, &thr, &ratio, &att, &rel, &makeup, &cmix, &lin, &ceil, &sat, &out, &limRel, &limLook })
        addAndMakeVisible (c);
}

void DynPanel::paint (juce::Graphics& g)
{
    PanelBase::paint (g);
    heading (g, "COMPRESSOR", { 12, 6, 200, 20 });
    heading (g, "LIMITER", { 12, 164, 200, 20 });
    g.setColour (col::dim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("Release", 408, 196, 120, 14, juce::Justification::centredLeft);
    g.drawText ("Lookahead", 538, 196, 120, 14, juce::Justification::centredLeft);
}

void DynPanel::resized()
{
    compByp.setBounds (12, 40, 90, 26);
    flowCells ({ 112, 28, 6 * kKnobW, kKnobH }, { &thr, &ratio, &att, &rel, &makeup, &cmix }, kKnobW, kKnobH);
    limByp.setBounds (12, 200, 90, 26);
    flowCells ({ 112, 188, 3 * kKnobW, kKnobH }, { &lin, &ceil, &sat }, kKnobW, kKnobH);
    limRel.setBounds (408, 214, 120, 26);
    limLook.setBounds (538, 214, 120, 26);
    out.setBounds (700, 188, kKnobW, kKnobH);
}

// ============================================================ Global
GlobalPanel::GlobalPanel (APVTS& s)
    : master (s, "glob_gain", "Master"), clickMaster (s, "click_gain", "Click Master"), a4 (s, "glob_a4", "A4 Tuning"),
      bend (s, "glob_bend", "Bend Range"), glide (s, "glob_glide", "Glide"), vel (s, "glob_velsens", "Velocity"),
      legato (s, "glob_legato", "Legato")
{
    for (auto* c : std::initializer_list<juce::Component*> { &master, &clickMaster, &a4, &bend, &glide, &vel, &legato })
        addAndMakeVisible (c);
}

void GlobalPanel::paint (juce::Graphics& g)
{
    PanelBase::paint (g);
    heading (g, "GLOBAL / MIDI", { 12, 6, 200, 20 });
    g.setColour (col::dim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("Any MIDI note triggers the kick; with Key track on, the note transposes the pitch curve.\n"
                "The pitch wheel bends the sub by the bend range. Glide + Legato slide between overlapping notes.\n"
                "Velocity sets how strongly note velocity scales the level (0 = fixed).",
                12, 190, 800, 60, juce::Justification::topLeft);
}

void GlobalPanel::resized()
{
    flowCells ({ 12, 30, 6 * kKnobW, kKnobH }, { &master, &clickMaster, &a4, &bend, &glide, &vel }, kKnobW, kKnobH);
    legato.setBounds (12 + 6 * kKnobW + 20, 58, 90, 26);
}
} // namespace kf::ui
