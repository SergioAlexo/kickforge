#include "PluginEditor.h"
#include "ui/Readout.h"

using namespace kf::ui;

void KickforgeEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::panel);
    g.fillRect (0, 0, getWidth(), 52);
    g.setColour (col::line);
    g.drawHorizontalLine (52, 0.0f, (float) getWidth());

    // wordmark: an ember wedge + KICKFORGE
    juce::Path wedge;
    wedge.addTriangle (16.0f, 38.0f, 30.0f, 12.0f, 44.0f, 38.0f);
    g.setColour (col::accent);
    g.fillPath (wedge);
    g.setColour (col::bg);
    g.fillEllipse (25.0f, 26.0f, 10.0f, 10.0f);
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    g.setColour (col::text);
    g.drawText ("KICK", 54, 8, 70, 36, juce::Justification::centredLeft);
    g.setColour (col::accent);
    g.drawText ("FORGE", 54 + 62, 8, 90, 36, juce::Justification::centredLeft);
}

KickforgeEditor::KickforgeEditor (KickforgeProcessor& p)
    : AudioProcessorEditor (&p), proc (p), presetBar (p), liveToggle (p), refStrip (p),
      waveService (p), curveEditor(), waveform (analyser), spectrum (analyser),
      subPanel (p.apvts), harmPanel (p.apvts), clickPanel (p.apvts, p.getClickLayer(), [this] (int l) { selectCurve (5 + l); }),
      fxPanel (p.apvts), eqPanel (p.apvts, analyser), dynPanel (p.apvts), globalPanel (p.apvts)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    content.addAndMakeVisible (presetBar);
    content.addAndMakeVisible (fundamental);
    fundamental.setJustificationType (juce::Justification::centredRight);
    fundamental.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    fundamental.setColour (juce::Label::textColourId, col::accent2);

    content.addAndMakeVisible (liveToggle);
    content.addAndMakeVisible (refStrip);
    waveform.setReference (&p.getReference());

    content.addAndMakeVisible (audition);
    audition.setButtonText ("AUDITION");
    audition.setTooltip ("Hold to play the kick without MIDI");
    audition.onHold = [this] (bool down)
    {
        const int note = juce::jlimit (0, 127, (int) proc.apvts.getRawParameterValue ("sub_root")->load());
        if (down) proc.keyboardState.noteOn (1, note, 1.0f);
        else proc.keyboardState.noteOff (1, note, 0.0f);
    };

    static const char* curveNames[] = { "Pitch", "Amp", "Drive", "Harmonics", "Key Track", "Click 1", "Click 2", "Click 3", "Click 4" };
    for (size_t i = 0; i < curveButtons.size(); ++i)
    {
        auto& b = curveButtons[i];
        b.setButtonText (curveNames[i]);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (101);
        b.onClick = [this, i] { selectCurve ((int) i); };
        content.addAndMakeVisible (b);
    }
    content.addAndMakeVisible (curveEditor);
    {
        auto& ws = proc.getWaveSettings();
        curveEditor.setLayerToggle (0, ws.sub);
        curveEditor.setLayerToggle (1, ws.clicks);
        curveEditor.setLayerToggle (2, ws.reference);
        curveEditor.setLayerToggle (3, ws.mix);
        curveEditor.getLayerButton (0).setColour (juce::TextButton::buttonOnColourId, kf::ui::wavecol::sub.darker (0.5f));
        curveEditor.getLayerButton (1).setColour (juce::TextButton::buttonOnColourId, kf::ui::wavecol::click (0).darker (0.6f));
        curveEditor.getLayerButton (2).setColour (juce::TextButton::buttonOnColourId, kf::ui::wavecol::ref.darker (0.7f));
        curveEditor.getLayerButton (3).setColour (juce::TextButton::buttonOnColourId, kf::ui::wavecol::mix.darker (0.6f));
        curveEditor.onLayerToggle = [this] (int i, bool on)
        {
            auto& w = proc.getWaveSettings();
            (i == 0 ? w.sub : i == 1 ? w.clicks : i == 2 ? w.reference : w.mix) = on;
        };
        curveEditor.paintBackdrop = [this] (juce::Graphics& g, const CurveView& v, juce::Rectangle<int> plot, double lenMs)
        {
            auto& w = proc.getWaveSettings();
            kf::ui::WaveDraw d;
            d.sub = w.sub; d.clicks = w.clicks; d.reference = w.reference; d.mix = w.mix;
            d.activeClick = curveIndex >= 5 ? curveIndex - 5 : -1;
            auto res = waveService.latest();
            kf::ui::paintWaveLayers (g, res.get(), refTracker.get (proc.getReference()), d, v, plot, lenMs);
        };
        waveService.onResult = [this] { curveEditor.repaint(); };
    }
    content.addAndMakeVisible (waveform);
    content.addAndMakeVisible (spectrum);

    static const char* tabNames[] = { "Sub", "Harmonics", "Click Layers", "FX", "EQ", "Dynamics", "Global / MIDI" };
    juce::Component* panels[] = { &subPanel, &harmPanel, &clickPanel, &fxPanel, &eqPanel, &dynPanel, &globalPanel };
    for (size_t i = 0; i < tabButtons.size(); ++i)
    {
        auto& b = tabButtons[i];
        b.setButtonText (tabNames[i]);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (102);
        b.onClick = [this, i] { selectTab ((int) i); };
        content.addAndMakeVisible (b);
        content.addChildComponent (*panels[i]);
    }

    lastHzMode = proc.apvts.getRawParameterValue ("sub_pitchmode")->load() > 0.5f;
    selectCurve (0);
    selectTab (0);

    setResizable (true, true);
    setResizeLimits (designW / 2, designH / 2, designW * 2, designH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) designW / designH);
    setSize (designW, designH);
    startTimerHz (30);
}

KickforgeEditor::~KickforgeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void KickforgeEditor::resized()
{
    const float s = (float) getWidth() / (float) designW;
    content.setTransform (juce::AffineTransform::scale (s));
    content.setBounds (0, 0, designW, designH);

    presetBar.setBounds (190, 10, 470, 32);
    fundamental.setBounds (664, 10, 200, 32);
    liveToggle.setBounds (870, 10, 130, 32);
    audition.setBounds (1006, 10, 100, 32);

    for (size_t i = 0; i < curveButtons.size(); ++i)
        curveButtons[i].setBounds (8 + (int) i * 82, 58, 78, 24);
    curveEditor.setBounds (8, 86, 736, 280);
    waveform.setBounds (752, 58, 360, 152);
    spectrum.setBounds (752, 214, 360, 152);

    for (size_t i = 0; i < tabButtons.size(); ++i)
        tabButtons[i].setBounds (8 + (int) i * 112, 372, 108, 26);
    refStrip.setBounds (8, 738, 1104, 38);
    const juce::Rectangle<int> r (8, 402, 1104, 330);
    for (auto* c : std::initializer_list<juce::Component*> { &subPanel, &harmPanel, &clickPanel, &fxPanel, &eqPanel, &dynPanel, &globalPanel })
        c->setBounds (r);
}

void KickforgeEditor::selectTab (int i)
{
    juce::Component* panels[] = { &subPanel, &harmPanel, &clickPanel, &fxPanel, &eqPanel, &dynPanel, &globalPanel };
    for (size_t k = 0; k < tabButtons.size(); ++k)
        panels[k]->setVisible ((int) k == i);
    tabButtons[(size_t) i].setToggleState (true, juce::dontSendNotification);
}

void KickforgeEditor::selectCurve (int index)
{
    curveIndex = juce::jlimit (0, 8, index);
    if (curveIndex == 3) selectTab (1);   // Harmonics curve -> harmonics panel
    else if (curveIndex >= 5 && curveIndex <= 8) selectTab (2);
    else if (curveIndex < 5 && tabButtons[1].getToggleState()) selectTab (0);
    curveButtons[(size_t) curveIndex].setToggleState (true, juce::dontSendNotification);
    curveEditor.setSource (makeSource (curveIndex));
}

CurveEditor::Source KickforgeEditor::makeSource (int index)
{
    auto* ap = &proc.apvts;
    auto raw = [ap] (const char* id) { return (double) ap->getRawParameterValue (id)->load(); };
    CurveEditor::Source s;
    auto* proc_ = &proc;

    if (index < 5)
    {
        const auto id = (kf::SubCurveId) index;
        static const char* names[] = { "PITCH", "AMPLITUDE", "DRIVE", "HARMONICS", "KEY TRACKING" };
        s.name = names[index];
        s.get = [proc_, id] { return proc_->getSubCurves().getCurve (id); };
        s.set = [proc_, id] (const kf::Curve& c) { proc_->getSubCurves().setCurve (id, c); };
        s.resetToDefault = [proc_, id] { proc_->getSubCurves().resetToDefault (id); };
        s.lengthMs = [raw] { return raw ("sub_length"); };
        if (id == kf::SubCurveId::Pitch)
        {
            const bool hz = raw ("sub_pitchmode") > 0.5;
            s.vmin = hz ? 0.0 : -12.0;
            s.vmax = hz ? 400.0 : 48.0;
            s.fitToCurve = true;
            s.hardMin = hz ? 10.0 : -60.0;
            s.hardMax = hz ? 4000.0 : 96.0;
            s.unit = hz ? "Hz" : "st";
            s.colour = col::accent;
            s.onLockHere = [ap] (double t)
            {
                auto setP = [ap] (const char* id, float plain)
                {
                    if (auto* p = ap->getParameter (id))
                    {
                        p->beginChangeGesture();
                        p->setValueNotifyingHost (p->convertTo0to1 (plain));
                        p->endChangeGesture();
                    }
                };
                setP ("sub_lockpos", (float) juce::jlimit (0.0, 1.0, t));
                setP ("sub_lock", 1.0f);
            };
            s.snapValue = [raw] (double v)
            {
                return snapPitchValue (v, raw ("sub_pitchmode") > 0.5, raw ("sub_root"), raw ("sub_tune"), raw ("sub_fine"), raw ("glob_a4"));
            };
            s.valueText = [raw] (double, double v)
            {
                return formatPitch (curveValueToHz (v, raw ("sub_pitchmode") > 0.5, raw ("sub_root"), raw ("sub_tune"),
                                                    raw ("sub_fine"), raw ("glob_a4")), raw ("glob_a4"));
            };
        }
        else
        {
            s.vmin = 0.0; s.vmax = 1.0; s.hardMin = 0.0; s.hardMax = 1.0;
            s.colour = id == kf::SubCurveId::Amp ? col::accent2
                     : id == kf::SubCurveId::Drive ? juce::Colour (0xffe0507a)
                     : id == kf::SubCurveId::KeyTrack ? juce::Colour (0xff7ac0ff) : juce::Colour (0xffb58cff);
            if (id == kf::SubCurveId::Amp)
                s.valueText = [] (double, double v) { return juce::String (juce::Decibels::gainToDecibels (v, -100.0), 1) + " dB"; };
        }
    }
    else
    {
        const int l = index - 5;
        s.name = "CLICK " + juce::String (l + 1) + " ENVELOPE";
        s.get = [proc_, l] { return proc_->getClickLayer().getEnvelope (l); };
        s.set = [proc_, l] (const kf::Curve& c) { proc_->getClickLayer().setEnvelope (l, c); };
        s.resetToDefault = [proc_, l] { proc_->getClickLayer().setEnvelope (l, kf::Curve ({ { 0.0, 1.0, 0.0 }, { 1.0, 1.0, 0.0 } })); };
        const auto lenId = kf::clickParamId (l, kf::CP::length);
        s.lengthMs = [ap, lenId] { return (double) ap->getRawParameterValue (lenId)->load(); };
        s.vmin = 0.0; s.vmax = 1.0; s.hardMin = 0.0; s.hardMax = 1.0;
        s.colour = juce::Colour (0xffffd166);
    }
    return s;
}

juce::String KickforgeEditor::fundamentalText() const
{
    const auto c = proc.getSubCurves().getCurve (kf::SubCurveId::Pitch);
    if (c.empty()) return {};
    auto raw = [this] (const char* id) { return (double) proc.apvts.getRawParameterValue (id)->load(); };
    const double hz = curveValueToHz (c.getPoints().back().value, raw ("sub_pitchmode") > 0.5, raw ("sub_root"),
                                      raw ("sub_tune"), raw ("sub_fine"), raw ("glob_a4"));
    return "SUB  " + formatPitch (hz, raw ("glob_a4"));
}

void KickforgeEditor::timerCallback()
{
    const double sr = proc.getSampleRate();
    if (sr > 0 && sr != lastRate)
    {
        lastRate = sr;
        analyser.setSampleRate (sr);
        LayerStrip::sessionRate = sr;
    }
    analyser.pump (proc.scopeFifo);
    if (analyser.getKickVersion() != lastKickVersion)
    {
        lastKickVersion = analyser.getKickVersion();
        waveform.repaint();
    }
    spectrum.repaint();
    curveEditor.setLayerToggleEnabled (2, proc.getReference().hasAudio());
    if (++liveTick % 8 == 0)   // ~4 Hz: link status dot / tooltip, reference overlay
    {
        liveToggle.refresh();
        waveform.repaint();
    }
    if (eqPanel.isShowing()) eqPanel.repaint();

    const auto t = fundamentalText();
    if (t != fundamental.getText()) fundamental.setText (t, juce::dontSendNotification);

    const bool hz = proc.apvts.getRawParameterValue ("sub_pitchmode")->load() > 0.5f;
    if (hz != lastHzMode)
    {
        lastHzMode = hz;
        if (curveIndex == 0) selectCurve (0);
    }
}
