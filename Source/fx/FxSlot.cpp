#include "FxSlot.h"

namespace kf
{
using namespace fxdsp;

namespace
{
inline float fold (float v)
{
    v = std::clamp (v, -1000.0f, 1000.0f);
    return (float) (0.6366197723675814 * std::asin (std::sin (1.5707963267948966 * (double) v)));
}
}

FxSlot::FxSlot() = default;

void FxSlot::prepare (double sampleRate)
{
    sr = sampleRate;
    for (int i = 0; i < 3; ++i)
    {
        os[(size_t) i] = std::make_unique<juce::dsp::Oversampling<float>> (
            1, (size_t) (i + 1), juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        os[(size_t) i]->initProcessing ((size_t) kChunk);
    }
    for (auto* s : { &aS, &bS, &cS, &mixS }) s->reset (sr, 0.02);
    dly.assign ((size_t) (sr * 0.55) + 8, 0.0f);
    reverb.setSampleRate (sr);
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) kChunk, 1 };
    svf.prepare (spec);
    comp.prepare (sr);
    curType = targetType = Off;
    osIdx = targetOs = 0;
    resetState();
}

void FxSlot::resetState()
{
    tone.reset(); pre.reset(); bpLp.reset(); bpDc.reset();
    dcX = dcY = tapePrev = held = 0; holdCount = 0; phase = 0;
    std::fill (dly.begin(), dly.end(), 0.0f); dlyWrite = 0; dlyDamp.reset();
    reverb.reset(); svf.reset();
    eqLs.reset(); eqPk.reset(); eqHs.reset();
    comp.gr = 0;
    for (auto& o : os) if (o) o->reset();
    dcR = 1.0f; // recomputed in shape()
}

void FxSlot::setParams (int type, float na, float nb, float nc, float nmix, bool bypass, int osIndex)
{
    const int eff = bypass ? (int) Off : juce::jlimit (0, (int) NumTypes - 1, type);
    na = std::clamp (na, 0.0f, 1.0f); nb = std::clamp (nb, 0.0f, 1.0f);
    nc = std::clamp (nc, 0.0f, 1.0f); nmix = std::clamp (nmix, 0.0f, 1.0f);
    osIndex = juce::jlimit (0, 3, osIndex);
    if (eff != curType || (isOversampled (eff) && osIndex != osIdx))
    {
        curType = eff;
        osIdx = osIndex;
        resetState();
        aS.setCurrentAndTargetValue (na); bS.setCurrentAndTargetValue (nb);
        cS.setCurrentAndTargetValue (nc);
        mixS.setCurrentAndTargetValue (0.0f); // fade the new effect in
    }
    aS.setTargetValue (na); bS.setTargetValue (nb); cS.setTargetValue (nc); mixS.setTargetValue (nmix);
}

void FxSlot::process (float* x, int n)
{
    if (curType == Off) return;
    while (n > 0)
    {
        const int m = std::min (n, kChunk);
        runChunk (x, m);
        x += m; n -= m;
    }
}

void FxSlot::runChunk (float* x, int m)
{
    a = aS.skip (m); b = bS.skip (m); c = cS.skip (m);
    const float mixPrev = mixS.getCurrentValue();
    const float mixNow = mixS.skip (m);
    std::copy (x, x + m, dry.begin());

    updateCoefficients();

    if (isOversampled (curType) && osIdx > 0)
    {
        float* chans[1] = { x };
        juce::dsp::AudioBlock<float> blk (chans, 1, (size_t) m);
        auto& o = *os[(size_t) osIdx - 1];
        auto up = o.processSamplesUp (blk);
        shape (up.getChannelPointer (0), (int) up.getNumSamples(), sr * (double) (1 << osIdx));
        o.processSamplesDown (blk);
    }
    else
        shape (x, m, sr);

    const bool additive = curType == Delay || curType == Reverb;
    for (int i = 0; i < m; ++i)
    {
        const float mx = mixPrev + (mixNow - mixPrev) * (float) (i + 1) / (float) m;
        float y = additive ? dry[(size_t) i] + mx * x[i] : dry[(size_t) i] + mx * (x[i] - dry[(size_t) i]);
        if (! std::isfinite (y)) y = 0.0f;
        x[i] = y;
    }
}

void FxSlot::updateCoefficients()
{
    switch (curType)
    {
        case Eq:
            eqLs.setLowShelf (sr, 120.0, (a - 0.5f) * 30.0f);
            eqPk.setPeak (sr, 1000.0, (b - 0.5f) * 30.0f, 1.0);
            eqHs.setHighShelf (sr, 6000.0, (c - 0.5f) * 30.0f);
            break;
        case Filter:
        {
            svf.setType (c < 0.34f ? juce::dsp::StateVariableTPTFilterType::lowpass
                                   : c < 0.67f ? juce::dsp::StateVariableTPTFilterType::highpass
                                               : juce::dsp::StateVariableTPTFilterType::bandpass);
            svf.setCutoffFrequency (std::min (30.0f * std::pow (600.0f, a), (float) sr * 0.45f));
            svf.setResonance (0.5f + b * b * 9.0f);
            break;
        }
        case Reverb:
        {
            juce::Reverb::Parameters p;
            p.roomSize = a; p.damping = b; p.width = c; p.wetLevel = 0.4f; p.dryLevel = 0.0f; p.freezeMode = 0.0f;
            reverb.setParameters (p);
            break;
        }
        case Comp:
            comp.thr = -50.0f + 50.0f * a;
            comp.ratio = 1.0f + 19.0f * b * b;
            comp.knee = 6.0f;
            comp.setTimes (0.5f + 40.0f * c, 20.0f + 480.0f * c);
            break;
        case FetComp:
        {
            fetIn = dbToLin (a * 30.0f);
            fetOut = dbToLin ((b - 0.5f) * 24.0f);
            comp.thr = -12.0f;
            comp.ratio = c < 0.2f ? 4.0f : c < 0.4f ? 8.0f : c < 0.6f ? 12.0f : 20.0f;
            comp.knee = c >= 0.8f ? 0.0f : 2.0f;
            comp.setTimes (c >= 0.8f ? 0.03f : 0.08f, c >= 0.8f ? 40.0f : 80.0f);
            break;
        }
        case Delay:
            dlyDamp.setCutoff (sr, 20000.0 * std::pow (0.1, (double) c));
            break;
        default: break;
    }
}

void FxSlot::shape (float* p, int m, double fs)
{
    const float drive = dbToLin (a * 36.0f);
    const float toneParam = curType == Complex ? c : b;
    const bool toneOpen = toneParam > 0.995f;
    tone.setCutoff (fs, 1000.0 * std::pow (20.0, (double) toneParam));
    dcR = (float) (1.0 - 2.0 * kPi * 10.0 / fs);
    auto toneFn = [&] (float v) { return toneOpen ? v : tone.process (v); };
    auto dcBlock = [&] (float v)
    {
        const float y = v - dcX + dcR * dcY;
        dcX = v; dcY = y;
        return y;
    };
    auto sane = [] (float v) { return std::isfinite (v) ? v : 0.0f; };

    switch (curType)
    {
        case Clipper:
        {
            const float L = dbToLin (-18.0f * (1.0f - c));
            for (int i = 0; i < m; ++i) p[i] = toneFn (std::clamp (sane (p[i]) * drive, -L, L));
            break;
        }
        case Tube:
        {
            const float bias = (c - 0.5f) * 1.2f, tb0 = std::tanh (bias);
            for (int i = 0; i < m; ++i)
                p[i] = toneFn (dcBlock (std::tanh (sane (p[i]) * drive + bias) - tb0));
            break;
        }
        case Wave:
            for (int i = 0; i < m; ++i)
            {
                const float v = std::clamp (sane (p[i]) * drive, -1000.0f, 1000.0f);
                p[i] = toneFn ((1.0f - c) * std::tanh (v) + c * std::sin (v));
            }
            break;
        case Tape:
        {
            const float fb = 0.35f * c, comp_ = 1.0f - fb;
            for (int i = 0; i < m; ++i)
            {
                const float y = std::tanh (sane (p[i]) * drive + fb * tapePrev);
                tapePrev = y;
                p[i] = toneFn (dcBlock (y * comp_));
            }
            break;
        }
        case Drive:
            pre.setCutoff (fs, 700.0);
            for (int i = 0; i < m; ++i)
            {
                const float v = sane (p[i]) * drive;
                const float v2 = v + c * 2.0f * (v - pre.process (v));
                p[i] = toneFn (v2 / (1.0f + std::abs (v2)));
            }
            break;
        case BitCrush:
        {
            const float levels = std::pow (2.0f, 16.0f - a * 15.0f); // 16 .. 1 bit
            const int hold = 1 + (int) (b * b * 63.0f);
            for (int i = 0; i < m; ++i)
            {
                if (holdCount <= 0)
                {
                    const float v = std::clamp (sane (p[i]), -1.0f, 1.0f);
                    held = std::round (v * levels * 0.5f) / (levels * 0.5f);
                    holdCount = hold;
                }
                --holdCount;
                p[i] = held;
            }
            break;
        }
        case Complex:
            for (int i = 0; i < m; ++i)
            {
                const float y1 = fold (sane (p[i]) * drive);
                const float y2 = fold (y1 * (1.0f + 3.0f * b));
                p[i] = toneFn (y1 + (y2 - y1) * b);
            }
            break;
        case Brassify:
        {
            bpLp.setCutoff (fs, 300.0 * std::pow (16.0, (double) b));
            bpDc.setCutoff (fs, 30.0);
            const float amt = a * 1.5f, g = 3.0f * (1.0f + c * 2.0f);
            for (int i = 0; i < m; ++i)
            {
                const float x = sane (p[i]);
                const float s = std::tanh (g * (x - bpLp.process (x)));
                const float even = s * s;
                const float evenAc = even - bpDc.process (even);
                p[i] = x + amt * ((1.0f - c) * 1.5f * evenAc + c * s);
            }
            break;
        }
        case RingMod:
        {
            const double inc = 2.0 * kPi * 20.0 * std::pow (250.0, (double) a) / fs;
            for (int i = 0; i < m; ++i)
            {
                const float s = (float) std::sin (phase);
                phase += inc; if (phase > 2.0 * kPi) phase -= 2.0 * kPi;
                const float car = (1.0f - c) * s + c * std::tanh (6.0f * s);
                p[i] = sane (p[i]) * (1.0f - b + b * car);
            }
            break;
        }
        case Delay:
        {
            const int size = (int) dly.size();
            const float t = 1.0f + 499.0f * a, fb = std::min (b, 1.0f) * 0.95f;
            for (int i = 0; i < m; ++i)
            {
                const float ti = dlyTimePrev + (t - dlyTimePrev) * (float) (i + 1) / (float) m;
                float rp = (float) dlyWrite - ti * 0.001f * (float) sr;
                while (rp < 0) rp += (float) size;
                const int i0 = (int) rp % size, i1 = (i0 + 1) % size;
                const float fr = rp - std::floor (rp);
                const float w = dly[(size_t) i0] * (1.0f - fr) + dly[(size_t) i1] * fr;
                dly[(size_t) dlyWrite] = sane (p[i]) + fb * dlyDamp.process (w);
                dlyWrite = (dlyWrite + 1) % size;
                p[i] = w;
            }
            dlyTimePrev = t;
            break;
        }
        case Reverb:
            reverb.processMono (p, m);
            break;
        case Filter:
            for (int i = 0; i < m; ++i) p[i] = svf.processSample (0, sane (p[i]));
            break;
        case Eq:
            for (int i = 0; i < m; ++i) p[i] = eqHs.process (eqPk.process (eqLs.process (sane (p[i]))));
            break;
        case Comp:
            for (int i = 0; i < m; ++i) { const float x = sane (p[i]); p[i] = x * comp.process (x); }
            break;
        case FetComp:
            for (int i = 0; i < m; ++i)
            {
                const float x = sane (p[i]) * fetIn;
                const float y = x * comp.process (x);
                p[i] = std::tanh (y * 0.5f) * 2.0f * fetOut;
            }
            break;
        default: break;
    }
}
}
