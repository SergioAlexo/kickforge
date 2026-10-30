#pragma once
#include "CurveGeometry.h"
#include "KfLookAndFeel.h"
#include <functional>

namespace kf::ui
{
/** Node-based editor for a kf::Curve (U-02..U-08, S-12).
    Double-click empty space adds a point; drag moves; double-click or right-click a point to delete /
    open its menu; drag a segment to bend its tension; wheel = zoom time, shift+wheel = pan,
    ctrl+wheel = zoom value. Undo/redo is local to the editor (Ctrl+Z / Ctrl+Y). */
class CurveEditor : public juce::Component, private juce::Timer
{
public:
    struct Source
    {
        juce::String name;
        std::function<Curve()> get;
        std::function<void (const Curve&)> set;      // thread-safe setter of the owner
        std::function<void()> resetToDefault;
        std::function<void (double t)> onLockHere;   // optional: "Phase lock here" (pitch curve)
        double vmin = 0.0, vmax = 1.0;               // default visible value range
        bool fitToCurve = false;                     // Fit / select / external load frames the curve's own value range (zoom out stays possible)
        double hardMin = -1e9, hardMax = 1e9;        // node value limits
        std::function<double()> lengthMs;            // optional: time axis in ms
        std::function<double (double)> snapValue;    // optional: note snap (pitch curves)
        std::function<juce::String (double t, double v)> valueText; // optional: extra readout
        juce::String unit;
        juce::Colour colour { 0xffff7a2f };
    };

    CurveEditor();
    ~CurveEditor() override { stopTimer(); }

    void setSource (Source);

    /** Backdrop painted inside the plot, under the grid and the curve (waveform layers). Args: view, plot rect, time axis length in ms. */
    std::function<void (juce::Graphics&, const CurveView&, juce::Rectangle<int>, double)> paintBackdrop;
    /** Layer toggles in the toolbar: 0 Sub, 1 Clicks, 2 Reference, 3 Mix. */
    static constexpr int numLayerToggles = 4;
    std::function<void (int, bool)> onLayerToggle;
    void setLayerToggle (int i, bool on) { layerBtns[(size_t) i].setToggleState (on, juce::dontSendNotification); repaint(); }
    void setLayerToggleEnabled (int i, bool en) { layerBtns[(size_t) i].setEnabled (en); }
    bool getLayerToggle (int i) const { return layerBtns[(size_t) i].getToggleState(); }
    juce::Button& getLayerButton (int i) { return layerBtns[(size_t) i]; }
    double getLengthMs() const { return src.lengthMs ? std::max (1.0, src.lengthMs()) : 1000.0; }
    const Curve& getCurve() const noexcept { return curve; }

    void undo();
    void redo();
    void fitView();

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    struct AxisLabel { juce::Rectangle<int> rect; juce::String text; double pos = 0.0; };
    /** Axis label layout (also used by paint). Every rect lies inside getLocalBounds(); overlapping labels are dropped. */
    std::vector<AxisLabel> timeLabels() const;
    std::vector<AxisLabel> valueLabels() const;

    // exposed for tests
    CurveView view;
    void addPointAt (float px, float py);
    bool deletePoint (int idx);

private:
    enum class Drag { none, point, segment, pan };

    void timerCallback() override;
    void updateViewRect();
    void commit();
    void pushUndo();
    void showMenu (int pointIdx, int segIdx, juce::Point<float> where);
    double timeStep() const;
    double valueStep() const;
    double snapT (double t, bool force = false) const;
    double snapV (double v) const;
    juce::String timeText (double t) const;
    juce::String readoutText (double t, double v) const;

    Source src;
    Curve curve;
    std::vector<Curve> undoStack, redoStack;
    int selected = -1, hoverPoint = -1, hoverSeg = -1;
    Drag drag = Drag::none;
    bool dragMoved = false;
    int dragSeg = -1;
    double dragU = 0.5, panT0 = 0.0, panT1 = 1.0;
    Curve beforeDrag;
    juce::Point<float> mousePos;
    bool mouseInside = false;

    juce::ToggleButton timeSnapBtn { "Time snap" }, valSnapBtn { "Note snap" }, logBtn { "Log time" };
    std::array<juce::TextButton, 4> layerBtns;
    juce::TextButton fitBtn { "Fit" }, undoBtn { "Undo" }, redoBtn { "Redo" };
    juce::Rectangle<int> plot;
};
} // namespace kf::ui
