#include "ClickGen.h"
#include <algorithm>
#include <cmath>

namespace kf
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
}

const char* clickTypeName (ClickType t) noexcept
{
    switch (t)
    {
        case ClickType::noiseBurst: return "Noise Burst";
        case ClickType::impulse:    return "Impulse";
        case ClickType::chirp:      return "Chirp";
        case ClickType::beater:     return "Beater";
        default: return "?";
    }
}

float ClickGen::noise() noexcept
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; // xorshift32
    return (float) ((double) (std::int32_t) rng * (1.0 / 2147483648.0));
}

void ClickGen::trigger (const ClickSettings& s, std::uint32_t seed) noexcept
{
    cfg = s;
    cfg.toneHz = std::clamp (cfg.toneHz, 20.0f, (float) (sr * 0.45));
    cfg.decayMs = std::clamp (cfg.decayMs, 0.5f, 1000.0f);
    cfg.color = std::clamp (cfg.color, 0.0f, 1.0f);
    rng = seed != 0 ? seed : 1u;
    n = 0; env = 1.0f; phase = 0; ic1 = ic2 = lp1 = lp2 = 0;

    // exponential envelope reaching -60 dB at decayMs
    const double tau = cfg.decayMs * 0.001 * sr;
    envMul = (float) std::exp (-6.907755278982137 / std::max (1.0, tau));

    norm = 1.0f;
    if (cfg.type == ClickType::impulse)
    {
        // resonant band-pass (TPT SVF); resonance rises with colour
        const double q = 0.7 + cfg.color * 14.0;
        g = std::tan (kPi * cfg.toneHz / sr);
        k = 1.0 / q;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        norm = (float) (0.9 / std::sqrt (q));
    }
}

float ClickGen::next() noexcept
{
    float out = 0.0f;
    const double t = (double) n / sr;
    switch (cfg.type)
    {
        case ClickType::noiseBurst:
        {
            // colour 0 = dark (low-passed at tone), colour 1 = bright (high-passed at tone)
            const double a = 1.0 - std::exp (-2.0 * kPi * cfg.toneHz / sr);
            const float white = noise();
            lp1 += a * ((double) white - lp1);
            const float hp = white - (float) lp1;
            out = (1.0f - cfg.color) * (float) lp1 * 1.6f + cfg.color * hp * 0.8f;
            break;
        }
        case ClickType::impulse:
        {
            const double v0 = n == 0 ? 1.0 : 0.0;
            const double v3 = v0 - ic2;
            const double v1 = a1 * ic1 + a2 * v3;
            const double v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            out = (float) v1 * norm * 2.0f; // band-pass output
            break;
        }
        case ClickType::chirp:
        {
            // sine falling from tone*(1+4*colour) to tone
            const double f = cfg.toneHz * (1.0 + 4.0 * cfg.color * std::exp (-t / std::max (1.0e-4, cfg.decayMs * 0.001 * 0.18)));
            phase += 2.0 * kPi * std::min (f, sr * 0.45) / sr;
            if (phase > 2.0 * kPi) phase -= 2.0 * kPi;
            out = (float) std::sin (phase);
            break;
        }
        case ClickType::beater:
        {
            // knock: ~1.5 ms low-passed noise burst + fast-damped, slightly dropping sine
            const double a = 1.0 - std::exp (-2.0 * kPi * (cfg.toneHz * 2.0) / sr);
            lp1 += a * ((double) noise() - lp1);
            const float burst = (float) lp1 * (float) std::exp (-t / 0.0015) * 2.0f;
            const double f = cfg.toneHz * (1.0 + 0.6 * std::exp (-t / 0.002));
            phase += 2.0 * kPi * std::min (f, sr * 0.45) / sr;
            if (phase > 2.0 * kPi) phase -= 2.0 * kPi;
            const float tone = (float) std::sin (phase);
            out = cfg.color * burst + (1.0f - 0.5f * cfg.color) * tone;
            break;
        }
        default: break;
    }
    float fade = 1.0f; // tiny fade-in avoids a hard step for the sine-based types
    if (cfg.type == ClickType::chirp || cfg.type == ClickType::beater)
        fade = (float) std::min (1.0, (double) n / (0.0002 * sr + 1.0));
    const float y = out * env * fade;
    env *= envMul;
    ++n;
    if (! std::isfinite (y)) return 0.0f;
    return std::clamp (y, -1.5f, 1.5f);
}

std::vector<float> renderClick (const ClickSettings& s, double sampleRate, int numSamples)
{
    std::vector<float> out ((size_t) std::max (0, numSamples));
    ClickGen gen;
    gen.prepare (sampleRate);
    gen.trigger (s);
    for (auto& x : out) x = gen.next();
    return out;
}
}
