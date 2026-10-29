#include "KickMetrics.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdarg>
#include <cstdio>
#include <limits>

namespace kf::analysis
{
const std::array<double, kNumTimes> kTimesMs = { 0, 2, 5, 10, 20, 35, 50, 75, 100, 150, 200, 300, 400, 600, 800 };
const std::array<double, kNumBands> kBandCentersHz = { 31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

namespace
{
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kPi = 3.14159265358979323846;

std::string fmt (const char* f, ...)
{
    char buf[512];
    va_list a;
    va_start (a, f);
    std::vsnprintf (buf, sizeof buf, f, a);
    va_end (a);
    return buf;
}

double toDb (double lin) { return 20.0 * std::log10 (std::max (lin, 1.0e-9)); }

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    void lowpass (double fc, double q, double sr)
    {
        const double w = 2.0 * kPi * fc / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + al;
        b0 = (1.0 - c) * 0.5 / a0; b1 = (1.0 - c) / a0; b2 = b0;
        a1 = -2.0 * c / a0; a2 = (1.0 - al) / a0;
    }
    void run (std::vector<float>& v) const
    {
        double z1 = 0, z2 = 0;
        for (auto& s : v)
        {
            const double x = s, y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            s = (float) y;
        }
    }
    void runZeroPhase (std::vector<float>& v) const
    {
        run (v);
        std::reverse (v.begin(), v.end());
        run (v);
        std::reverse (v.begin(), v.end());
    }
};

using cd = std::complex<double>;
void fft (std::vector<cd>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / (double) len;
        const cd wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            cd w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/** Hann-windowed power spectrum of x[start..start+len); returns one-sided |X|^2, sets nfft and sumW2. */
std::vector<double> powerSpectrum (const float* x, int n, int start, int len, size_t& nfft, double& sumW2)
{
    len = std::max (0, std::min (len, n - start));
    nfft = 1024;
    while ((int) nfft < len) nfft <<= 1;
    std::vector<cd> a (nfft, cd (0, 0));
    sumW2 = 1.0e-12;
    for (int i = 0; i < len; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * (i + 0.5) / (double) len);
        a[(size_t) i] = x[start + i] * w;
        sumW2 += w * w;
    }
    fft (a);
    std::vector<double> p (nfft / 2 + 1);
    for (size_t k = 0; k < p.size(); ++k) p[k] = std::norm (a[k]);
    return p;
}

void bandDb (const std::vector<double>& p, size_t nfft, double sumW2, double sr, std::array<double, kNumBands>& out)
{
    for (int b = 0; b < kNumBands; ++b)
    {
        const double lo = kBandCentersHz[(size_t) b] / std::sqrt (2.0), hi = kBandCentersHz[(size_t) b] * std::sqrt (2.0);
        double e = 0.0;
        for (size_t k = 1; k < p.size(); ++k)
        {
            const double f = (double) k * sr / (double) nfft;
            if (f >= lo && f < hi) e += 2.0 * p[k];
        }
        e /= (double) nfft * sumW2;
        out[(size_t) b] = 10.0 * std::log10 (e + 1.0e-12);
    }
}
} // namespace

// ------------------------------------------------------------------ helpers
int findOnset (const float* x, int n)
{
    float pk = 0.0f;
    for (int i = 0; i < n; ++i) pk = std::max (pk, std::abs (x[i]));
    if (pk <= 0.0f) return 0;
    const float thr = 0.03f * pk;
    for (int i = 0; i < n; ++i)
        if (std::abs (x[i]) >= thr) return i;
    return 0;
}

int alignLag (const float* ref, int nref, int refStart, const float* syn, int nsyn, int synStart, double sr,
              double maxMs, double winMs)
{
    const int maxLag = (int) (maxMs * sr / 1000.0), win = (int) (winMs * sr / 1000.0);
    double best = -2.0;
    int bestL = 0;
    for (int L = -maxLag; L <= maxLag; ++L)
    {
        const int s0 = synStart + L;
        if (s0 < 0) continue;
        double xy = 0, xx = 1.0e-18, yy = 1.0e-18;
        for (int i = 0; i < win; ++i)
        {
            const int ri = refStart + i, si = s0 + i;
            if (ri >= nref || si >= nsyn) break;
            xy += (double) ref[ri] * syn[si];
            xx += (double) ref[ri] * ref[ri];
            yy += (double) syn[si] * syn[si];
        }
        const double c = xy / std::sqrt (xx * yy);
        if (c > best + 1.0e-9) { best = c; bestL = L; }
    }
    return bestL;
}

std::string noteName (double hz)
{
    if (hz <= 0.0) return "-";
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double m = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int r = (int) std::lround (m);
    const int cents = (int) std::lround ((m - r) * 100.0);
    return fmt ("%s%d %+dct", names[((r % 12) + 12) % 12], r / 12 - 1, cents);
}

// ------------------------------------------------------------------ Tracker
Tracker::Tracker (const float* x, int n, double sampleRate, int start) : sr (sampleRate)
{
    start = std::max (0, std::min (start, n));
    const int cap = (int) (3.0 * sr);
    const int len = std::min (n - start, cap);
    raw.assign (x + start, x + start + std::max (0, len));

    std::vector<float> f = raw;
    Biquad b;
    b.lowpass (600.0, 0.7071, sr);
    b.runZeroPhase (f);
    b.runZeroPhase (f);
    decim = std::max (1, (int) (sr / 4000.0));
    lp.reserve (f.size() / (size_t) decim + 1);
    for (size_t i = 0; i < f.size(); i += (size_t) decim) lp.push_back (f[i]);

    const int w = std::max (4, (int) (0.010 * sr / decim)), hop = std::max (1, w / 2);
    for (int s = 0; s + w <= (int) lp.size(); s += hop)
    {
        double e = 0;
        for (int i = 0; i < w; ++i) e += (double) lp[(size_t) (s + i)] * lp[(size_t) (s + i)];
        lpPeakRms = std::max (lpPeakRms, std::sqrt (e / w));
    }
}

double Tracker::rmsDbAt (double tMs) const
{
    const int w = std::max (4, (int) (std::clamp (0.5 * tMs, 2.0, 25.0) * sr / 1000.0));
    const int c = (int) (tMs * sr / 1000.0);
    const int s = std::max (0, c - w / 2);
    if (s >= (int) raw.size()) return -120.0;
    double e = 0;
    for (int i = 0; i < w; ++i)
    {
        const int k = s + i;
        const double v = k < (int) raw.size() ? raw[(size_t) k] : 0.0;
        e += v * v;
    }
    return std::max (-120.0, toDb (std::sqrt (e / w)));
}

double Tracker::hzAt (double tMs, double* confOut) const
{
    if (confOut) *confOut = 0.0;
    const double srd = sr / decim;
    const int M = (int) lp.size();
    const int Wn = std::max (8, (int) (std::clamp (0.8 * tMs, 14.0, 60.0) * srd / 1000.0));
    const int c = (int) (tMs * srd / 1000.0);
    const int s = std::max (0, c - Wn / 2);
    if (s + Wn / 2 >= M) return -1.0;

    double e = 0;
    int cnt = 0;
    for (int i = 0; i < Wn && s + i < M; ++i, ++cnt) e += (double) lp[(size_t) (s + i)] * lp[(size_t) (s + i)];
    if (cnt == 0 || std::sqrt (e / cnt) < lpPeakRms * 0.0056) return -1.0; // 45 dB under the loudest window

    const int minLag = std::max (2, (int) (srd / 500.0)), maxLag = std::min (M - 2, (int) (srd / 25.0));
    if (maxLag <= minLag + 2) return -1.0;
    std::vector<double> nsdf ((size_t) maxLag + 2, 0.0);
    for (int lag = 1; lag <= maxLag + 1; ++lag)
    {
        double num = 0, den = 1.0e-18;
        for (int i = 0; i < Wn; ++i)
        {
            const int a = s + i, b = a + lag;
            if (b >= M) break;
            num += (double) lp[(size_t) a] * lp[(size_t) b];
            den += (double) lp[(size_t) a] * lp[(size_t) a] + (double) lp[(size_t) b] * lp[(size_t) b];
        }
        nsdf[(size_t) lag] = 2.0 * num / den;
    }

    struct Peak { int lag; double v; };
    std::vector<Peak> peaks;
    int lag = 1;
    while (lag <= maxLag && nsdf[(size_t) lag] > 0.0) ++lag; // skip the zero-lag lobe
    while (lag <= maxLag)
    {
        while (lag <= maxLag && nsdf[(size_t) lag] <= 0.0) ++lag;
        int bi = -1;
        double bv = 0.0;
        while (lag <= maxLag && nsdf[(size_t) lag] > 0.0)
        {
            if (nsdf[(size_t) lag] > bv) { bv = nsdf[(size_t) lag]; bi = lag; }
            ++lag;
        }
        if (bi >= minLag) peaks.push_back ({ bi, bv });
    }
    if (peaks.empty()) return -1.0;
    double mx = 0;
    for (auto& p : peaks) mx = std::max (mx, p.v);
    const Peak* pick = nullptr;
    for (auto& p : peaks)
        if (p.v >= 0.9 * mx) { pick = &p; break; }
    if (pick == nullptr || pick->v < 0.6) return -1.0;

    double l = pick->lag;
    const double y0 = nsdf[(size_t) pick->lag - 1], y1 = nsdf[(size_t) pick->lag], y2 = nsdf[(size_t) pick->lag + 1];
    const double d = y0 - 2.0 * y1 + y2;
    if (std::abs (d) > 1.0e-12) l += std::clamp (0.5 * (y0 - y2) / d, -1.0, 1.0);
    if (confOut) *confOut = pick->v;
    return srd / l;
}

// ------------------------------------------------------------------ features
KickFeatures analyseKick (const float* x, int n, double sr, int start)
{
    KickFeatures f;
    f.sr = sr;
    start = std::max (0, std::min (start, n));
    f.start = start;
    const int len = n - start;
    if (len < (int) (0.01 * sr)) return f;
    const float* p = x + start;

    float pk = 0;
    for (int i = 0; i < len; ++i) pk = std::max (pk, std::abs (p[i]));
    if (pk <= 1.0e-6f) return f;
    f.ok = true;
    f.peakDb = toDb (pk);

    Tracker tr (x, n, sr, start);
    // envelope peak + length
    const int w = std::max (8, (int) (0.010 * sr)), hop = std::max (1, w / 2);
    std::vector<double> rms;
    for (int s = 0; s + w <= len; s += hop)
    {
        double e = 0;
        for (int i = 0; i < w; ++i) e += (double) p[s + i] * p[s + i];
        rms.push_back (toDb (std::sqrt (e / w)));
    }
    if (rms.empty()) rms.push_back (f.peakDb);
    f.envPeakDb = *std::max_element (rms.begin(), rms.end());
    int lastAbove = 0;
    for (size_t i = 0; i < rms.size(); ++i)
        if (rms[i] > f.envPeakDb - 50.0) lastAbove = (int) i;
    f.lengthMs = std::max (5.0, ((double) lastAbove * hop + w) * 1000.0 / sr);

    // overall RMS (first 400 ms)
    {
        const int m = std::min (len, (int) (0.4 * sr));
        double e = 0;
        for (int i = 0; i < m; ++i) e += (double) p[i] * p[i];
        f.rmsDb = toDb (std::sqrt (e / std::max (1, m)));
        double e10 = 0;
        const int m10 = std::min (len, (int) (0.010 * sr));
        for (int i = 0; i < m10; ++i) e10 += (double) p[i] * p[i];
        f.transientPct = e > 0 ? 100.0 * e10 / e : 0.0;
    }

    for (int i = 0; i < kNumTimes; ++i)
    {
        const double t = kTimesMs[(size_t) i];
        f.envDb[(size_t) i] = t < tr.durationMs() ? tr.rmsDbAt (t) : -120.0;
        f.pitchHz[(size_t) i] = t <= f.lengthMs ? tr.hzAt (t) : -1.0;
    }

    // tail fundamental: upper half of the windowed estimates from 100 ms until the level drops 35 dB
    std::vector<double> hz;
    for (double t = 100.0; t < tr.durationMs() - 30.0 && t < 3000.0; t += 20.0)
    {
        if (tr.rmsDbAt (t) < f.envPeakDb - 35.0) break;
        const double h = tr.hzAt (t);
        if (h > 0) hz.push_back (h);
    }
    if (hz.empty())
    {
        for (int i = kNumTimes - 1; i >= 0; --i)
            if (f.pitchHz[(size_t) i] > 0) { hz.push_back (f.pitchHz[(size_t) i]); break; }
    }
    else
        hz.erase (hz.begin(), hz.begin() + (long) (hz.size() / 2));
    if (! hz.empty())
    {
        std::sort (hz.begin(), hz.end());
        f.tailHz = hz[hz.size() / 2];
    }

    // spectra
    {
        size_t nfft; double sw;
        auto pe = powerSpectrum (p, len, 0, (int) (0.050 * sr), nfft, sw);
        bandDb (pe, nfft, sw, sr, f.bandEarlyDb);
        auto pl = powerSpectrum (p, len, (int) (0.050 * sr), (int) (0.300 * sr), nfft, sw);
        bandDb (pl, nfft, sw, sr, f.bandLateDb);
    }
    return f;
}

std::vector<double> logSpectrumDb (const float* x, int n, double sr, int start, double lenMs, int points,
                                   double fMin, double fMax)
{
    std::vector<double> out ((size_t) std::max (1, points), -120.0);
    start = std::max (0, std::min (start, n));
    size_t nfft; double sw;
    auto p = powerSpectrum (x, n, start, (int) (lenMs * sr / 1000.0), nfft, sw);
    const double binHz = sr / (double) nfft;
    for (int i = 0; i < points; ++i)
    {
        const double u = points > 1 ? (double) i / (points - 1) : 0.0;
        const double f = fMin * std::pow (fMax / fMin, u);
        const double lo = f / std::pow (2.0, 1.0 / 24.0), hi = f * std::pow (2.0, 1.0 / 24.0);
        size_t k0 = (size_t) std::ceil (lo / binHz), k1 = (size_t) std::floor (hi / binHz);
        k0 = std::max<size_t> (k0, 1);
        double e = 0;
        int c = 0;
        for (size_t k = k0; k <= k1 && k < p.size(); ++k, ++c) e += p[k];
        if (c == 0)
        {
            const size_t k = std::min (p.size() - 1, (size_t) std::lround (f / binHz));
            e = p[k];
            c = 1;
        }
        out[(size_t) i] = 10.0 * std::log10 (e / c / (sw * (double) nfft) + 1.0e-12);
    }
    return out;
}

// ------------------------------------------------------------------ compare
namespace
{
double part (double e, double scale) { return 100.0 * std::exp (-e / scale); }

std::string span (int i0, int i1) // ms span text
{
    return fmt ("%g-%g ms", kTimesMs[(size_t) i0], kTimesMs[(size_t) i1]);
}
std::string curveT (int i0, int i1, double lenMs)
{
    return fmt ("t=%.3f-%.3f", std::min (1.0, kTimesMs[(size_t) i0] / lenMs), std::min (1.0, kTimesMs[(size_t) i1] / lenMs));
}
double median (std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::sort (v.begin(), v.end());
    return v[v.size() / 2];
}
} // namespace

Comparison compareKicks (const KickFeatures& ref, const KickFeatures& syn, const HintContext& ctx)
{
    Comparison c;
    c.pitchErrSt.fill (kNaN);
    c.envErrDb.fill (kNaN);
    c.bandErrEarlyDb.fill (kNaN);
    c.bandErrLateDb.fill (kNaN);
    c.tailCents = kNaN;
    if (! ref.ok || ! syn.ok)
    {
        c.loss = 1.0e3;
        c.hints.push_back (! syn.ok ? "synth render is silent: check sub_mute, sub_gain, Amp curve and the master section"
                                    : "reference is silent or too short");
        return c;
    }

    // ---- pitch
    double pSum = 0; int pCnt = 0; bool refHasPitch = false;
    for (size_t i = 0; i < kNumTimes; ++i)
    {
        if (ref.pitchHz[i] > 0 && syn.pitchHz[i] > 0)
            c.pitchErrSt[i] = 12.0 * std::log2 (syn.pitchHz[i] / ref.pitchHz[i]);
        const bool relevant = ref.envDb[i] > ref.envPeakDb - 40.0;
        if (relevant && ref.pitchHz[i] > 0) refHasPitch = true;
        if (relevant && std::isfinite (c.pitchErrSt[i])) { pSum += std::min (12.0, std::abs (c.pitchErrSt[i])); ++pCnt; }
    }
    const double eP = pCnt > 0 ? pSum / pCnt : (refHasPitch ? 6.0 : 0.0);

    // ---- envelope
    double eSum = 0; int eCnt = 0;
    for (size_t i = 0; i < kNumTimes; ++i)
    {
        c.envErrDb[i] = std::clamp (syn.envDb[i] - ref.envDb[i], -60.0, 60.0);
        if (std::max (ref.envDb[i], syn.envDb[i]) > ref.envPeakDb - 60.0) { eSum += std::min (30.0, std::abs (c.envErrDb[i])); ++eCnt; }
    }
    const double eE = eCnt > 0 ? eSum / eCnt : 0.0;

    // ---- spectrum
    double refMax = -200;
    for (size_t b = 0; b < kNumBands; ++b) refMax = std::max ({ refMax, ref.bandEarlyDb[b], ref.bandLateDb[b] });
    double sSum = 0; int sCnt = 0;
    for (size_t b = 0; b < kNumBands; ++b)
    {
        if (std::max (ref.bandEarlyDb[b], syn.bandEarlyDb[b]) > refMax - 50.0)
        {
            c.bandErrEarlyDb[b] = syn.bandEarlyDb[b] - ref.bandEarlyDb[b];
            sSum += std::min (30.0, std::abs (c.bandErrEarlyDb[b])); ++sCnt;
        }
        if (std::max (ref.bandLateDb[b], syn.bandLateDb[b]) > refMax - 50.0)
        {
            c.bandErrLateDb[b] = syn.bandLateDb[b] - ref.bandLateDb[b];
            sSum += std::min (30.0, std::abs (c.bandErrLateDb[b])); ++sCnt;
        }
    }
    const double eS = sCnt > 0 ? sSum / sCnt : 0.0;

    // ---- tail / length / transient
    double eT = 0.0;
    if (ref.tailHz > 0 && syn.tailHz > 0)
    {
        c.tailCents = 1200.0 * std::log2 (syn.tailHz / ref.tailHz);
        eT = std::min (12.0, std::abs (c.tailCents) / 100.0);
    }
    else if (ref.tailHz > 0 || syn.tailHz > 0)
        eT = 6.0;
    c.lengthRatio = syn.lengthMs / std::max (1.0, ref.lengthMs);
    const double eL = std::abs (std::log (std::max (1.0e-3, c.lengthRatio)));
    c.transientDiffPct = syn.transientPct - ref.transientPct;
    const double eX = std::abs (c.transientDiffPct);

    c.pitchScore = part (eP, 1.5);
    c.envScore = part (eE, 4.0);
    c.spectrumScore = part (eS, 5.0);
    c.tailScore = part (eT, 0.5);
    c.lengthScore = part (eL, 0.3);
    c.transientScore = part (eX, 8.0);
    c.score = 0.30 * c.pitchScore + 0.25 * c.envScore + 0.20 * c.spectrumScore + 0.10 * c.tailScore
              + 0.05 * c.lengthScore + 0.10 * c.transientScore;
    c.loss = 0.30 * eP / 1.5 + 0.25 * eE / 4.0 + 0.20 * eS / 5.0 + 0.10 * eT / 0.5 + 0.05 * eL / 0.3 + 0.10 * eX / 8.0;

    // ---- hints (most important first)
    auto& H = c.hints;
    double tailSt = 0.0;
    if (std::isfinite (c.tailCents) && std::abs (c.tailCents) > 25.0)
    {
        tailSt = c.tailCents / 100.0;
        H.push_back (fmt ("tail pitch %.1f st too %s (synth %.1f Hz vs ref %.1f Hz) -> %s sub_tune by ~%.1f st (or Sub root note / sub_fine for small steps)",
                          std::abs (tailSt), tailSt > 0 ? "high" : "low", syn.tailHz, ref.tailHz,
                          tailSt > 0 ? "lower" : "raise", std::abs (tailSt)));
    }
    // pitch-curve shape errors relative to the tail offset
    for (size_t i = 0; i < kNumTimes;)
    {
        auto bad = [&] (size_t k) {
            return std::isfinite (c.pitchErrSt[k]) && ref.envDb[k] > ref.envPeakDb - 40.0
                   && std::abs (c.pitchErrSt[k] - tailSt) > 0.7 && kTimesMs[k] < 600.0;
        };
        if (! bad (i)) { ++i; continue; }
        size_t j = i; double sum = 0; int cnt = 0;
        const bool pos = c.pitchErrSt[i] - tailSt > 0;
        while (j < kNumTimes && bad (j) && ((c.pitchErrSt[j] - tailSt > 0) == pos)) { sum += c.pitchErrSt[j] - tailSt; ++cnt; ++j; }
        H.push_back (fmt ("pitch too %s %s by ~%.1f st (curve %s) -> %s Pitch curve points in that span%s",
                          pos ? "high" : "low", span ((int) i, (int) j - 1).c_str(), std::abs (sum / cnt),
                          curveT ((int) i, (int) j - 1, ctx.subLengthMs).c_str(), pos ? "lower" : "raise",
                          pos ? " or shorten the glide (Pitch curve tension)" : " or lengthen the glide"));
        i = j;
    }
    // envelope: global offset, then residual runs
    std::vector<double> sig;
    for (size_t i = 0; i < kNumTimes; ++i)
        if (std::max (ref.envDb[i], syn.envDb[i]) > ref.envPeakDb - 40.0 && kTimesMs[i] <= 100.0) sig.push_back (c.envErrDb[i]);
    const double off = median (sig);
    if (std::abs (off) > 1.5)
        H.push_back (fmt ("overall level %.1f dB too %s -> %s sub_gain by ~%.1f dB (or the master/FX gain)", std::abs (off),
                          off > 0 ? "loud" : "quiet", off > 0 ? "lower" : "raise", std::abs (off)));
    else
    {
        // keep `off` out of the residual when it is negligible
    }
    const double base = std::abs (off) > 1.5 ? off : 0.0;
    for (size_t i = 0; i < kNumTimes;)
    {
        auto bad = [&] (size_t k) {
            return k > 0 && std::max (ref.envDb[k], syn.envDb[k]) > ref.envPeakDb - 50.0 && std::abs (c.envErrDb[k] - base) > 3.0;
        };
        if (! bad (i)) { ++i; continue; }
        size_t j = i; double sum = 0; int cnt = 0;
        const bool pos = c.envErrDb[i] - base > 0;
        while (j < kNumTimes && bad (j) && ((c.envErrDb[j] - base > 0) == pos)) { sum += c.envErrDb[j] - base; ++cnt; ++j; }
        H.push_back (fmt ("envelope too %s %s by ~%.1f dB (curve %s) -> %s Amp curve points there%s",
                          pos ? "loud" : "quiet", span ((int) i, (int) j - 1).c_str(), std::abs (sum / cnt),
                          curveT ((int) i, (int) j - 1, ctx.subLengthMs).c_str(), pos ? "lower" : "raise",
                          kTimesMs[i] >= 100 ? (pos ? " or shorten the decay / sub_length" : " or lengthen the decay / sub_length") : ""));
        i = j;
    }
    if (c.lengthRatio > 1.3 || c.lengthRatio < 0.77)
        H.push_back (fmt ("synth is %.0f%% %s than the reference (%.0f vs %.0f ms to -50 dB) -> %s sub_length / the Amp curve tail",
                          std::abs (c.lengthRatio - 1.0) * 100.0, c.lengthRatio > 1 ? "longer" : "shorter", syn.lengthMs,
                          ref.lengthMs, c.lengthRatio > 1 ? "reduce" : "increase"));
    // spectrum: three worst bands
    struct B { double err; int band; bool late; };
    std::vector<B> bs;
    for (int b = 0; b < kNumBands; ++b)
    {
        if (std::isfinite (c.bandErrEarlyDb[(size_t) b]) && std::abs (c.bandErrEarlyDb[(size_t) b]) > 6.0) bs.push_back ({ c.bandErrEarlyDb[(size_t) b], b, false });
        if (std::isfinite (c.bandErrLateDb[(size_t) b]) && std::abs (c.bandErrLateDb[(size_t) b]) > 6.0) bs.push_back ({ c.bandErrLateDb[(size_t) b], b, true });
    }
    std::sort (bs.begin(), bs.end(), [] (const B& a, const B& b) { return std::abs (a.err) > std::abs (b.err); });
    if (bs.size() > 3) bs.resize (3);
    for (auto& b : bs)
    {
        const double hz = kBandCentersHz[(size_t) b.band];
        const char* fix = hz <= 63 ? "sub_gain / Amp curve / sub_root"
                          : hz <= 500 ? "sub harmonics (sub_h1..h4_gain) or sub_drive"
                          : "click layers (click_* level/filters) or FX";
        H.push_back (fmt ("%g Hz band (%s) %.0f dB too %s -> %s %s", hz, b.late ? "50-350 ms" : "0-50 ms", std::abs (b.err),
                          b.err > 0 ? "strong" : "weak", b.err > 0 ? "reduce" : "raise", fix));
    }
    if (std::abs (c.transientDiffPct) > 6.0)
        H.push_back (fmt ("attack energy share synth %.0f%% vs ref %.0f%% in the first 10 ms -> %s click layer level / sub attack (Amp curve start, sub_phase)",
                          syn.transientPct, ref.transientPct, c.transientDiffPct > 0 ? "reduce" : "raise"));
    if (H.empty())
        H.push_back ("no major differences found");
    if (H.size() > 8) H.resize (8);
    return c;
}
} // namespace kf::analysis
