#pragma once
#include <cstdint>
#include <vector>

namespace kf
{
/** Synthesized click sources (we ship no third-party samples). */
enum class ClickType { noiseBurst = 0, impulse, chirp, beater, count };

struct ClickSettings
{
    ClickType type = ClickType::beater;
    float toneHz = 2500.0f;  // main frequency / filter cutoff (20..20000)
    float decayMs = 15.0f;   // time to -60 dB (0.5..1000)
    float color = 0.5f;      // 0..1, type-specific brightness / resonance / noise amount
};

/** Per-sample click generator. Realtime-safe: no allocation. Output is bounded. */
class ClickGen
{
public:
    void prepare (double sampleRate) noexcept { sr = sampleRate; }
    /** Restarts the click. seed makes the noise reproducible. */
    void trigger (const ClickSettings& s, std::uint32_t seed = 0x9E3779B9u) noexcept;
    float next() noexcept;
    /** True once the envelope has fallen far below audibility. */
    bool finished() const noexcept { return env < 1.0e-5f && n > 2; }

private:
    float noise() noexcept;

    double sr = 44100.0;
    ClickSettings cfg;
    std::uint32_t rng = 1;
    long n = 0;
    float env = 1.0f, envMul = 1.0f;
    double ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0; // TPT SVF (impulse type)
    double phase = 0, lp1 = 0, lp2 = 0;
    float norm = 1.0f;
};

/** Offline helper: renders one click into a buffer (for tests and previews). */
std::vector<float> renderClick (const ClickSettings&, double sampleRate, int numSamples);
const char* clickTypeName (ClickType) noexcept;
}
