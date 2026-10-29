#include "SubVoice.h"
#include <algorithm>
#include <cmath>

namespace kf
{
void SubVoice::prepare (double sampleRate, int) { sr = sampleRate; active = false; }

void SubVoice::noteOn (int, float velocity)
{
    vel = velocity;
    phase = 0.0;
    time = 0.0;
    active = true;
}

void SubVoice::noteOff() {} // one-shot kick: ignores note-off

void SubVoice::process (juce::AudioBuffer<float>& buffer, int start, int num)
{
    if (! active) return;
    auto* out = buffer.getWritePointer (0) + start;
    constexpr double fStart = 200.0, fEnd = 50.0, pitchTau = 0.03, decay = 0.4;
    const double dt = 1.0 / sr;
    const double ampK = std::log (1000.0) / decay; // -60 dB at 400 ms
    for (int i = 0; i < num; ++i)
    {
        const double f = fEnd + (fStart - fEnd) * std::exp (-time / pitchTau);
        phase += f * dt;
        phase -= std::floor (phase);
        const double attack = std::min (1.0, time / 0.001); // 1 ms declick
        const double a = std::exp (-ampK * time) * attack;
        out[i] += (float) (std::sin (juce::MathConstants<double>::twoPi * phase) * a) * vel;
        time += dt;
        if (time > decay * 1.5) { active = false; break; }
    }
}
}
