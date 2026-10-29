#include "Views.h"
#include "EqResponse.h"

namespace kf::ui
{
void WaveformView::paint (juce::Graphics& g)
{
    using namespace col;
    auto b = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (b, 6.0f);
    auto r = b.reduced (6.0f, 18.0f);
    g.setColour (bg);
    g.fillRect (r);
    g.setColour (line);
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    // ---- reference (AI link), shown dimmed; onset = first sample above the analyser's capture threshold
    double refRate = 0.0;
    std::shared_ptr<const std::vector<float>> ref;
    float refGain = 1.0f;
    if (reference != nullptr && reference->hasAudio())
    {
        ref = reference->getDisplay (refRate);
        refGain = juce::Decibels::decibelsToGain (reference->getInfo().gainDb, -60.0f);
    }
    if (ref != nullptr && ref != refCache)
    {
        refCache = ref;
        refOnset = 0;
        refPeak = 0.0f;
        for (size_t i = 0; i < ref->size(); ++i)
        {
            const float a = std::abs ((*ref)[i]);
            if (refPeak == 0.0f && a > 0.02f) refOnset = (int) i;
            refPeak = std::max (refPeak, a);
        }
    }
    else if (ref == nullptr)
        refCache.reset();

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.setColour (dim);
    const auto& k = analyser.getKick();
    const bool haveRef = ref != nullptr && refRate > 0.0 && refPeak > 0.0f;
    if (k.empty() && ! haveRef)
    {
        g.drawText ("OUTPUT  -  play a note or press Audition", getLocalBounds().removeFromTop (18).reduced (8, 0),
                    juce::Justification::centredLeft);
        return;
    }
    float peak = 0.0f;
    for (float s : k) peak = std::max (peak, std::abs (s));
    const double sr = analyser.getSampleRate();
    double ms = k.empty() ? 0.0 : 1000.0 * (double) k.size() / sr;
    if (! k.empty())
        g.drawText ("OUTPUT   peak " + juce::String (juce::Decibels::gainToDecibels (peak, -99.0f), 1) + " dBFS   " +
                        juce::String ((int) ms) + " ms",
                    getLocalBounds().removeFromTop (18).reduced (8, 0), juce::Justification::centredLeft);
    else
        g.drawText ("OUTPUT  -  play a note to compare", getLocalBounds().removeFromTop (18).reduced (8, 0),
                    juce::Justification::centredLeft);

    const int w = (int) r.getWidth();
    // time span shown: the kick; with no kick yet, the reference
    const double spanSec = k.empty() ? std::min (1.5, (double) (ref->size() - (size_t) refOnset) / refRate)
                                     : (double) k.size() / sr;
    float refVisPeak = 0.0f;
    if (haveRef)
    {
        const size_t lim = std::min (ref->size(), (size_t) refOnset + (size_t) (spanSec * refRate) + 1);
        for (size_t i = (size_t) refOnset; i < lim; ++i) refVisPeak = std::max (refVisPeak, std::abs ((*ref)[i]) * refGain);
    }
    const float scale = 1.0f / std::max (0.25f, std::max (peak, refVisPeak));

    if (haveRef)
    {
        juce::Path rp;
        const double perRef = spanSec * refRate / (double) w;
        for (int x = 0; x < w; ++x)
        {
            const size_t a = (size_t) refOnset + (size_t) (x * perRef);
            const size_t e = std::min (ref->size(), (size_t) refOnset + (size_t) ((x + 1) * perRef) + 1);
            if (a >= ref->size()) break;
            float lo = 0, hi = 0;
            for (size_t i = a; i < e; ++i) { lo = std::min (lo, (*ref)[i] * refGain); hi = std::max (hi, (*ref)[i] * refGain); }
            const float px = r.getX() + (float) x;
            rp.addLineSegment (juce::Line<float> (px, r.getCentreY() - hi * scale * r.getHeight() * 0.5f,
                                                  px, r.getCentreY() - lo * scale * r.getHeight() * 0.5f - 0.5f), 0.5f);
        }
        g.setColour (accent.withAlpha (0.45f));
        g.fillPath (rp);
        g.setColour (accent.withAlpha (0.8f));
        g.drawText ("REF", getLocalBounds().removeFromTop (18).reduced (8, 0), juce::Justification::centredRight);
    }

    if (! k.empty())
    {
        juce::Path p;
        const double per = (double) k.size() / w;
        for (int x = 0; x < w; ++x)
        {
            const size_t a = (size_t) (x * per), e = std::min (k.size(), (size_t) ((x + 1) * per) + 1);
            float lo = 0, hi = 0;
            for (size_t i = a; i < e; ++i) { lo = std::min (lo, k[i]); hi = std::max (hi, k[i]); }
            const float px = r.getX() + (float) x;
            p.addLineSegment (juce::Line<float> (px, r.getCentreY() - hi * scale * r.getHeight() * 0.5f,
                                                 px, r.getCentreY() - lo * scale * r.getHeight() * 0.5f - 0.5f), 0.5f);
        }
        g.setColour (accent2);
        g.fillPath (p);
    }
}
void SpectrumView::paint (juce::Graphics& g)
{
    using namespace col;
    auto b = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (b, 6.0f);
    const auto r = b.reduced (6.0f, 18.0f).withTrimmedBottom (12.0f);
    g.setColour (bg);
    g.fillRect (r);

    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = freqToX (f, r.getX(), r.getWidth());
        g.setColour (line.withAlpha (0.6f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (dim);
        g.drawText (f >= 1000 ? juce::String (f / 1000.0, 0) + "k" : juce::String ((int) f), (int) x - 16, (int) r.getBottom() + 1, 32, 11,
                    juce::Justification::centred);
    }
    constexpr float topDb = 0.0f, botDb = -90.0f;
    auto dbToY = [&] (float db) { return r.getBottom() - (std::clamp (db, botDb, topDb) - botDb) / (topDb - botDb) * r.getHeight(); };
    for (float db = -80.0f; db <= -10.0f; db += 20.0f)
    {
        g.setColour (line.withAlpha (0.6f));
        g.drawHorizontalLine ((int) dbToY (db), r.getX(), r.getRight());
        g.setColour (dim);
        g.drawText (juce::String ((int) db), (int) b.getX() + 1, (int) dbToY (db) - 5, 24, 10, juce::Justification::centredLeft);
    }
    g.setColour (dim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText (eq != nullptr ? "SPECTRUM  +  EQ RESPONSE" : "SPECTRUM", getLocalBounds().removeFromTop (18).reduced (8, 0),
                juce::Justification::centredLeft);

    const auto& sp = analyser.getSpectrumDb();
    const double binHz = analyser.binToHz (1.0);
    juce::Path fill, stroke;
    bool first = true;
    const int w = (int) r.getWidth();
    for (int x = 0; x < w; ++x)
    {
        const double b0 = xToFreq (r.getX() + (float) x, r.getX(), r.getWidth()) / binHz;
        const double b1 = xToFreq (r.getX() + (float) x + 1.0f, r.getX(), r.getWidth()) / binHz;
        float db;
        if (b1 - b0 < 1.0)
        {
            const double bm = 0.5 * (b0 + b1);
            const int i = std::clamp ((int) bm, 0, Analyser::numBins - 2);
            const float fr = (float) (bm - i);
            db = sp[(size_t) i] * (1 - fr) + sp[(size_t) i + 1] * fr;
        }
        else
        {
            db = Analyser::floorDb;
            for (int i = std::max (0, (int) b0); i <= std::min (Analyser::numBins - 1, (int) b1); ++i)
                db = std::max (db, sp[(size_t) i]);
        }
        const float px = r.getX() + (float) x, py = dbToY (db);
        if (first) { stroke.startNewSubPath (px, py); fill.startNewSubPath (px, r.getBottom()); first = false; }
        else stroke.lineTo (px, py);
        fill.lineTo (px, py);
    }
    fill.lineTo (r.getRight(), r.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (accent2.withAlpha (0.35f), 0, r.getY(), accent2.withAlpha (0.02f), 0, r.getBottom(), false));
    g.fillPath (fill);
    g.setColour (accent2);
    g.strokePath (stroke, juce::PathStrokeType (1.4f));

    if (eq != nullptr)
    {
        auto v = [this] (const char* id) { return (double) eq->getRawParameterValue (id)->load(); };
        EqSettings s { v ("fx_eq_lc"), v ("fx_eq_ls_f"), v ("fx_eq_ls_g"), v ("fx_eq_p1_f"), v ("fx_eq_p1_g"), v ("fx_eq_p1_q"),
                       v ("fx_eq_p2_f"), v ("fx_eq_p2_g"), v ("fx_eq_p2_q"), v ("fx_eq_hs_f"), v ("fx_eq_hs_g") };
        const bool byp = v ("fx_eq_byp") > 0.5;
        juce::Path rp;
        for (int x = 0; x < w; ++x)
        {
            const double f = xToFreq (r.getX() + (float) x, r.getX(), r.getWidth());
            const float db = (float) std::clamp (eqResponseDb (s, f, analyser.getSampleRate()), -24.0, 24.0);
            const float py = r.getCentreY() - db / 24.0f * r.getHeight() * 0.5f;
            if (x == 0) rp.startNewSubPath (r.getX(), py); else rp.lineTo (r.getX() + (float) x, py);
        }
        g.setColour (accent.withAlpha (byp ? 0.35f : 1.0f));
        g.strokePath (rp, juce::PathStrokeType (2.0f));
        g.setColour (dim);
        g.drawText (byp ? "EQ bypassed (response +/-24 dB scale)" : "response +/-24 dB scale", getLocalBounds().removeFromTop (18).reduced (8, 0),
                    juce::Justification::centredRight);
    }
}
} // namespace kf::ui
