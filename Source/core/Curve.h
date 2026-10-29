#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace kf
{
/** Breakpoint envelope. Points are kept sorted by time. Each point's tension
    shapes the segment that LEAVES that point (the last point's tension is unused).
    Tension in -1..1: 0 = linear, >0 = slow start / fast end (u^e, e = 8^tension),
    <0 = fast start / slow end. Time units are the caller's choice (normalized 0..1 or seconds).
    Outside the point range, eval() holds the first/last value. */
class Curve
{
public:
    struct Point
    {
        double time = 0.0, value = 0.0, tension = 0.0;
        bool operator== (const Point&) const = default;
    };

    Curve() = default;
    explicit Curve (std::vector<Point> pts) { setPoints (std::move (pts)); }

    void setPoints (std::vector<Point> pts);
    const std::vector<Point>& getPoints() const noexcept { return points; }
    int size() const noexcept { return (int) points.size(); }
    bool empty() const noexcept { return points.empty(); }

    /** Inserts a point (re-sorting). Returns its index. */
    int addPoint (double time, double value, double tension = 0.0);
    void removePoint (int index);
    /** Moves a point; it is re-sorted, returns its new index. */
    int movePoint (int index, double time, double value);
    void setTension (int index, double tension);

    double eval (double t) const noexcept;

    /** Applies the tension shape to a normalized position u in 0..1. */
    static double shape (double u, double tension) noexcept;

    /** Cheap sequential evaluation: amortized O(1) per call when t only increases.
        The curve must outlive and not be modified during the walk. */
    class Walker
    {
    public:
        explicit Walker (const Curve& c) noexcept : curve (&c) {}
        double at (double t) noexcept;
        void reset() noexcept { seg = 0; }
    private:
        const Curve* curve;
        size_t seg = 0;
    };
    Walker walker() const noexcept { return Walker (*this); }

    /** Fills out[i] = eval(t0 + i*dt). */
    void fill (double* out, int n, double t0, double dt) const noexcept;

    juce::ValueTree toValueTree() const;
    static Curve fromValueTree (const juce::ValueTree&);

    bool operator== (const Curve& o) const { return points == o.points; }

private:
    static double segmentValue (const Point& a, const Point& b, double t) noexcept;
    std::vector<Point> points;
};
} // namespace kf
