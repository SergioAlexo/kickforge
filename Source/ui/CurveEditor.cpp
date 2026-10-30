#include "CurveEditor.h"

namespace kf::ui
{
namespace
{
constexpr double kMinSpan = 0.01;
}

CurveEditor::CurveEditor()
{
    setWantsKeyboardFocus (true);
    for (auto* b : std::initializer_list<juce::Button*> { &timeSnapBtn, &valSnapBtn, &logBtn, &fitBtn, &undoBtn, &redoBtn })
        addAndMakeVisible (b);
    {
        static const char* names[] = { "Sub", "Clicks", "Ref", "Mix" };
        static const char* tips[] = { "Show the sub waveform behind the curve", "Show the click layers behind the curve",
                                      "Show the reference kick behind the curve (needs a loaded reference)", "Show the full post-FX mix outline" };
        for (size_t i = 0; i < layerBtns.size(); ++i)
        {
            auto& b = layerBtns[i];
            b.setButtonText (names[i]);
            b.setTooltip (tips[i]);
            b.setClickingTogglesState (true);
            b.onClick = [this, i] { if (onLayerToggle) onLayerToggle ((int) i, layerBtns[i].getToggleState()); repaint(); };
            addAndMakeVisible (b);
        }
    }
    timeSnapBtn.setToggleState (true, juce::dontSendNotification);
    valSnapBtn.setToggleState (true, juce::dontSendNotification);
    logBtn.onClick = [this] { view.logTime = logBtn.getToggleState(); repaint(); };
    fitBtn.onClick = [this] { fitView(); };
    undoBtn.onClick = [this] { undo(); };
    redoBtn.onClick = [this] { redo(); };
    timeSnapBtn.onClick = valSnapBtn.onClick = [this] { repaint(); };
    startTimerHz (15);
}

void CurveEditor::setSource (Source s)
{
    src = std::move (s);
    curve = src.get ? src.get() : Curve();
    undoStack.clear();
    redoStack.clear();
    selected = hoverPoint = hoverSeg = -1;
    drag = Drag::none;
    valSnapBtn.setButtonText (src.snapValue ? "Note snap" : "Value snap");
    fitView();
}

void CurveEditor::fitView()
{
    viewTouched = false;
    view.t0 = 0.0;
    view.t1 = defaultSpanT();
    double lo = src.vmin, hi = src.vmax;
    if (src.fitToCurve && ! curve.empty())
    {
        double clo = 1e300, chi = -1e300;
        for (auto& p : curve.getPoints()) { clo = std::min (clo, p.value); chi = std::max (chi, p.value); }
        const double defSpan = std::max (1e-6, src.vmax - src.vmin);
        if (chi - clo < defSpan * 0.05) // (nearly) flat: keep the default framing but include the curve
        {
            lo = std::min (lo, clo); hi = std::max (hi, chi);
        }
        else
        {
            lo = clo; hi = chi;
        }
    }
    else
        for (auto& p : curve.getPoints()) { lo = std::min (lo, p.value); hi = std::max (hi, p.value); }
    const double pad = (hi - lo) * (src.fitToCurve ? 0.12 : 0.06);
    view.vBottom = lo - pad;
    view.vTop = hi + pad;
    repaint();
}

double CurveEditor::defaultSpanT() const
{
    if (! src.viewMs) return 1.0;
    return std::clamp (src.viewMs() / getLengthMs(), 0.02, 50.0);
}

void CurveEditor::refreshDefaultView()
{
    if (viewTouched || drag != Drag::none) return;
    view.t0 = 0.0;
    view.t1 = defaultSpanT();
    repaint();
}

void CurveEditor::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (26).reduced (0, 2);
    timeSnapBtn.setBounds (bar.removeFromLeft (84)); bar.removeFromLeft (4);
    valSnapBtn.setBounds (bar.removeFromLeft (84)); bar.removeFromLeft (4);
    logBtn.setBounds (bar.removeFromLeft (76)); bar.removeFromLeft (4);
    fitBtn.setBounds (bar.removeFromLeft (40)); bar.removeFromLeft (12);
    for (auto& b : layerBtns) { b.setBounds (bar.removeFromLeft (b.getButtonText().length() > 3 ? 48 : 40)); bar.removeFromLeft (3); }
    redoBtn.setBounds (bar.removeFromRight (50)); bar.removeFromRight (4);
    undoBtn.setBounds (bar.removeFromRight (50));
    plot = r.withTrimmedLeft (46).withTrimmedBottom (18).withTrimmedRight (8).withTrimmedTop (6);
    updateViewRect();
}

void CurveEditor::updateViewRect()
{
    view.x = (float) plot.getX();
    view.y = (float) plot.getY();
    view.w = (float) plot.getWidth();
    view.h = (float) plot.getHeight();
}

void CurveEditor::timerCallback()
{
    undoBtn.setEnabled (! undoStack.empty());
    redoBtn.setEnabled (! redoStack.empty());
    if (drag != Drag::none || ! src.get) return;
    auto c = src.get();
    if (! (c == curve)) { curve = std::move (c); selected = -1; if (src.fitToCurve) fitView(); repaint(); }
}

void CurveEditor::commit()
{
    if (src.set) src.set (curve);
    repaint();
}

void CurveEditor::pushUndo()
{
    undoStack.push_back (curve);
    if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void CurveEditor::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back (curve);
    curve = undoStack.back();
    undoStack.pop_back();
    selected = -1;
    commit();
}

void CurveEditor::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (curve);
    curve = redoStack.back();
    redoStack.pop_back();
    selected = -1;
    commit();
}

double CurveEditor::timeStep() const
{
    const double span = view.t1 - view.t0;
    if (src.lengthMs)
    {
        const double L = std::max (1.0, src.lengthMs());
        return niceStep (span * L, 8) / L;
    }
    return niceStep (span, 8);
}

double CurveEditor::valueStep() const { return niceStep (view.vTop - view.vBottom, 6); }

double CurveEditor::snapT (double t, bool force) const
{
    t = std::clamp (t, 0.0, 1.0);
    return (force || timeSnapBtn.getToggleState()) ? snapToStep (t, timeStep()) : t;
}

double CurveEditor::snapV (double v) const
{
    if (valSnapBtn.getToggleState())
        v = src.snapValue ? src.snapValue (v) : snapToStep (v, valueStep());
    return std::clamp (v, src.hardMin, src.hardMax);
}

juce::String CurveEditor::timeText (double t) const
{
    if (src.lengthMs)
    {
        const double ms = t * src.lengthMs();
        return ms >= 1000.0 ? juce::String (ms / 1000.0, 2) + " s" : juce::String (ms, ms < 10 ? 1 : 0) + " ms";
    }
    return juce::String (t, 3);
}

juce::String CurveEditor::readoutText (double t, double v) const
{
    auto s = timeText (t) + "   " + juce::String (v, 2) + (src.unit.isEmpty() ? "" : " " + src.unit);
    if (src.valueText) s << "   " << src.valueText (t, v);
    return s;
}

std::vector<CurveEditor::AxisLabel> CurveEditor::timeLabels() const
{
    std::vector<AxisLabel> out;
    constexpr int labelW = 52, labelH = 14;
    const double step = timeStep();
    CurveView v = view; // paint may not have run yet: use the plot rect directly
    v.x = (float) plot.getX(); v.w = (float) plot.getWidth();
    int lastRight = -1000;
    for (double t = std::ceil (v.t0 / step) * step; t <= v.t1 + 1e-9; t += step)
    {
        const float x = v.timeToX (t);
        if (x < (float) plot.getX() - 0.5f || x > (float) plot.getRight() + 0.5f) continue;
        // centred on the grid line, but inset so edge labels never leave the component
        const int left = juce::jlimit (0, juce::jmax (0, getWidth() - labelW), (int) std::lround (x) - labelW / 2);
        const juce::Rectangle<int> r (left, plot.getBottom() + 2, juce::jmin (labelW, getWidth()), labelH);
        if (x >= (float) plot.getRight() - 1.0f) // the end-of-axis label wins over its neighbours
            while (! out.empty() && r.getX() < out.back().rect.getRight() + 2) out.pop_back();
        else if (r.getX() < lastRight + 2) continue;
        lastRight = r.getRight();
        out.push_back ({ r, timeText (t), t });
    }
    return out;
}

std::vector<CurveEditor::AxisLabel> CurveEditor::valueLabels() const
{
    std::vector<AxisLabel> out;
    const double step = valueStep();
    CurveView v = view;
    v.y = (float) plot.getY(); v.h = (float) plot.getHeight();
    for (double val = std::ceil (v.vBottom / step) * step; val <= v.vTop + 1e-9; val += step)
    {
        const float y = v.valueToY (val);
        if (y < (float) plot.getY() || y > (float) plot.getBottom()) continue;
        const int top = juce::jlimit (0, juce::jmax (0, getHeight() - 14), (int) y - 7);
        out.push_back ({ { 0, top, juce::jmax (1, plot.getX() - 4), 14 },
                         juce::String (val, step < 1.0 ? 2 : (step < 10 ? 1 : 0)), val });
    }
    return out;
}

// ------------------------------------------------------------------ painting
void CurveEditor::paint (juce::Graphics& g)
{
    using namespace col;
    updateViewRect();
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    const auto pf = plot.toFloat();
    g.setColour (bg);
    g.fillRect (pf);
    if (paintBackdrop) paintBackdrop (g, view, plot, getLengthMs());

    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    // time grid
    {
        const double step = timeStep();
        float lastX = -100.0f;
        for (double t = std::ceil (view.t0 / step) * step; t <= view.t1 + 1e-9; t += step)
        {
            const float x = view.timeToX (t);
            if (x < pf.getX() - 0.5f || x > pf.getRight() + 0.5f || x < lastX + 4.0f) continue;
            lastX = x;
            g.setColour (line.withAlpha (0.55f));
            g.drawVerticalLine ((int) x, pf.getY(), pf.getBottom());
        }
        g.setColour (dim);
        for (auto& l : timeLabels())
            g.drawText (l.text, l.rect, juce::Justification::centred);
    }
    // value grid
    {
        const double step = valueStep();
        for (double v = std::ceil (view.vBottom / step) * step; v <= view.vTop + 1e-9; v += step)
        {
            const float y = view.valueToY (v);
            if (y < pf.getY() || y > pf.getBottom()) continue;
            g.setColour (std::abs (v) < 1e-9 ? line.brighter (0.4f) : line.withAlpha (0.55f));
            g.drawHorizontalLine ((int) y, pf.getX(), pf.getRight());
        }
        g.setColour (dim);
        for (auto& l : valueLabels())
            g.drawText (l.text, l.rect, juce::Justification::centredRight);
    }
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (plot);

    if (! curve.empty())
    {
        juce::Path stroke, fill;
        bool first = true;
        for (int px = plot.getX(); px <= plot.getRight(); px += 2)
        {
            const float y = view.valueToY (curve.eval (view.xToTime ((float) px)));
            if (first) { stroke.startNewSubPath ((float) px, y); fill.startNewSubPath ((float) px, pf.getBottom()); fill.lineTo ((float) px, y); first = false; }
            else { stroke.lineTo ((float) px, y); fill.lineTo ((float) px, y); }
        }
        fill.lineTo (pf.getRight(), pf.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (src.colour.withAlpha (0.28f), 0, pf.getY(), src.colour.withAlpha (0.0f), 0, pf.getBottom(), false));
        g.fillPath (fill);
        g.setColour (src.colour);
        g.strokePath (stroke, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const auto& pts = curve.getPoints();
        for (int i = 0; i + 1 < curve.size(); ++i) // tension handle diamonds at segment middles
        {
            const double tm = 0.5 * (pts[(size_t) i].time + pts[(size_t) i + 1].time);
            const float hx = view.timeToX (tm), hy = view.valueToY (curve.eval (tm));
            juce::Path d;
            d.addRectangle (-3.0f, -3.0f, 6.0f, 6.0f);
            g.setColour (i == hoverSeg || (drag == Drag::segment && i == dragSeg) ? text : src.colour.withAlpha (0.55f));
            g.fillPath (d, juce::AffineTransform::rotation (0.785398f).translated (hx, hy));
        }
        for (int i = 0; i < curve.size(); ++i)
        {
            const float x = view.timeToX (pts[(size_t) i].time), y = view.valueToY (pts[(size_t) i].value);
            const bool hot = i == selected || i == hoverPoint;
            g.setColour (bg);
            g.fillEllipse (x - 6, y - 6, 12, 12);
            g.setColour (hot ? text : src.colour);
            g.drawEllipse (x - 5, y - 5, 10, 10, 2.0f);
            if (i == selected) { g.setColour (src.colour); g.fillEllipse (x - 3, y - 3, 6, 6); }
        }
    }

    // readout: selected / hovered point, else cursor position
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    juce::String txt;
    const int ri = drag == Drag::point ? selected : hoverPoint;
    if (ri >= 0 && ri < curve.size())
        txt = readoutText (curve.getPoints()[(size_t) ri].time, curve.getPoints()[(size_t) ri].value);
    else if (mouseInside && plot.toFloat().contains (mousePos))
        txt = readoutText (view.xToTime (mousePos.x), view.yToValue (mousePos.y));
    if (txt.isNotEmpty())
    {
        const int w = g.getCurrentFont().getStringWidth (txt) + 14;
        const auto r = juce::Rectangle<int> (plot.getX() + 6, plot.getY() + 6, w, 20);
        g.setColour (panel.withAlpha (0.9f));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (text);
        g.drawText (txt, r, juce::Justification::centred);
    }
    g.setColour (dim);
    g.drawText (src.name, plot.withTrimmedRight (6).removeFromTop (22), juce::Justification::centredRight);
}

// ------------------------------------------------------------------ mouse
void CurveEditor::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    mouseInside = true;
    hoverPoint = hitPoint (curve, view, e.position.x, e.position.y);
    hoverSeg = hoverPoint < 0 ? hitSegment (curve, view, e.position.x, e.position.y) : -1;
    repaint();
}

void CurveEditor::mouseExit (const juce::MouseEvent&)
{
    mouseInside = false;
    hoverPoint = hoverSeg = -1;
    repaint();
}

void CurveEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto p = e.position;
    const int pi = hitPoint (curve, view, p.x, p.y);
    const int si = pi < 0 ? hitSegment (curve, view, p.x, p.y) : -1;

    if (e.mods.isPopupMenu())
    {
        if (pi >= 0) selected = pi;
        showMenu (pi, si, p);
        return;
    }
    dragMoved = false;
    beforeDrag = curve;
    if (pi >= 0)
    {
        selected = pi;
        drag = Drag::point;
    }
    else if (si >= 0)
    {
        drag = Drag::segment;
        dragSeg = si;
        const auto& a = curve.getPoints()[(size_t) si];
        const auto& b = curve.getPoints()[(size_t) si + 1];
        dragU = std::clamp ((view.xToTime (p.x) - a.time) / std::max (1e-9, b.time - a.time), 0.15, 0.85);
        selected = -1;
    }
    else
    {
        selected = -1;
        drag = Drag::pan;
        viewTouched = true;
        panT0 = view.t0;
        panT1 = view.t1;
    }
    repaint();
}

void CurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    mousePos = e.position;
    if (drag == Drag::none) return;
    if (drag == Drag::pan)
    {
        const double span = panT1 - panT0;
        double dt = -(double) e.getDistanceFromDragStartX() / view.w * span;
        dt = std::clamp (dt, -panT0, 1.0 - panT1);
        view.t0 = panT0 + dt;
        view.t1 = panT1 + dt;
        repaint();
        return;
    }
    if (! dragMoved)
    {
        if (e.getDistanceFromDragStart() < 3) return;
        dragMoved = true;
        pushUndo();
    }
    if (drag == Drag::point && selected >= 0)
    {
        const double t = clampPointTime (curve, selected, snapT (view.xToTime (e.position.x)));
        const double v = snapV (view.yToValue (e.position.y));
        selected = curve.movePoint (selected, t, v);
        commit();
    }
    else if (drag == Drag::segment && dragSeg >= 0 && dragSeg + 1 < curve.size())
    {
        const auto& a = curve.getPoints()[(size_t) dragSeg];
        const auto& b = curve.getPoints()[(size_t) dragSeg + 1];
        if (std::abs (b.value - a.value) > 1e-9)
        {
            const double s = (view.yToValue (e.position.y) - a.value) / (b.value - a.value);
            curve.setTension (dragSeg, tensionForTarget (dragU, s));
            commit();
        }
    }
}

void CurveEditor::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::none;
    repaint();
}

void CurveEditor::addPointAt (float px, float py)
{
    pushUndo();
    const double t = snapT (view.xToTime (px));
    const double v = snapV (view.yToValue (py));
    selected = curve.addPoint (t, v, 0.0);
    commit();
}

bool CurveEditor::deletePoint (int idx)
{
    if (curve.size() <= 2 || idx <= 0 || idx >= curve.size() - 1) return false;
    pushUndo();
    curve.removePoint (idx);
    selected = hoverPoint = -1;
    commit();
    return true;
}

void CurveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    drag = Drag::none;
    const int pi = hitPoint (curve, view, e.position.x, e.position.y);
    if (pi >= 0) deletePoint (pi);
    else if (plot.toFloat().contains (e.position)) addPointAt (e.position.x, e.position.y);
}

void CurveEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    viewTouched = true;
    const double tl = tLimit();
    const double span = view.t1 - view.t0;
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const double d = (e.mods.isShiftDown() ? w.deltaY : -w.deltaX) * span * 0.5;
        const double dt = std::clamp (-d, -view.t0, tl - view.t1);
        view.t0 += dt;
        view.t1 += dt;
    }
    else if (e.mods.isCtrlDown())
    {
        const double f = std::exp (-w.deltaY * 0.6), vc = view.yToValue (e.position.y);
        view.vBottom = vc - (vc - view.vBottom) * f;
        view.vTop = vc + (view.vTop - vc) * f;
    }
    else
    {
        const double f = std::exp (-w.deltaY * 0.6), tc = std::clamp (view.xToTime (e.position.x), 0.0, tl);
        double n0 = tc - (tc - view.t0) * f, n1 = tc + (view.t1 - tc) * f;
        if (n1 - n0 < kMinSpan) { n0 = tc - kMinSpan / 2; n1 = tc + kMinSpan / 2; }
        if (n1 - n0 > tl) { n0 = 0; n1 = tl; }
        if (n0 < 0) { n1 -= n0; n0 = 0; }
        if (n1 > tl) { n0 -= n1 - tl; n1 = tl; }
        view.t0 = std::max (0.0, n0);
        view.t1 = std::min (tl, n1);
    }
    repaint();
}

bool CurveEditor::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) return deletePoint (selected);
    const auto m = k.getModifiers();
    if (m.isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
    {
        if (m.isShiftDown()) redo(); else undo();
        return true;
    }
    if (m.isCommandDown() && (k.getKeyCode() == 'Y' || k.getKeyCode() == 'y')) { redo(); return true; }
    return false;
}

void CurveEditor::showMenu (int pi, int si, juce::Point<float> where)
{
    juce::PopupMenu m;
    const bool canDelete = pi > 0 && pi < curve.size() - 1;
    if (pi >= 0)
    {
        m.addItem (1, "Delete point", canDelete);
        m.addItem (2, "Linear segment after point", pi < curve.size() - 1);
        m.addItem (3, src.snapValue ? "Snap value to note" : "Snap value to grid");
        m.addItem (4, "Snap time to grid", pi > 0 && pi < curve.size() - 1);
        if (src.onLockHere) m.addItem (9, "Phase lock here");
    }
    else
    {
        m.addItem (5, "Add point here");
        m.addItem (2, "Linear segment", si >= 0);
    }
    m.addSeparator();
    m.addItem (6, "Undo", ! undoStack.empty());
    m.addItem (7, "Redo", ! redoStack.empty());
    m.addItem (8, "Reset curve to default", (bool) src.resetToDefault);

    juce::Component::SafePointer<CurveEditor> self (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (
                         localAreaToGlobal (juce::Rectangle<int> ((int) where.x, (int) where.y, 1, 1))),
                     [self, pi, si, where] (int r)
                     {
                         if (self == nullptr || r == 0) return;
                         auto& c = *self;
                         const int seg = pi >= 0 ? pi : si;
                         switch (r)
                         {
                             case 1: c.deletePoint (pi); break;
                             case 2: if (seg >= 0 && seg < c.curve.size() - 1) { c.pushUndo(); c.curve.setTension (seg, 0.0); c.commit(); } break;
                             case 3:
                             {
                                 const auto p = c.curve.getPoints()[(size_t) pi];
                                 const bool was = c.valSnapBtn.getToggleState();
                                 c.valSnapBtn.setToggleState (true, juce::dontSendNotification);
                                 const double v = c.snapV (p.value);
                                 c.valSnapBtn.setToggleState (was, juce::dontSendNotification);
                                 c.pushUndo();
                                 c.curve.movePoint (pi, p.time, v);
                                 c.commit();
                                 break;
                             }
                             case 4:
                             {
                                 const auto p = c.curve.getPoints()[(size_t) pi];
                                 c.pushUndo();
                                 c.curve.movePoint (pi, clampPointTime (c.curve, pi, c.snapT (p.time, true)), p.value);
                                 c.commit();
                                 break;
                             }
                             case 9: if (c.src.onLockHere && pi >= 0) c.src.onLockHere (c.curve.getPoints()[(size_t) pi].time); break;
                             case 5: c.addPointAt (where.x, where.y); break;
                             case 6: c.undo(); break;
                             case 7: c.redo(); break;
                             case 8:
                                 if (c.src.resetToDefault)
                                 {
                                     c.pushUndo();
                                     c.src.resetToDefault();
                                     c.curve = c.src.get();
                                     c.repaint();
                                 }
                                 break;
                             default: break;
                         }
                     });
}
} // namespace kf::ui
