#pragma once
#include "core/Curve.h"
#include <algorithm>
#include <cmath>

namespace kf::ui
{
/** Pure (GUI-free) geometry of the curve editor: time/value <-> pixel mapping, hit testing and
    snapping. Kept separate from the component so it can be unit-tested. */
struct CurveView
{
    double t0 = 0.0, t1 = 1.0;       // visible time range (normalized curve time)
    double vBottom = 0.0, vTop = 1.0; // visible value range
    bool logTime = false;             // log warp: zooms into the attack
    float x = 0, y = 0, w = 100, h = 100; // plot area in pixels
    static constexpr double logK = 40.0;

    float timeToX (double t) const noexcept
    {
        double u = (t - t0) / (t1 - t0);
        if (logTime)
            u = (u < 0 ? -1.0 : 1.0) * std::log1p (logK * std::abs (u)) / std::log1p (logK);
        return x + (float) (u * w);
    }
    double xToTime (float px) const noexcept
    {
        double u = (px - x) / w;
        if (logTime)
            u = (u < 0 ? -1.0 : 1.0) * std::expm1 (std::abs (u) * std::log1p (logK)) / logK;
        return t0 + u * (t1 - t0);
    }
    float valueToY (double v) const noexcept
    {
        return y + h - (float) ((v - vBottom) / (vTop - vBottom) * h);
    }
    double yToValue (float py) const noexcept
    {
        return vBottom + (double) (y + h - py) / h * (vTop - vBottom);
    }
};

/** 1/2/5 x 10^k step giving roughly `targetLines` divisions across `span`. */
inline double niceStep (double span, int targetLines = 8)
{
    if (! (span > 0.0)) return 1.0;
    const double raw = span / targetLines, mag = std::pow (10.0, std::floor (std::log10 (raw)));
    for (double m : { 1.0, 2.0, 5.0, 10.0 })
        if (m * mag >= raw * 0.999) return m * mag;
    return 10.0 * mag;
}

inline double snapToStep (double v, double step) { return step > 0 ? std::round (v / step) * step : v; }

/** Index of the point within `radius` px of (px,py) (nearest wins), or -1. */
inline int hitPoint (const Curve& c, const CurveView& v, float px, float py, float radius = 9.0f)
{
    int best = -1;
    float bestD = radius * radius;
    for (int i = 0; i < c.size(); ++i)
    {
        const auto& p = c.getPoints()[(size_t) i];
        const float dx = v.timeToX (p.time) - px, dy = v.valueToY (p.value) - py;
        const float d = dx * dx + dy * dy;
        if (d <= bestD) { bestD = d; best = i; }
    }
    return best;
}

namespace detail
{
inline float distToSegment (float px, float py, float ax, float ay, float bx, float by)
{
    const float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    const float t = l2 > 0 ? std::clamp (((px - ax) * dx + (py - ay) * dy) / l2, 0.0f, 1.0f) : 0.0f;
    return std::hypot (px - (ax + t * dx), py - (ay + t * dy));
}
}

/** Index i of the segment (points i -> i+1) whose drawn line passes within `tol` px of (px,py),
    or -1. Points themselves are excluded (they take priority via hitPoint). */
inline int hitSegment (const Curve& c, const CurveView& v, float px, float py, float tol = 6.0f)
{
    if (hitPoint (c, v, px, py, 9.0f) >= 0) return -1;
    const auto& pts = c.getPoints();
    for (int i = 0; i + 1 < c.size(); ++i)
    {
        const auto &a = pts[(size_t) i], &b = pts[(size_t) i + 1];
        constexpr int n = 32;
        float lx = v.timeToX (a.time), ly = v.valueToY (a.value);
        for (int k = 1; k <= n; ++k)
        {
            const double t = a.time + (b.time - a.time) * k / n;
            const float cx = v.timeToX (t), cy = v.valueToY (c.eval (t));
            if (detail::distToSegment (px, py, lx, ly, cx, cy) <= tol) return i;
            lx = cx; ly = cy;
        }
    }
    return -1;
}

/** Tension that makes the segment pass through normalized position u (0..1 along the segment)
    at normalized height s (0..1 from start value to end value). Inverse of Curve::shape. */
inline double tensionForTarget (double u, double s)
{
    u = std::clamp (u, 0.02, 0.98);
    s = std::clamp (s, 0.001, 0.999);
    const double e = std::log (s) / std::log (u);
    return std::clamp (std::log (e) / std::log (8.0), -1.0, 1.0);
}

/** Clamps a dragged point's time between its neighbours; end points keep their time. */
inline double clampPointTime (const Curve& c, int idx, double t, double gap = 1e-4)
{
    const auto& p = c.getPoints();
    if (idx <= 0 || idx >= c.size() - 1) return p[(size_t) idx].time;
    return std::clamp (t, p[(size_t) idx - 1].time + gap, p[(size_t) idx + 1].time - gap);
}
} // namespace kf::ui
