// Factory presets: authored in code by setting parameters / curves on a scratch processor,
// then captured as full-state XML. All original to Kickforge.
#include "PresetManager.h"
#include "PluginProcessor.h"
#include "fx/FxParams.h"
#include <initializer_list>
#include <utility>

namespace kf
{
namespace
{
using Pts = std::vector<Curve::Point>;
using Kv = std::pair<const char*, float>;

struct Builder
{
    KickforgeProcessor p;

    void set (std::initializer_list<Kv> kv)
    {
        for (auto& e : kv)
        {
            auto* par = p.apvts.getParameter (e.first);
            jassert (par != nullptr);
            if (par != nullptr) par->setValueNotifyingHost (par->convertTo0to1 (e.second));
        }
    }
    void curve (SubCurveId id, Pts pts) { p.getSubCurves().setCurve (id, Curve (std::move (pts))); }

    // Sub shorthand. root: MIDI note of the base pitch (key tracking on, play that note).
    void sub (int root, float lengthMs, Pts pitch, Pts amp, int wave = 0, float gainDb = 0.0f)
    {
        set ({ { "sub_root", (float) root }, { "sub_length", lengthMs }, { "sub_wave", (float) wave },
               { "sub_gain", gainDb } });
        curve (SubCurveId::Pitch, std::move (pitch));
        curve (SubCurveId::Amp, std::move (amp));
    }
    // Click layer 1 (layer index 1..4): source 0 off,2 noise,3 impulse,4 chirp,5 beater
    void click (int layer, int source, float toneHz, float decayMs, float gainDb, float color = 0.5f)
    {
        const auto n = juce::String (layer);
        auto setP = [&] (const char* suffix, float v) {
            auto* par = p.apvts.getParameter ("click_" + n + "_" + suffix);
            if (par != nullptr) par->setValueNotifyingHost (par->convertTo0to1 (v));
        };
        setP ("source", (float) source);
        setP ("tone", toneHz);
        setP ("decay", decayMs);
        setP ("gain", gainDb);
        setP ("color", color);
    }
    // FX slot 0..3 (a1 a2 b1 b2); type index per fxTypeNames
    void fx (int slot, int type, float a, float b, float c, float mix = 1.0f)
    {
        const juce::String id = juce::String ("fx_") + kFxSlotNames[slot];
        auto setP = [&] (const char* suffix, float v) {
            auto* par = p.apvts.getParameter (id + "_" + suffix);
            if (par != nullptr) par->setValueNotifyingHost (par->convertTo0to1 (v));
        };
        setP ("type", (float) type);
        setP ("a", a); setP ("b", b); setP ("c", c); setP ("mix", mix);
    }
};

enum Fx { Off, Clipper, Tube, Wave, Tape, Drive, BitCrush, Complex, Brassify, RingMod, Delay, Reverb, Filter, Eq };
enum Src { sOff = 0, sSample, sNoise, sImpulse, sChirp, sBeater };

struct Def
{
    const char* name;
    const char* category;
    void (*build) (Builder&);
};

const Def kDefs[] = {
    // ---------------- Techno
    { "Warehouse Rumble", "Techno", [] (Builder& b) {
        b.sub (31, 650, { { 0, 18, -0.3 }, { 0.05, 5, -0.4 }, { 0.25, 0, 0 }, { 1, 0, 0 } },
               { { 0, 1, 0.3 }, { 0.35, 0.7, -0.5 }, { 1, 0, 0 } });
        b.click (1, sBeater, 3000, 8, -12);
        b.fx (0, Tube, 0.35f, 0.5f, 0.5f, 0.6f); } },
    { "Peak Time Punch", "Techno", [] (Builder& b) {
        b.sub (33, 320, { { 0, 30, -0.4 }, { 0.06, 6, -0.4 }, { 0.2, 0, 0 }, { 1, 0, 0 } },
               { { 0, 1, 0.3 }, { 0.4, 0.55, -0.5 }, { 1, 0, 0 } });
        b.click (1, sBeater, 3500, 6, -9);
        b.fx (0, Clipper, 0.4f, 0.5f, 0.8f); } },
    { "Industrial Thud", "Techno", [] (Builder& b) {
        b.sub (32, 420, { { 0, 22, -0.3 }, { 0.08, 4, -0.3 }, { 0.3, 0, 0 }, { 1, 0, 0 } },
               { { 0, 1, 0.3 }, { 0.3, 0.7, -0.6 }, { 1, 0, 0 } }, 4);
        b.click (1, sNoise, 2200, 12, -10, 0.6f);
        b.fx (0, Drive, 0.6f, 0.5f, 0.5f, 0.8f);
        b.fx (1, Clipper, 0.5f, 0.5f, 0.8f); } },
    // ---------------- House
    { "Classic House", "House", [] (Builder& b) {
        b.sub (34, 380, { { 0, 14, -0.3 }, { 0.08, 4, -0.4 }, { 0.3, 0, 0 }, { 1, 0, 0 } },
               { { 0, 1, 0.3 }, { 0.35, 0.65, -0.5 }, { 1, 0, 0 } });
        b.click (1, sBeater, 2800, 8, -12); } },
    { "Deep House Round", "House", [] (Builder& b) {
        b.sub (33, 480, { { 0, 10, -0.4 }, { 0.1, 2, -0.4 }, { 0.35, 0, 0 }, { 1, 0, 0 } },
               { { 0, 0.9, 0.2 }, { 0.3, 0.7, -0.5 }, { 1, 0, 0 } });
        b.click (1, sOff, 2000, 10, -18); } },
    { "Disco Thump", "House", [] (Builder& b) {
        b.sub (36, 300, { { 0, 20, -0.4 }, { 0.07, 5, -0.4 }, { 0.25, 0, 0 }, { 1, 0, 0 } },
               { { 0, 1, 0.3 }, { 0.4, 0.5, -0.5 }, { 1, 0, 0 } });
        b.click (1, sBeater, 4000, 6, -10);
        b.fx (0, Tape, 0.45f, 0.5f, 0.5f, 0.7f); } },
    // ---------------- 808 / Trap
    { "808 Long Sub", "808", [] (Builder& b) {
        b.sub (29, 2200, { { 0, 12, -0.5 }, { 0.02, 0, -0.2 }, { 1, 0, 0 } },
               { { 0, 1, 0.1 }, { 0.6, 0.8, -0.3 }, { 1, 0, 0 } });
        b.click (1, sOff, 2000, 10, -18);
        b.fx (0, Tube, 0.3f, 0.4f, 0.5f, 0.6f); } },
    { "808 Boom", "808", [] (Builder& b) {
        b.sub (28, 3000, { { 0, 7, -0.5 }, { 0.015, 0, -0.2 }, { 1, 0, 0 } },
               { { 0, 1, 0.1 }, { 0.5, 0.75, -0.4 }, { 1, 0, 0 } });
        b.click (1, sImpulse, 1500, 5, -16);
        b.fx (0, Tube, 0.4f, 0.4f, 0.5f, 0.7f); } },
    { "808 Dirty", "808", [] (Builder& b) {
        b.sub (31, 1500, { { 0, 9, -0.5 }, { 0.02, 0, -0.2 }, { 1, 0, 0 } },
               { { 0, 1, 0.1 }, { 0.5, 0.7, -0.4 }, { 1, 0, 0 } }, 1);
        b.click (1, sNoise, 1800, 6, -14);
        b.fx (0, Clipper, 0.6f, 0.5f, 0.8f); } },
    // ---------------- Hardstyle
    { "Hardstyle Screech", "Hardstyle", [] (Builder& b) {
        b.sub (36, 700, { { 0, 36, -0.2 }, { 0.12, 12, -0.4 }, { 0.35, 0, 0 }, { 1, 0, 0 } },
               { { 0, 1, 0.2 }, { 0.5, 0.8, -0.4 }, { 1, 0, 0 } });
        b.click (1, sChirp, 4000, 14, -9);
        b.fx (0, Clipper, 0.85f, 0.5f, 0.7f);
        b.fx (1, Wave, 0.5f, 0.6f, 0.5f, 0.6f); } },
    { "Rawstyle Kick", "Hardstyle", [] (Builder& b) {
        b.sub (35, 600, { { 0, 32, -0.3 }, { 0.1, 9, -0.4 }, { 0.3, 0, 0 }, { 1, 0, 0 } },
               { { 0, 1, 0.2 }, { 0.5, 0.8, -0.4 }, { 1, 0, 0 } });
        b.click (1, sNoise, 3000, 10, -10, 0.6f);
        b.fx (0, Clipper, 0.8f, 0.5f, 0.7f);
        b.fx (1, Brassify, 0.5f, 0.5f, 0.5f, 0.5f); } },
    { "Gabber Crunch", "Hardstyle", [] (Builder& b) {
        b.sub (38, 450, { { 0, 34, -0.3 }, { 0.1, 8, -0.4 }, { 0.3, 0, 0 }, { 1, 0, 0 } },
               { { 0, 1, 0.2 }, { 0.5, 0.85, -0.4 }, { 1, 0, 0 } }, 2);
        b.click (1, sBeater, 3500, 8, -10);
        b.fx (0, BitCrush, 0.6f, 0.2f, 0.5f, 0.6f);
        b.fx (1, Clipper, 0.9f, 0.5f, 0.7f); } },
    // ---------------- Psy
    { "Psy Bass Kick", "Psy", [] (Builder& b) {
        b.sub (34, 350, { { 0, 24, -0.4 }, { 0.06, 5, -0.4 }, { 0.2, 0, 0 }, { 1, 0, 0 } },
               { { 0, 1, 0.3 }, { 0.35, 0.6, -0.5 }, { 1, 0, 0 } });
        b.click (1, sBeater, 3000, 6, -12);
        b.fx (0, Tube, 0.3f, 0.5f, 0.5f, 0.6f); } },
    { "Full-On Punch", "Psy", [] (Builder& b) {
        b.sub (35, 300, { { 0, 26, -0.4 }, { 0.05, 6, -0.4 }, { 0.2, 0, 0 }, { 1, 0, 0 } },
               { { 0, 1, 0.3 }, { 0.4, 0.55, -0.5 }, { 1, 0, 0 } });
        b.click (1, sChirp, 5000, 9, -10);
        b.fx (0, Clipper, 0.35f, 0.5f, 0.8f); } },
    // ---------------- D&B
    { "Punchy DnB", "DnB", [] (Builder& b) {
        b.sub (38, 180, { { 0, 24, -0.5 }, { 0.08, 4, -0.4 }, { 0.3, 0, 0 }, { 1, 0, 0 } },
               { { 0, 1, 0.3 }, { 0.4, 0.5, -0.5 }, { 1, 0, 0 } });
        b.click (1, sImpulse, 3500, 5, -9);
        b.fx (0, Clipper, 0.3f, 0.5f, 0.8f); } },
    { "Neuro Tight", "DnB", [] (Builder& b) {
        b.sub (40, 140, { { 0, 20, -0.5 }, { 0.1, 3, -0.4 }, { 0.3, 0, 0 }, { 1, 0, 0 } },
               { { 0, 1, 0.3 }, { 0.5, 0.4, -0.5 }, { 1, 0, 0 } }, 4);
        b.click (1, sNoise, 5000, 4, -9, 0.7f);
        b.fx (0, Drive, 0.5f, 0.6f, 0.5f, 0.6f); } },
    // ---------------- Lo-Fi
    { "Dusty Tape Kick", "Lo-Fi", [] (Builder& b) {
        b.sub (33, 350, { { 0, 12, -0.4 }, { 0.1, 3, -0.4 }, { 0.3, 0, 0 }, { 1, 0, 0 } },
               { { 0, 0.95, 0.2 }, { 0.3, 0.6, -0.5 }, { 1, 0, 0 } });
        b.click (1, sNoise, 1500, 10, -14, 0.4f);
        b.fx (0, Tape, 0.6f, 0.35f, 0.5f);
        b.fx (1, Filter, 0.55f, 0.2f, 0.0f); } },
    { "Bit Boom Bap", "Lo-Fi", [] (Builder& b) {
        b.sub (36, 300, { { 0, 14, -0.4 }, { 0.1, 3, -0.4 }, { 0.3, 0, 0 }, { 1, 0, 0 } },
               { { 0, 1, 0.3 }, { 0.35, 0.55, -0.5 }, { 1, 0, 0 } });
        b.click (1, sBeater, 2000, 8, -12);
        b.fx (0, BitCrush, 0.5f, 0.3f, 0.5f, 0.5f); } },
    // ---------------- Cinematic / FX
    { "Cinematic Impact", "Cinematic", [] (Builder& b) {
        b.sub (28, 3500, { { 0, 14, -0.4 }, { 0.03, 2, -0.3 }, { 0.2, 0, 0 }, { 1, 0, 0 } },
               { { 0, 1, 0.1 }, { 0.5, 0.6, -0.4 }, { 1, 0, 0 } });
        b.click (1, sNoise, 800, 40, -12, 0.3f);
        b.fx (0, Reverb, 0.8f, 0.5f, 0.8f, 0.3f); } },
    { "Ring Zap", "Experimental", [] (Builder& b) {
        b.sub (36, 400, { { 0, 36, -0.4 }, { 0.15, 7, -0.4 }, { 0.4, 0, 0 }, { 1, 0, 0 } },
               { { 0, 1, 0.3 }, { 0.4, 0.5, -0.5 }, { 1, 0, 0 } });
        b.click (1, sChirp, 6000, 20, -10);
        b.fx (0, RingMod, 0.3f, 0.4f, 0.3f, 0.35f); } },
    // ---------------- Utility
    { "Clean Sub", "Utility", [] (Builder& b) {
        b.sub (33, 500, { { 0, 12, -0.4 }, { 0.08, 0, -0.3 }, { 1, 0, 0 } },
               { { 0, 1, 0.2 }, { 0.4, 0.7, -0.5 }, { 1, 0, 0 } });
        b.click (1, sOff, 2000, 10, -18); } },
    { "Click Only", "Utility", [] (Builder& b) {
        b.sub (33, 100, { { 0, 0, 0 }, { 1, 0, 0 } }, { { 0, 0, 0 }, { 1, 0, 0 } }, 0, -60.0f);
        b.set ({ { "sub_mute", 1.0f } });
        b.click (1, sBeater, 3000, 10, -3); } },
};
}

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> list = [] {
        std::vector<FactoryPreset> v;
        for (auto& d : kDefs)
        {
            Builder b;
            d.build (b);
            FactoryPreset f;
            f.name = d.name;
            f.category = d.category;
            auto st = b.p.apvts.copyState();
            st.removeChild (st.getChildWithName ("click"), nullptr);
            st.appendChild (b.p.getClickLayer().saveState(), nullptr);
            f.state = st.createXml();
            v.push_back (std::move (f));
        }
        return v;
    }();
    return list;
}
}
