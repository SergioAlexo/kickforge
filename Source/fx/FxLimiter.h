#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace kf
{
/** Lookahead brickwall limiter (F-22/23/24).
    Gain = moving average (over the lookahead window) of a release-shaped sliding minimum of the required gain,
    which guarantees |out| <= ceiling. An optional tanh saturation stage precedes it ("driven hard" colour). */
class Limiter
{
public:
    static constexpr float kLookMs[3] = { 1.0f, 2.5f, 5.0f };
    static constexpr float kReleaseMs[3] = { 40.0f, 120.0f, 350.0f };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        maxL = (int) std::ceil (0.005 * sr) + 2;
        delay.assign ((size_t) maxL + 2, 0.0f);
        rbuf.assign ((size_t) maxL + 2, 1.0f);
        dqVal.assign ((size_t) maxL + 4, 1.0f);
        dqIdx.assign ((size_t) maxL + 4, 0);
        L = (int) std::lround (kLookMs[1] * 0.001 * sr);
        setRelease (1);
        reset();
    }

    static int lookaheadSamples (int idx, double sr)
    {
        return (int) std::lround (kLookMs[std::clamp (idx, 0, 2)] * 0.001 * sr);
    }

    int latency() const { return L; }

    void setLookahead (int samples)
    {
        samples = std::clamp (samples, 0, maxL);
        if (samples != L) { L = samples; reset(); }
    }
    void setRelease (int idx)
    {
        const double n = kReleaseMs[std::clamp (idx, 0, 2)] * 0.001 * sr;
        relCoef = (float) (1.0 - std::exp (-1.0 / std::max (n, 1.0)));
    }

    void reset()
    {
        std::fill (delay.begin(), delay.end(), 0.0f);
        std::fill (rbuf.begin(), rbuf.end(), 1.0f);
        w = 0; ri = 0; sum = (double) (L + 1); r = 1.0f;
        head = 0; size = 0; counter = 0;
    }

    float process (float x, float inGain, float ceiling, float sat, bool bypass)
    {
        const int dsz = L + 1;
        float in = bypass ? x : x * inGain;
        if (! bypass && sat > 0.0f)
            in = in * (1.0f - sat) + sat * ceiling * std::tanh (in / ceiling);
        delay[(size_t) w] = in;
        const float delayed = delay[(size_t) ((w + 1) % dsz)];
        w = (w + 1) % dsz;
        if (bypass)
        {
            counter++;
            return delayed;
        }

        const float a = std::abs (in);
        const float q = a > ceiling ? ceiling / a : 1.0f;
        const int cap = (int) dqVal.size();
        while (size > 0 && dqVal[(size_t) ((head + size - 1) % cap)] >= q) --size;
        dqVal[(size_t) ((head + size) % cap)] = q;
        dqIdx[(size_t) ((head + size) % cap)] = counter;
        ++size;
        while (dqIdx[(size_t) head] <= counter - dsz) { head = (head + 1) % cap; --size; }
        const float m = dqVal[(size_t) head];
        ++counter;

        r = m < r ? m : r + (m - r) * relCoef;
        sum += (double) r - (double) rbuf[(size_t) ri];
        rbuf[(size_t) ri] = r;
        ri = (ri + 1) % dsz;
        const float avg = (float) (sum / (double) dsz);
        return std::clamp (delayed * std::min (avg, 1.0f), -ceiling, ceiling);
    }

private:
    double sr = 48000.0;
    int maxL = 0, L = 0, w = 0, ri = 0, head = 0, size = 0;
    int64_t counter = 0;
    double sum = 0;
    float r = 1.0f, relCoef = 0.01f;
    std::vector<float> delay, rbuf, dqVal;
    std::vector<int64_t> dqIdx;
};
}
