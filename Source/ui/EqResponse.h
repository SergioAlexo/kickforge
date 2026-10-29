#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace kf::ui
{
struct EqSettings
{
    double lowCut = 20, lsF = 100, lsG = 0, p1F = 250, p1G = 0, p1Q = 1, p2F = 2500, p2G = 0, p2Q = 1,
           hsF = 8000, hsG = 0;
};

namespace detail
{
struct Bq { double b0, b1, b2, a0, a1, a2; };

inline double magDb (const Bq& q, double hz, double sr)
{
    using C = std::complex<double>;
    const double w = 2.0 * std::numbers::pi * hz / sr;
    const C z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    const C num = q.b0 + q.b1 * z1 + q.b2 * z2, den = q.a0 + q.a1 * z1 + q.a2 * z2;
    return 20.0 * std::log10 (std::max (1e-9, std::abs (num / den)));
}
inline Bq peak (double f, double g, double Q, double sr)
{
    const double A = std::pow (10.0, g / 40.0), w = 2 * std::numbers::pi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * Q);
    return { 1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A };
}
inline Bq shelf (double f, double g, double sr, bool high)
{
    const double A = std::pow (10.0, g / 40.0), w = 2 * std::numbers::pi * f / sr, c = std::cos (w);
    const double al = std::sin (w) / 2.0 * std::sqrt (2.0), t = 2 * std::sqrt (A) * al;
    if (! high)
        return { A * ((A + 1) - (A - 1) * c + t), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - t),
                 (A + 1) + (A - 1) * c + t, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - t };
    return { A * ((A + 1) + (A - 1) * c + t), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - t),
             (A + 1) - (A - 1) * c + t, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - t };
}
inline Bq highpass (double f, double sr)
{
    const double w = 2 * std::numbers::pi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * 0.7071);
    return { (1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al };
}
} // namespace detail

/** Approximate magnitude response (dB) of the master EQ, for the display only (RBJ biquads;
    the low cut is assumed 2nd-order). */
inline double eqResponseDb (const EqSettings& s, double hz, double sr)
{
    double db = 0.0;
    if (s.lowCut > 20.5) db += detail::magDb (detail::highpass (s.lowCut, sr), hz, sr);
    if (std::abs (s.lsG) > 1e-3) db += detail::magDb (detail::shelf (s.lsF, s.lsG, sr, false), hz, sr);
    if (std::abs (s.p1G) > 1e-3) db += detail::magDb (detail::peak (s.p1F, s.p1G, s.p1Q, sr), hz, sr);
    if (std::abs (s.p2G) > 1e-3) db += detail::magDb (detail::peak (s.p2F, s.p2G, s.p2Q, sr), hz, sr);
    if (std::abs (s.hsG) > 1e-3) db += detail::magDb (detail::shelf (s.hsF, s.hsG, sr, true), hz, sr);
    return db;
}
} // namespace kf::ui
