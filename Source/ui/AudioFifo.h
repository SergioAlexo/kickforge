#pragma once
#include <juce_core/juce_core.h>
#include <cstring>
#include <vector>

namespace kf
{
/** Lock-free single-producer / single-consumer float FIFO. The audio thread push()es the final
    mono output; the UI thread pull()s it for the scope and analyser. push() never blocks or
    allocates; when the FIFO is full the newest samples are dropped. */
class AudioFifo
{
public:
    explicit AudioFifo (int capacity = 1 << 16) : fifo (capacity), data ((size_t) capacity) {}

    void push (const float* src, int n) noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToWrite (n, s1, z1, s2, z2);
        if (z1 > 0) std::memcpy (data.data() + s1, src, (size_t) z1 * sizeof (float));
        if (z2 > 0) std::memcpy (data.data() + s2, src + z1, (size_t) z2 * sizeof (float));
        fifo.finishedWrite (z1 + z2);
    }

    /** Copies up to maxN samples into dst, returns how many were read. */
    int pull (float* dst, int maxN) noexcept
    {
        int s1, z1, s2, z2;
        fifo.prepareToRead (maxN, s1, z1, s2, z2);
        if (z1 > 0) std::memcpy (dst, data.data() + s1, (size_t) z1 * sizeof (float));
        if (z2 > 0) std::memcpy (dst + z1, data.data() + s2, (size_t) z2 * sizeof (float));
        fifo.finishedRead (z1 + z2);
        return z1 + z2;
    }

    int available() const noexcept { return fifo.getNumReady(); }

private:
    juce::AbstractFifo fifo;
    std::vector<float> data;
};
} // namespace kf
