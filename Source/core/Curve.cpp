#include "Curve.h"
#include <algorithm>
#include <cmath>

namespace kf
{
void Curve::setPoints (std::vector<Point> pts)
{
    for (auto& p : pts)
        p.tension = std::clamp (p.tension, -1.0, 1.0);
    std::stable_sort (pts.begin(), pts.end(), [] (const Point& a, const Point& b) { return a.time < b.time; });
    points = std::move (pts);
}

int Curve::addPoint (double time, double value, double tension)
{
    Point p { time, value, std::clamp (tension, -1.0, 1.0) };
    auto it = std::upper_bound (points.begin(), points.end(), p,
                                [] (const Point& a, const Point& b) { return a.time < b.time; });
    return (int) std::distance (points.begin(), points.insert (it, p));
}

void Curve::removePoint (int index)
{
    if (index >= 0 && index < size())
        points.erase (points.begin() + index);
}

int Curve::movePoint (int index, double time, double value)
{
    if (index < 0 || index >= size())
        return index;
    const auto t = points[(size_t) index].tension;
    removePoint (index);
    return addPoint (time, value, t);
}

void Curve::setTension (int index, double tension)
{
    if (index >= 0 && index < size())
        points[(size_t) index].tension = std::clamp (tension, -1.0, 1.0);
}

double Curve::shape (double u, double tension) noexcept
{
    u = std::clamp (u, 0.0, 1.0);
    if (tension == 0.0)
        return u;
    return std::pow (u, std::pow (8.0, std::clamp (tension, -1.0, 1.0)));
}

double Curve::segmentValue (const Point& a, const Point& b, double t) noexcept
{
    const double dt = b.time - a.time;
    const double u = dt > 0.0 ? (t - a.time) / dt : 1.0;
    return a.value + (b.value - a.value) * shape (u, a.tension);
}

double Curve::eval (double t) const noexcept
{
    if (points.empty()) return 0.0;
    if (t <= points.front().time) return points.front().value;
    if (t >= points.back().time) return points.back().value;
    auto it = std::upper_bound (points.begin(), points.end(), t,
                                [] (double x, const Point& p) { return x < p.time; });
    return segmentValue (*(it - 1), *it, t);
}

double Curve::Walker::at (double t) noexcept
{
    const auto& p = curve->points;
    if (p.empty()) return 0.0;
    if (t <= p.front().time) { seg = 0; return p.front().value; }
    if (t >= p.back().time) { seg = p.size() - 1; return p.back().value; }
    if (seg >= p.size() - 1 || t < p[seg].time) seg = 0;
    while (seg + 1 < p.size() - 1 && t >= p[seg + 1].time) ++seg;
    return segmentValue (p[seg], p[seg + 1], t);
}

void Curve::fill (double* out, int n, double t0, double dt) const noexcept
{
    auto w = walker();
    for (int i = 0; i < n; ++i)
        out[i] = w.at (t0 + i * dt);
}

juce::ValueTree Curve::toValueTree() const
{
    juce::ValueTree v ("CURVE");
    for (auto& p : points)
    {
        juce::ValueTree c ("POINT");
        c.setProperty ("t", p.time, nullptr);
        c.setProperty ("v", p.value, nullptr);
        c.setProperty ("k", p.tension, nullptr);
        v.appendChild (c, nullptr);
    }
    return v;
}

Curve Curve::fromValueTree (const juce::ValueTree& v)
{
    std::vector<Point> pts;
    if (v.hasType ("CURVE"))
        for (auto c : v)
            pts.push_back ({ (double) c.getProperty ("t", 0.0), (double) c.getProperty ("v", 0.0),
                             (double) c.getProperty ("k", 0.0) });
    return Curve (std::move (pts));
}
} // namespace kf
