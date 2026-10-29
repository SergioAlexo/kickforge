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
    : AudioProcessorEditor (&p), proc (p), presetBar (p),
      curveEditor(), waveform (analyser), spectrum (analyser),
      subPanel (p.apvts), clickPanel (p.apvts, p.getClickLayer(), [this] (int l) { selectCurve (4 + l); }),
      fxPanel (p.apvts), eqPanel (p.apvts, analyser), dynPanel (p.apvts), globalPanel (p.apvts)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);

    content.addAndMakeVisible (presetBar);
    content.addAndMakeVisible (fundamental);
    fundamental.setJustificationType (juce::Justification::centredRight);
    fundamental.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    fundamental.setColour (juce::Label::textColourId, col::accent2);

    content.addAndMakeVisible (audition);
    audition.setButtonText ("AUDITION");
    audition.setTooltip ("Hold to play the kick without MIDI");
    audition.onHold = [this] (bool down)
    {
        const int note = juce::jlimit (0, 127, (int) proc.apvts.getRawParameterValue ("sub_root")->load());
        if (down) proc.keyboardState.noteOn (1, note, 1.0f);
        else proc.keyboardState.noteOff (1, note, 0.0f);
    };

    static const char* curveNames[] = { "Pitch", "Amp", "Drive", "Harmonics", "Click 1 Env", "Click 2 Env", "Click 3 Env", "Click 4 Env" };
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
    content.addAndMakeVisible (waveform);
    content.addAndMakeVisible (spectrum);

    static const char* tabNames[] = { "Sub", "Click Layers", "FX", "EQ", "Dynamics", "Global / MIDI" };
    juce::Component* panels[] = { &subPanel, &clickPanel, &fxPanel, &eqPanel, &dynPanel, &globalPanel };
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

    presetBar.setBounds (190, 10, 520, 32);
    fundamental.setBounds (716, 10, 250, 32);
    audition.setBounds (976, 10, 130, 32);

    for (size_t i = 0; i < curveButtons.size(); ++i)
        curveButtons[i].setBounds (8 + (int) i * 92, 58, 88, 24);
    curveEditor.setBounds (8, 86, 736, 280);
    waveform.setBounds (752, 58, 360, 152);
    spectrum.setBounds (752, 214, 360, 152);

    for (size_t i = 0; i < tabButtons.size(); ++i)
        tabButtons[i].setBounds (8 + (int) i * 112, 372, 108, 26);
    const juce::Rectangle<int> r (8, 402, 1104, 330);
    for (auto* c : std::initializer_list<juce::Component*> { &subPanel, &clickPanel, &fxPanel, &eqPanel, &dynPanel, &globalPanel })
        c->setBounds (r);
}

void KickforgeEditor::selectTab (int i)
{
    juce::Component* panels[] = { &subPanel, &clickPanel, &fxPanel, &eqPanel, &dynPanel, &globalPanel };
    for (size_t k = 0; k < tabButtons.size(); ++k)
        panels[k]->setVisible ((int) k == i);
    tabButtons[(size_t) i].setToggleState (true, juce::dontSendNotification);
}

void KickforgeEditor::selectCurve (int index)
{
    curveIndex = juce::jlimit (0, 7, index);
    curveButtons[(size_t) curveIndex].setToggleState (true, juce::dontSendNotification);
    curveEditor.setSource (makeSource (curveIndex));
}

CurveEditor::Source KickforgeEditor::makeSource (int index)
{
    auto* ap = &proc.apvts;
    auto raw = [ap] (const char* id) { return (double) ap->getRawParameterValue (id)->load(); };
    CurveEditor::Source s;
    auto* proc_ = &proc;

    if (index < 4)
    {
        const auto id = (kf::SubCurveId) index;
        static const char* names[] = { "PITCH", "AMPLITUDE", "DRIVE", "HARMONICS" };
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
            s.hardMin = hz ? 10.0 : -60.0;
            s.hardMax = hz ? 4000.0 : 96.0;
            s.unit = hz ? "Hz" : "st";
            s.colour = col::accent;
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
                     : id == kf::SubCurveId::Drive ? juce::Colour (0xffe0507a) : juce::Colour (0xffb58cff);
            if (id == kf::SubCurveId::Amp)
                s.valueText = [] (double, double v) { return juce::String (juce::Decibels::gainToDecibels (v, -100.0), 1) + " dB"; };
        }
    }
    else
    {
        const int l = index - 4;
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
