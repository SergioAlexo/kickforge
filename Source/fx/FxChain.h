#pragma once
#include <array>
#include <utility>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "FxSlot.h"
#include "FxLimiter.h"

namespace kf
{
/** Everything the chain reads per block. Plain data so tests can drive it without a processor. */
struct FxSettings
{
    struct Slot { int type = 0; float a = .5f, b = .5f, c = .5f, mix = 1.0f; bool bypass = false; };
    std::array<Slot, 4> slot {};          // bus A1, A2, B1, B2 (serial)
    int quality = 0;                      // oversampling: 0 off, 1 2x, 2 4x, 3 8x
    std::array<float, 4> macro {};        // 0..1
    std::array<int, 4> macroTarget {};    // 0 none, else 1 + slot*4 + (0 a, 1 b, 2 c, 3 mix)
    std::array<float, 4> macroAmt { 1.0f, 1.0f, 1.0f, 1.0f };

    bool eqBypass = true;
    float lowCut = 20, lsF = 100, lsG = 0, p1F = 250, p1G = 0, p1Q = 1, p2F = 2500, p2G = 0, p2Q = 1, hsF = 8000, hsG = 0;

    bool compBypass = true;
    float compThr = -18, compRatio = 3, compAtt = 10, compRel = 120, compMakeup = 0, compMix = 1;

    bool limBypass = false;
    float limIn = 0, limCeil = -0.3f, limSat = 0;
    int limRel = 1, limLook = 1;

    float outDb = 0;
};

/** Master FX chain: bus A (2 slots) -> bus B (2 slots) -> EQ -> compressor -> output gain -> limiter.
    Works in place on channel 0 of the range. Latency = limiter lookahead (see getLatencySamples()). */
class FxChain
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    /** Whole chain in place (legacy / tests): bus A -> bus B -> master. */
    void process (juce::AudioBuffer<float>& buffer, int start, int num);

    /** Split API used by the processor (per-layer routing, F-02). Per block: beginBlock() once,
        then processBus(0 = A, 1 = B) on the signals routed to each bus, then processMaster()
        on the sum. Each channel needs its own FxChain instance (state is mono). */
    void beginBlock();
    void processBus (int bus, float* x, int num);
    void processMaster (float* x, int num);

    /** Stereo-linked versions used by the processor: `left` owns the shared dynamics (Comp/FetComp slots,
        master compressor, limiter: detection = max(|L|,|R|), one gain applied to both channels);
        `right` supplies the per-channel state for the right signal. Settings must match (call beginBlock on both). */
    static void processBusPair (FxChain& left, FxChain& right, int bus, float* l, float* r, int num);
    static void processMasterPair (FxChain& left, FxChain& right, float* l, float* r, int num);

    /** Read parameters from the processor's APVTS on every process() call. */
    void bind (juce::AudioProcessorValueTreeState& apvts);
    void setSettings (const FxSettings& s) { cur = s; }
    int getLatencySamples() const { return limiter.latency(); }

private:
    void applyEq (const FxSettings& s);
    void eqBlock (float* x, int num);
    template <typename T> void link (juce::AudioProcessorValueTreeState&, const juce::String&, T&);

    double sr = 48000.0;
    FxSettings cur, blk;
    bool bound = false;
    std::vector<std::pair<std::atomic<float>*, float*>> fLinks;
    std::vector<std::pair<std::atomic<float>*, int*>> iLinks;
    std::vector<std::pair<std::atomic<float>*, bool*>> bLinks;

    std::array<FxSlot, 4> slots;
    std::vector<float> det;

    fxdsp::Biquad lowCut, ls, p1, p2, hs;
    std::array<float, 12> eqKey {};
    bool eqWasOn = false, compWasOn = false;
    fxdsp::DynComp comp;
    juce::SmoothedValue<float> outGain;
    Limiter limiter;
    juce::SmoothedValue<float> limInGain;
};
}
