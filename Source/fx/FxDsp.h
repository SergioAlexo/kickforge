#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace kf::fxdsp
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToLin (float db) { return std::pow (10.0f, db * 0.05f); }
inline float linToDb (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

/** RBJ biquad (transposed direct form II, double state). */
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }
    void setIdentity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }

    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2)
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }

    static double clampF (double fs, double f) { return std::clamp (f, 10.0, fs * 0.45); }

    void setHighpass (double fs, double f, double q)
    {
        const double w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setLowpass (double fs, double f, double q)
    {
        const double w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setPeak (double fs, double f, double gainDb, double q)
    {
        const double A = std::pow (10.0, gainDb / 40), w = 2 * kPi * clampF (fs, f) / fs;
        const double c = std::cos (w), al = std::sin (w) / (2 * std::max (q, 0.05));
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void setLowShelf (double fs, double f, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40), w = 2 * kPi * clampF (fs, f) / fs;
        const double c = std::cos (w), al = std::sin (w) / 2 * std::sqrt (2.0), sA = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA),
             (A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA);
    }
    void setHighShelf (double fs, double f, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40), w = 2 * kPi * clampF (fs, f) / fs;
        const double c = std::cos (w), al = std::sin (w) / 2 * std::sqrt (2.0), sA = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
             (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    }
};

/** One-pole lowpass. */
struct OnePole
{
    float z = 0, g = 1;
    void reset() { z = 0; }
    void setCutoff (double fs, double f)
    {
        f = std::clamp (f, 5.0, fs * 0.45);
        g = (float) (1.0 - std::exp (-2.0 * kPi * f / fs));
    }
    float process (float x) { z += g * (x - z); return z; }
};

/** Feed-forward dB-domain compressor core (peak detector, smoothed gain reduction). */
struct DynComp
{
    double fs = 48000;
    float thr = -18, ratio = 4, knee = 6;
    float atk = 0, rel = 0; // one-pole coefficients
    float gr = 0;            // current gain reduction in dB (<= 0), smoothed

    void prepare (double sr) { fs = sr; gr = 0; }
    void setTimes (float attMs, float relMs)
    {
        atk = std::exp (-1.0f / (std::max (attMs, 0.01f) * 0.001f * (float) fs));
        rel = std::exp (-1.0f / (std::max (relMs, 1.0f) * 0.001f * (float) fs));
    }
    /** Returns linear gain to apply for the detector input level `x`. */
    float process (float x)
    {
        const float lvl = linToDb (std::abs (x));
        const float over = lvl - thr;
        float target = 0;
        const float slope = 1.0f / std::max (ratio, 1.0f) - 1.0f;
        if (2 * over > knee)       target = slope * over;
        else if (2 * over > -knee) { const float t = over + knee * 0.5f; target = slope * t * t / (2 * std::max (knee, 1.0e-3f)); }
        // gr in dB negative: attack when target falls (more reduction)
        const float c = target < gr ? atk : rel;
        gr = c * gr + (1 - c) * target;
        return dbToLin (gr);
    }
};
}
