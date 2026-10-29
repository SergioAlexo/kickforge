#include "FxChain.h"
#include "FxParams.h"

namespace kf
{
template <typename T>
void FxChain::link (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, T& dest)
{
    auto* p = apvts.getRawParameterValue (id);
    jassert (p != nullptr);
    if (p == nullptr) return;
    if constexpr (std::is_same_v<T, float>)     fLinks.emplace_back (p, &dest);
    else if constexpr (std::is_same_v<T, int>)  iLinks.emplace_back (p, &dest);
    else                                        bLinks.emplace_back (p, &dest);
}

void FxChain::bind (juce::AudioProcessorValueTreeState& apvts)
{
    fLinks.clear(); iLinks.clear(); bLinks.clear();
    for (int s = 0; s < 4; ++s)
    {
        const juce::String p = juce::String ("fx_") + kFxSlotNames[s];
        auto& d = cur.slot[(size_t) s];
        link (apvts, p + "_type", d.type); link (apvts, p + "_a", d.a); link (apvts, p + "_b", d.b);
        link (apvts, p + "_c", d.c); link (apvts, p + "_mix", d.mix); link (apvts, p + "_byp", d.bypass);
    }
    link (apvts, "fx_quality", cur.quality);
    for (int i = 0; i < 4; ++i)
    {
        const juce::String p = "fx_macro" + juce::String (i + 1);
        link (apvts, p, cur.macro[(size_t) i]); link (apvts, p + "_target", cur.macroTarget[(size_t) i]);
        link (apvts, p + "_amt", cur.macroAmt[(size_t) i]);
    }
    link (apvts, "fx_eq_byp", cur.eqBypass); link (apvts, "fx_eq_lc", cur.lowCut);
    link (apvts, "fx_eq_ls_f", cur.lsF); link (apvts, "fx_eq_ls_g", cur.lsG);
    link (apvts, "fx_eq_p1_f", cur.p1F); link (apvts, "fx_eq_p1_g", cur.p1G); link (apvts, "fx_eq_p1_q", cur.p1Q);
    link (apvts, "fx_eq_p2_f", cur.p2F); link (apvts, "fx_eq_p2_g", cur.p2G); link (apvts, "fx_eq_p2_q", cur.p2Q);
    link (apvts, "fx_eq_hs_f", cur.hsF); link (apvts, "fx_eq_hs_g", cur.hsG);
    link (apvts, "fx_comp_byp", cur.compBypass); link (apvts, "fx_comp_thr", cur.compThr);
    link (apvts, "fx_comp_ratio", cur.compRatio); link (apvts, "fx_comp_att", cur.compAtt);
    link (apvts, "fx_comp_rel", cur.compRel); link (apvts, "fx_comp_makeup", cur.compMakeup);
    link (apvts, "fx_comp_mix", cur.compMix);
    link (apvts, "fx_lim_byp", cur.limBypass); link (apvts, "fx_lim_in", cur.limIn);
    link (apvts, "fx_lim_ceil", cur.limCeil); link (apvts, "fx_lim_rel", cur.limRel);
    link (apvts, "fx_lim_look", cur.limLook); link (apvts, "fx_lim_sat", cur.limSat);
    link (apvts, "fx_out", cur.outDb);
    bound = true;
}

void FxChain::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    det.assign ((size_t) std::max (16, maxBlock), 0.0f);
    for (auto& s : slots) s.prepare (sr);
    comp.prepare (sr);
    limiter.prepare (sr);
    outGain.reset (sr, 0.02); limInGain.reset (sr, 0.02);
    outGain.setCurrentAndTargetValue (fxdsp::dbToLin (cur.outDb));
    limInGain.setCurrentAndTargetValue (fxdsp::dbToLin (cur.limIn));
    limiter.setLookahead (Limiter::lookaheadSamples (cur.limLook, sr));
    limiter.setRelease (cur.limRel);
    for (auto* b : { &lowCut, &ls, &p1, &p2, &hs }) { b->reset(); b->setIdentity(); }
    eqKey.fill (-1.0e9f);
    eqWasOn = compWasOn = false;
}

void FxChain::applyEq (const FxSettings& s)
{
    const std::array<float, 12> key { s.lowCut, s.lsF, s.lsG, s.p1F, s.p1G, s.p1Q, s.p2F, s.p2G, s.p2Q, s.hsF, s.hsG, 0.0f };
    if (key == eqKey) return;
    eqKey = key;
    lowCut.setHighpass (sr, s.lowCut, 0.7071);
    ls.setLowShelf (sr, s.lsF, s.lsG);
    p1.setPeak (sr, s.p1F, s.p1G, s.p1Q);
    p2.setPeak (sr, s.p2F, s.p2G, s.p2Q);
    hs.setHighShelf (sr, s.hsF, s.hsG);
}

void FxChain::beginBlock()
{
    if (bound)
    {
        for (auto& l : fLinks) *l.second = l.first->load();
        for (auto& l : iLinks) *l.second = (int) std::lround (l.first->load());
        for (auto& l : bLinks) *l.second = l.first->load() >= 0.5f;
    }
    blk = cur;
    // F-19 macros: add macro * amount to the targeted slot knob
    for (size_t m = 0; m < 4; ++m)
    {
        const int t = blk.macroTarget[m] - 1;
        if (t < 0 || t >= 16) continue;
        auto& sl = blk.slot[(size_t) (t / 4)];
        float* knob[4] = { &sl.a, &sl.b, &sl.c, &sl.mix };
        float& k = *knob[t % 4];
        k = std::clamp (k + blk.macro[m] * blk.macroAmt[m], 0.0f, 1.0f);
    }
}

void FxChain::processBus (int bus, float* x, int num)
{
    if (num <= 0) return;
    for (size_t i = (size_t) bus * 2; i < (size_t) bus * 2 + 2 && i < 4; ++i)
    {
        const auto& d = blk.slot[i];
        slots[i].setParams (d.type, d.a, d.b, d.c, d.mix, d.bypass, blk.quality);
        slots[i].process (x, num);
    }
}

void FxChain::process (juce::AudioBuffer<float>& buffer, int start, int num)
{
    if (num <= 0) return;
    beginBlock();
    float* x = buffer.getWritePointer (0) + start;
    processBus (0, x, num);
    processBus (1, x, num);
    processMaster (x, num);
}

void FxChain::eqBlock (float* x, int num)
{
    const FxSettings& s = blk;
    if (! s.eqBypass)
    {
        if (! eqWasOn) { for (auto* b : { &lowCut, &ls, &p1, &p2, &hs }) b->reset(); eqKey.fill (-1.0e9f); }
        applyEq (s);
        const bool lc = s.lowCut > 20.5f;
        for (int i = 0; i < num; ++i)
        {
            float v = x[i];
            if (lc) v = lowCut.process (v);
            x[i] = hs.process (p2.process (p1.process (ls.process (v))));
        }
    }
    eqWasOn = ! s.eqBypass;
}

void FxChain::processBusPair (FxChain& L, FxChain& R, int bus, float* l, float* r, int num)
{
    while (num > 0)
    {
        const int n = std::min (num, (int) L.det.size());
        for (size_t i = (size_t) bus * 2; i < (size_t) bus * 2 + 2 && i < 4; ++i)
        {
            const auto& d = L.blk.slot[i];
            L.slots[i].setParams (d.type, d.a, d.b, d.c, d.mix, d.bypass, L.blk.quality);
            const auto& e = R.blk.slot[i];
            R.slots[i].setParams (e.type, e.a, e.b, e.c, e.mix, e.bypass, R.blk.quality);
            if (L.slots[i].isDynamics() && R.slots[i].isDynamics())
            {
                for (int k = 0; k < n; ++k)
                {
                    const float a = std::abs (l[k]), b = std::abs (r[k]);
                    L.det[(size_t) k] = std::max (std::isfinite (a) ? a : 0.0f, std::isfinite (b) ? b : 0.0f);
                }
                L.slots[i].process (l, n, L.det.data());
                R.slots[i].process (r, n, L.det.data());
            }
            else
            {
                L.slots[i].process (l, n);
                R.slots[i].process (r, n);
            }
        }
        l += n; r += n; num -= n;
    }
}

void FxChain::processMasterPair (FxChain& L, FxChain& R, float* l, float* r, int num)
{
    if (num <= 0) return;
    L.eqBlock (l, num);
    R.eqBlock (r, num);

    const FxSettings& s = L.blk;
    if (! s.compBypass)
    {
        auto& comp = L.comp;
        if (! L.compWasOn) comp.gr = 0;
        comp.thr = s.compThr; comp.ratio = s.compRatio; comp.knee = 6.0f;
        comp.setTimes (s.compAtt, s.compRel);
        const float mk = fxdsp::dbToLin (s.compMakeup);
        for (int i = 0; i < num; ++i)
        {
            const float dl = l[i], dr = r[i];
            const float gain = comp.process (std::max (std::abs (dl), std::abs (dr))) * mk;
            l[i] = dl + s.compMix * (dl * gain - dl);
            r[i] = dr + s.compMix * (dr * gain - dr);
        }
    }
    L.compWasOn = ! s.compBypass;

    L.outGain.setTargetValue (fxdsp::dbToLin (s.outDb));
    L.limInGain.setTargetValue (fxdsp::dbToLin (s.limIn));
    L.limiter.setLookahead (Limiter::lookaheadSamples (s.limLook, L.sr));
    L.limiter.setRelease (s.limRel);
    const float ceil = fxdsp::dbToLin (s.limCeil);
    for (int i = 0; i < num; ++i)
    {
        const float og = L.outGain.getNextValue(), ig = L.limInGain.getNextValue();
        float a = l[i] * og, b = r[i] * og;
        if (! std::isfinite (a)) a = 0.0f;
        if (! std::isfinite (b)) b = 0.0f;
        L.limiter.process2 (a, b, ig, ceil, s.limSat, s.limBypass);
        l[i] = a; r[i] = b;
    }
}

void FxChain::processMaster (float* x, int num)
{
    if (num <= 0) return;
    const FxSettings& s = blk;

    eqBlock (x, num);

    // master compressor
    if (! s.compBypass)
    {
        if (! compWasOn) comp.gr = 0;
        comp.thr = s.compThr; comp.ratio = s.compRatio; comp.knee = 6.0f;
        comp.setTimes (s.compAtt, s.compRel);
        const float mk = fxdsp::dbToLin (s.compMakeup);
        for (int i = 0; i < num; ++i)
        {
            const float d = x[i];
            x[i] = d + s.compMix * (d * comp.process (d) * mk - d);
        }
    }
    compWasOn = ! s.compBypass;

    // output gain (F-26) -> limiter (F-22..24)
    outGain.setTargetValue (fxdsp::dbToLin (s.outDb));
    limInGain.setTargetValue (fxdsp::dbToLin (s.limIn));
    limiter.setLookahead (Limiter::lookaheadSamples (s.limLook, sr));
    limiter.setRelease (s.limRel);
    for (int i = 0; i < num; ++i)
    {
        float v = x[i] * outGain.getNextValue();
        if (! std::isfinite (v)) v = 0.0f;
        x[i] = limiter.process (v, limInGain.getNextValue(), fxdsp::dbToLin (s.limCeil), s.limSat, s.limBypass);
    }
}
}
