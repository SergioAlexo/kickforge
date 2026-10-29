#pragma once
#include <array>
#include <memory>
#include <vector>
#include <juce_dsp/juce_dsp.h>
#include "FxDsp.h"

namespace kf
{
/** Swappable effect slot (F-03..F-18). Four generic 0..1 controls (a, b, c, mix) whose meaning depends on the type:

    Clipper  a drive  b tone  c ceiling          Tube     a drive  b tone  c asymmetry
    Wave     a drive  b tone  c sine-fold blend  Tape     a drive  b tone  c head-bump/feedback
    Drive    a drive  b tone  c mid emphasis     BitCrush a bits   b downsample
    Complex  a drive  b stages c tone            Brassify a amount b corner c even<->odd
    RingMod  a freq   b depth c square blend     Delay    a time   b feedback c damping   (mix = send level)
    Reverb   a room   b damp  c width (mix = send level)
    Filter   a cutoff b reso  c type LP/HP/BP    Eq       a low shelf b mid peak c high shelf (+-15 dB)
    Comp     a thresh b ratio c time character   FetComp  a input   b output c ratio step (4/8/12/20/all)

    Distortion types (Clipper..Complex, Brassify) run oversampled according to the quality setting. */
class FxSlot
{
public:
    enum Type { Off = 0, Clipper, Tube, Wave, Tape, Drive, BitCrush, Complex, Brassify,
                RingMod, Delay, Reverb, Filter, Eq, Comp, FetComp, NumTypes };

    static constexpr int kChunk = 32;

    FxSlot();
    void prepare (double sampleRate);
    /** osIndex: 0 = off, 1 = 2x, 2 = 4x, 3 = 8x. Call before process() each block. */
    void setParams (int type, float a, float b, float c, float mix, bool bypass, int osIndex);
    void process (float* x, int n);

    static bool isOversampled (int t) { return (t >= Clipper && t <= Complex && t != BitCrush) || t == Brassify; }

private:
    void resetState();
    void runChunk (float* x, int n);
    void shape (float* p, int m, double fs);
    void updateCoefficients();

    double sr = 48000.0;
    int curType = Off, targetType = Off, osIdx = 0, targetOs = 0;
    float ta = .5f, tb = .5f, tc = .5f, tmix = 1.0f;
    float a = .5f, b = .5f, c = .5f; // values for the current chunk
    juce::SmoothedValue<float> aS, bS, cS, mixS;

    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 3> os;

    // shape state
    fxdsp::OnePole tone, pre, bpLp, bpDc;
    float dcX = 0, dcY = 0, dcR = 0.999f, tapePrev = 0;
    float toneG = 1;
    // bitcrush
    float held = 0; int holdCount = 0;
    // ringmod
    double phase = 0;
    // delay
    std::vector<float> dly;
    int dlyWrite = 0;
    float dlyTimePrev = 100.0f;
    fxdsp::OnePole dlyDamp;
    // reverb / filter / eq / comps
    juce::Reverb reverb;
    juce::dsp::StateVariableTPTFilter<float> svf;
    fxdsp::Biquad eqLs, eqPk, eqHs;
    fxdsp::DynComp comp;
    float fetIn = 1, fetOut = 1;

    std::array<float, kChunk> dry {};
};
}
