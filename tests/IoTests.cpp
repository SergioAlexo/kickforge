#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <chrono>
#include <cmath>
#include "PluginProcessor.h"
#include "trigger/Trigger.h"

// Sidechain trigger (G-06..G-10), MIDI out (G-11), stem outputs (G-13), CPU report.
namespace
{
constexpr double SR = 48000.0;
constexpr int BLOCK = 256;

void setP (KickforgeProcessor& p, const juce::String& id, float plain)
{
    auto* prm = p.apvts.getParameter (id);
    REQUIRE (prm != nullptr);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plain));
}

struct Proc
{
    juce::ScopedJuceInitialiser_GUI init;
    KickforgeProcessor p;
    Proc()
    {
        p.setPlayConfigDetails (0, 2, SR, BLOCK);
        p.prepareToPlay (SR, BLOCK);
    }
};
}

TEST_CASE ("Trigger: detector fires on bursts above threshold with hysteresis and returns")
{
    kf::TriggerDetector det;
    det.prepare (SR);
    kf::TriggerSettings s;
    s.enable = true;
    s.threshDb = -24.0f;
    std::vector<float> x (48000, 0.0f);
    auto burst = [&] (int at, float amp)
    {
        for (int i = 0; i < 4800; ++i)
            x[(size_t) (at + i)] = amp * std::sin (2.0f * 3.14159265f * 100.0f * (float) i / (float) SR);
    };
    burst (4800, 0.5f);
    burst (20000, 0.005f); // -46 dB: below the threshold, ignored
    burst (30000, 0.5f);
    std::array<kf::TriggerDetector::Event, 64> ev;
    const int n = det.process (s, x.data(), nullptr, (int) x.size(), ev.data(), 64);
    std::vector<kf::TriggerDetector::Event> ons, offs;
    for (int i = 0; i < n; ++i) (ev[(size_t) i].on ? ons : offs).push_back (ev[(size_t) i]);
    REQUIRE (ons.size() == 2);
    REQUIRE (offs.size() == 2);
    REQUIRE (ons[0].pos >= 4800);
    REQUIRE (ons[0].pos < 4800 + 600);
    REQUIRE (ons[1].pos >= 30000);
    REQUIRE (ons[1].pos < 30000 + 600);
    REQUIRE (offs[0].pos > ons[0].pos);
    REQUIRE (offs[0].pos < ons[1].pos);
    REQUIRE (ons[0].note == 36);

    kf::TriggerDetector det2;
    det2.prepare (SR);
    s.threshDb = -3.0f; // above the signal: nothing
    REQUIRE (det2.process (s, x.data(), nullptr, (int) x.size(), ev.data(), 64) == 0);

    kf::TriggerDetector det3;
    det3.prepare (SR);
    s.threshDb = -24.0f;
    s.hpHz = 2000.0f; // input filter removes the 100 Hz burst
    REQUIRE (det3.process (s, x.data(), nullptr, (int) x.size(), ev.data(), 64) == 0);
}

TEST_CASE ("Engine: sidechain trigger plays the kick; MIDI out mirrors triggers")
{
    Proc t;
    auto lay = t.p.getBusesLayout();
    lay.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    REQUIRE (t.p.setBusesLayout (lay));
    t.p.prepareToPlay (SR, BLOCK);
    setP (t.p, "trig_enable", 1);
    setP (t.p, "midiout_enable", 1);
    setP (t.p, "midiout_note", 40);
    setP (t.p, "midiout_len", 50);

    float quiet = 0, loud = 0;
    int noteOns = 0, noteOffs = 0, onNote = -1;
    for (int b = 0; b < 60; ++b)
    {
        juce::AudioBuffer<float> buf (2, BLOCK);
        buf.clear();
        if (b == 10) // a loud sidechain burst
            for (int i = 0; i < BLOCK; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    buf.setSample (ch, i, 0.6f * std::sin (2.0f * 3.14159265f * 100.0f * (float) i / (float) SR));
        juce::MidiBuffer midi;
        t.p.processBlock (buf, midi);
        for (const auto m : midi)
        {
            if (m.getMessage().isNoteOn()) { ++noteOns; onNote = m.getMessage().getNoteNumber(); }
            if (m.getMessage().isNoteOff()) ++noteOffs;
        }
        const float pk = buf.getMagnitude (0, BLOCK);
        if (b < 10) quiet = std::max (quiet, pk); else loud = std::max (loud, pk);
    }
    REQUIRE (quiet == 0.0f);
    REQUIRE (loud > 0.1f);
    REQUIRE (noteOns == 1);
    REQUIRE (onNote == 40);
    REQUIRE (noteOffs == 1);
}

TEST_CASE ("Engine: optional Sub and Click stem outputs carry the pre-FX layers")
{
    Proc t;
    auto lay = t.p.getBusesLayout();
    lay.outputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    lay.outputBuses.getReference (2) = juce::AudioChannelSet::stereo();
    REQUIRE (t.p.setBusesLayout (lay));
    t.p.prepareToPlay (SR, BLOCK);
    double main = 0, sub = 0, clk = 0;
    for (int b = 0; b < 40; ++b)
    {
        juce::AudioBuffer<float> buf (6, BLOCK);
        buf.clear();
        juce::MidiBuffer midi;
        if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110), 10);
        t.p.processBlock (buf, midi);
        main += buf.getRMSLevel (0, 0, BLOCK);
        sub += buf.getRMSLevel (2, 0, BLOCK);
        clk += buf.getRMSLevel (4, 0, BLOCK);
        REQUIRE (buf.getRMSLevel (3, 0, BLOCK) == Catch::Approx (buf.getRMSLevel (2, 0, BLOCK)).margin (1.0e-6));
    }
    REQUIRE (main > 0.01);
    REQUIRE (sub > 0.01);
    REQUIRE (clk > 0.001);
}

TEST_CASE ("Engine: CPU cost at 4x oversampling with every slot active (report)")
{
    Proc t;
    setP (t.p, "fx_quality", 2);
    const float types[4] = { 1, 2, 3, 7 }; // Clipper, Tube, Wave, Complex
    const char* slots[4] = { "a1", "a2", "b1", "b2" };
    for (int i = 0; i < 4; ++i)
        setP (t.p, juce::String ("fx_") + slots[i] + "_type", types[i]);
    for (int l = 1; l < 4; ++l) setP (t.p, "click_" + juce::String (l + 1) + "_source", 3.0f);
    setP (t.p, "sub_drive", 0.5f);
    for (int h = 1; h <= 9; ++h) setP (t.p, "sub_h" + juce::String (h) + "_gain", -12.0f);
    setP (t.p, "fx_eq_byp", 0);
    setP (t.p, "fx_comp_byp", 0);
    setP (t.p, "sub_bus", 1);
    setP (t.p, "click_1_bus", 2);
    const int blocks = (int) (SR * 4.0) / BLOCK;
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buf (2, BLOCK);
        juce::MidiBuffer midi;
        if (b % 40 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110), 0);
        t.p.processBlock (buf, midi);
    }
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    const double msPerSec = sec * 1000.0 / 4.0;
    WARN ("CPU: " << msPerSec << " ms of processing per second of audio (4x OS, all slots active, retrigger every 0.2 s)");
    REQUIRE (msPerSec < 500.0);
}
