#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>
#include <vector>

namespace kf
{
/** Plays a mono clip (e.g. the import dialog's selected region) through the plugin's own main output,
    so auditioning never opens a second audio device next to the host's.
    Message thread: play / stop / collectGarbage. Audio thread: prepare (via prepareToPlay) / process.
    Clips are handed over by atomic pointer exchange; the audio thread never allocates or frees:
    a replaced clip is parked in `retired` and deleted by the message thread on its next call. */
class PreviewPlayer
{
public:
    struct Clip
    {
        std::vector<float> data;
        double rate = 44100.0;
    };

    ~PreviewPlayer()
    {
        delete pending.exchange (nullptr);
        delete retired.exchange (nullptr);
        delete current;
    }

    // ---- message thread
    void play (std::vector<float> data, double rate)
    {
        collectGarbage();
        auto* c = new Clip { std::move (data), rate };
        delete pending.exchange (c); // a clip the audio thread never picked up is still ours to free
        stopRequest.store (false);
    }
    void stop()
    {
        delete pending.exchange (nullptr); // never started: drop it here
        stopRequest.store (true);
        collectGarbage();
    }
    void collectGarbage() { delete retired.exchange (nullptr); }
    bool isPlaying() const noexcept { return playing.load() || pending.load() != nullptr; }
    float getProgress() const noexcept { return progress.load(); }

    // ---- audio thread
    void prepare (double sampleRate) { hostRate = sampleRate > 0 ? sampleRate : 44100.0; }

    /** Adds the clip into every channel of `out` (the main bus) at `gain`. */
    void process (juce::AudioBuffer<float>& out, float gain = 0.8f)
    {
        if (pending.load() != nullptr)
        {
            Clip* expected = nullptr;
            // Take the new clip only once the old one can be parked; otherwise retry next block.
            if (current == nullptr || retired.compare_exchange_strong (expected, current))
            {
                current = pending.exchange (nullptr);
                pos = 0.0;
                fadeOut = -1;
                playing.store (current != nullptr);
            }
        }
        if (current == nullptr || ! playing.load()) return;

        const double fade = std::max (1.0, 0.004 * current->rate); // 4 ms edge fades avoid clicks
        const int stopLen = std::max (1, (int) (0.004 * hostRate));
        if (stopRequest.exchange (false) && fadeOut < 0) fadeOut = stopLen;

        const double ratio = current->rate / hostRate;
        const double last = (double) current->data.size() - 1.0;
        const auto& d = current->data;
        const int n = out.getNumSamples(), chans = out.getNumChannels();
        for (int i = 0; i < n; ++i)
        {
            if (pos >= last || fadeOut == 0) { playing.store (false); break; }
            const int i0 = (int) pos;
            const float fr = (float) (pos - i0);
            float v = d[(size_t) i0] * (1.0f - fr) + d[(size_t) i0 + 1] * fr;
            v *= (float) std::min ({ 1.0, pos / fade, (last - pos) / fade });
            if (fadeOut > 0) v *= (float) --fadeOut / (float) stopLen;
            for (int c = 0; c < chans; ++c)
                out.addSample (c, i, v * gain);
            pos += ratio;
        }
        progress.store ((float) (pos / std::max (1.0, last)));
    }

private:
    std::atomic<Clip*> pending { nullptr }, retired { nullptr };
    Clip* current = nullptr; // audio thread only
    std::atomic<bool> playing { false }, stopRequest { false };
    std::atomic<float> progress { 0.0f };
    double hostRate = 44100.0, pos = 0.0;
    int fadeOut = -1;
};
} // namespace kf
