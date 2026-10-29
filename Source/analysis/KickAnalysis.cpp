#include "KickAnalysis.h"
#include "synth/SubVoice.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace kf::analysis
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
using cd = std::complex<double>;

// ---------------------------------------------------------------- filters
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double run (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};
using Cascade = std::vector<Biquad>;

Biquad rbj (bool hp, double fc, double q, double sr)
{
    fc = std::clamp (fc, 1.0, sr * 0.49);
    const double w0 = 2.0 * kPi * fc / sr, c = std::cos (w0), al = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + al;
    Biquad b;
    if (hp) { b.b0 = (1 + c) / 2; b.b1 = -(1 + c); b.b2 = (1 + c) / 2; }
    else    { b.b0 = (1 - c) / 2; b.b1 = 1 - c;    b.b2 = (1 - c) / 2; }
    b.b0 /= a0; b.b1 /= a0; b.b2 /= a0;
    b.a1 = -2.0 * c / a0;
    b.a2 = (1.0 - al) / a0;
    return b;
}

Cascade butter4 (bool hp, double fc, double sr)
{
    return { rbj (hp, fc, 0.5411961, sr), rbj (hp, fc, 1.3065630, sr) };
}

void runForward (Cascade c, std::vector<double>& v)
{
    for (auto& s : v)
    {
        double x = s;
        for (auto& b : c) x = b.run (x);
        s = x;
    }
}

/** Zero-phase filtering (forward + backward), zero padded so the transient is not clipped. */
void filtfilt (const Cascade& proto, std::vector<double>& v, int pad)
{
    const size_t n = v.size();
    std::vector<double> p ((size_t) pad * 2 + n, 0.0);
    std::copy (v.begin(), v.end(), p.begin() + pad);
    runForward (proto, p);
    std::reverse (p.begin(), p.end());
    runForward (proto, p);
    std::reverse (p.begin(), p.end());
    std::copy (p.begin() + pad, p.begin() + pad + (long) n, v.begin());
}

// ---------------------------------------------------------------- FFT / Hilbert
void fft (std::vector<cd>& a, bool inverse)
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
        const double ang = 2.0 * kPi / (double) len * (inverse ? 1.0 : -1.0);
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
    if (inverse)
        for (auto& v : a) v /= (double) n;
}

std::vector<cd> analytic (const std::vector<double>& x)
{
    size_t N = 1;
    while (N < x.size() * 2) N <<= 1;
    std::vector<cd> a (N, cd (0, 0));
    for (size_t i = 0; i < x.size(); ++i) a[i] = x[i];
    fft (a, false);
    for (size_t k = 1; k < N / 2; ++k) a[k] *= 2.0;
    for (size_t k = N / 2 + 1; k < N; ++k) a[k] = 0.0;
    fft (a, true);
    a.resize (x.size());
    return a;
}

// ---------------------------------------------------------------- helpers
double weightedMedian (std::vector<std::pair<double, double>> v) // (value, weight)
{
    if (v.empty()) return 0.0;
    std::sort (v.begin(), v.end());
    double tot = 0;
    for (auto& p : v) tot += p.second;
    double acc = 0;
    for (auto& p : v)
    {
        acc += p.second;
        if (acc >= tot * 0.5) return p.first;
    }
    return v.back().first;
}

/** Complex demodulation: LP( x * exp(-j*k*psi) ), zero phase. */
std::vector<cd> demod (const std::vector<double>& x, const std::vector<double>& psi, int k, double cutoff, double sr)
{
    const size_t m = x.size();
    std::vector<double> re (m), im (m);
    for (size_t i = 0; i < m; ++i)
    {
        const double ph = (double) k * psi[i];
        re[i] = x[i] * std::cos (ph);
        im[i] = -x[i] * std::sin (ph);
    }
    const auto lp = butter4 (false, cutoff, sr);
    const int pad = (int) (sr * 0.25);
    filtfilt (lp, re, pad);
    filtfilt (lp, im, pad);
    std::vector<cd> z (m);
    for (size_t i = 0; i < m; ++i) z[i] = cd (re[i], im[i]);
    return z;
}
} // namespace

// ---------------------------------------------------------------- curve fitting
Curve fitBreakpoints (const std::vector<double>& t, const std::vector<double>& y, double tol, int maxPoints,
                      double relFloor)
{
    const int n = (int) t.size();
    if (n == 0) return Curve ({ { 0, 0, 0 }, { 1, 0, 0 } });
    if (n == 1) return Curve ({ { t[0], y[0], 0 }, { t[0] + 1.0, y[0], 0 } });

    struct Seg { int a, b; double tension, maxErr; int worst; double sse; };
    auto errOf = [&] (double d, double yy) { return relFloor > 0.0 ? std::abs (d) / (relFloor + std::abs (yy)) : std::abs (d); };
    auto fitSeg = [&] (int a, int b) {
        Seg s { a, b, 0.0, 0.0, a, 0.0 };
        if (b - a < 2) return s;
        const double ta = t[(size_t) a], tb = t[(size_t) b], ya = y[(size_t) a], yb = y[(size_t) b];
        double bestSse = 1e300;
        for (int k = -20; k <= 20; ++k)
        {
            const double ten = k / 20.0;
            double sse = 0, mx = 0;
            int worst = a;
            for (int i = a + 1; i < b; ++i)
            {
                const double u = (t[(size_t) i] - ta) / (tb - ta);
                const double e = errOf (ya + (yb - ya) * Curve::shape (u, ten) - y[(size_t) i], y[(size_t) i]);
                sse += e * e;
                if (e > mx) { mx = e; worst = i; }
            }
            if (sse < bestSse) { bestSse = sse; s.tension = ten; s.maxErr = mx; s.worst = worst; s.sse = sse; }
        }
        return s;
    };

    std::vector<Seg> segs { fitSeg (0, n - 1) };
    while ((int) segs.size() + 1 < maxPoints)
    {
        size_t wi = 0;
        for (size_t i = 1; i < segs.size(); ++i)
            if (segs[i].maxErr > segs[wi].maxErr) wi = i;
        if (segs[wi].maxErr <= tol) break;
        const Seg old = segs[wi];
        // split where the two halves together fit best
        int bestM = old.worst;
        double bestCost = 1e300;
        const int stepM = std::max (1, (old.b - old.a) / 60);
        for (int mi = old.a + 1; mi < old.b; mi += stepM)
        {
            const double cst = fitSeg (old.a, mi).sse + fitSeg (mi, old.b).sse;
            if (cst < bestCost) { bestCost = cst; bestM = mi; }
        }
        segs[wi] = fitSeg (old.a, bestM);
        segs.insert (segs.begin() + (long) wi + 1, fitSeg (bestM, old.b));
    }
    std::vector<Curve::Point> pts;
    for (auto& s : segs) pts.push_back ({ t[(size_t) s.a], y[(size_t) s.a], s.tension });
    pts.push_back ({ t[(size_t) segs.back().b], y[(size_t) segs.back().b], 0.0 });
    return Curve (std::move (pts));
}

// ---------------------------------------------------------------- hit detection
std::vector<Hit> detectHits (const float* x, int n, double sr)
{
    std::vector<Hit> hits;
    if (n < 16) return hits;
    std::vector<double> lp (x, x + n);
    runForward (butter4 (false, 200.0, sr), lp);

    // backward peak-hold envelope of the low band (a 37 Hz tail would make short RMS windows ripple)
    const int hop = std::max (1, (int) (sr * 0.001)), hold = (int) (0.025 * sr), nh = n / hop;
    std::vector<double> e ((size_t) nh);
    double eMax = 0;
    for (int i = 0; i < nh; ++i)
    {
        const int b = std::min (n, i * hop + hop), a = std::max (0, b - hold);
        double pk = 0;
        for (int j = a; j < b; ++j) pk = std::max (pk, std::abs (lp[(size_t) j]));
        e[(size_t) i] = pk;
        eMax = std::max (eMax, pk);
    }
    if (eMax < 1.0e-9) return hits;

    const double thr = 0.2 * eMax;
    const int gap = 60;
    int lastOn = -100000;
    for (int i = 0; i < nh; ++i)
    {
        const double prev = i >= 8 ? e[(size_t) i - 8] : 0.0;
        if (! (e[(size_t) i] > thr && e[(size_t) i] > 1.8 * prev && i - lastOn >= gap)) continue;
        lastOn = i;
        const int a = std::max (0, (i - 10) * hop), pe = std::min (n, i * hop + (int) (0.03 * sr));
        double P = 0;
        for (int j = a; j < pe; ++j) P = std::max (P, (double) std::abs (x[j]));
        int s = a;
        while (s < pe && std::abs ((double) x[s]) < 0.1 * P) ++s;
        s = std::max (0, s - (int) (0.001 * sr));
        Hit h;
        h.start = s;
        for (int j = i; j < std::min (nh, i + 100); ++j) h.strength = std::max (h.strength, (float) e[(size_t) j]);
        hits.push_back (h);
    }
    for (size_t k = 0; k < hits.size(); ++k)
    {
        const int cap = hits[k].start + (int) (4.0 * sr);
        const int nxt = k + 1 < hits.size() ? hits[k + 1].start - (int) (0.002 * sr) : n;
        hits[k].end = std::max (hits[k].start + 1, std::min ({ n, cap, nxt }));
    }
    return hits;
}

int strongestHit (const std::vector<Hit>& h)
{
    int best = -1;
    for (size_t i = 0; i < h.size(); ++i)
        if (best < 0 || h[i].strength > h[(size_t) best].strength * 1.03f) best = (int) i;
    return best;
}

// ---------------------------------------------------------------- analysis
Result analyse (const float* xin, int nin, double sr, int regionStart, int regionEnd, const Options& opt)
{
    Result R;
    R.sampleRate = sr;
    auto fail = [&] (const char* msg) { R.ok = false; R.error = msg; return R; };
    if (xin == nullptr || nin < (int) (0.02 * sr)) return fail ("Sample is too short");

    if (regionStart < 0)
    {
        const auto hits = detectHits (xin, nin, sr);
        const int k = strongestHit (hits);
        if (k < 0) return fail ("No kick hit found (silent file?)");
        regionStart = hits[(size_t) k].start;
        regionEnd = hits[(size_t) k].end;
    }
    regionStart = std::clamp (regionStart, 0, nin - 1);
    regionEnd = std::clamp (regionEnd <= regionStart ? nin : regionEnd, regionStart + 1, nin);
    regionEnd = std::min (regionEnd, regionStart + (int) (4.0 * sr));
    const int m = regionEnd - regionStart;
    if (m < (int) (0.02 * sr)) return fail ("Selected region is too short");
    R.regionStart = regionStart;
    R.regionEnd = regionEnd;

    std::vector<double> x ((size_t) m);
    for (int i = 0; i < m; ++i) x[(size_t) i] = xin[regionStart + i];

    // ---- 1. low band + analytic signal -> pitch track
    std::vector<double> lp = x;
    {
        const int pad = (int) (sr * 0.25);
        filtfilt (butter4 (false, 480.0, sr), lp, pad);
        filtfilt (butter4 (true, 18.0, sr), lp, pad);
    }
    const auto an = analytic (lp);
    const int hop = std::max (1, (int) (sr * 0.001));
    const int nf = m / hop;
    if (nf < 8) return fail ("Selected region is too short");

    std::vector<double> csum ((size_t) m + 1, 0.0);
    for (int i = 0; i < m; ++i) csum[(size_t) i + 1] = csum[(size_t) i] + std::abs (an[(size_t) i]);
    auto envAt = [&] (int c) {
        const int a = std::max (0, c - hop), b = std::min (m, c + hop + 1);
        return (csum[(size_t) b] - csum[(size_t) a]) / (double) (b - a);
    };
    std::vector<double> envF ((size_t) nf);
    double envPeak = 0;
    for (int f = 0; f < nf; ++f)
    {
        envF[(size_t) f] = envAt (f * hop);
        envPeak = std::max (envPeak, envF[(size_t) f]);
    }
    if (envPeak < 1.0e-7) return fail ("Sample is silent in the low band");

    std::vector<double> fHz ((size_t) nf, 0.0);
    int firstV = -1, lastV = -1;
    for (int f = 0; f < nf; ++f)
        if (envF[(size_t) f] >= 0.03 * envPeak) { if (firstV < 0) firstV = f; lastV = f; }
    if (lastV - firstV < 4) return fail ("Could not find a low-frequency body in this sample");

    double fprev = 150.0;
    for (int f = firstV; f <= lastV; ++f)
    {
        const int c = f * hop;
        const int W = std::clamp ((int) (0.75 / fprev * sr), (int) (0.002 * sr), (int) (0.03 * sr));
        const int lo = std::max (0, c - W), hi = std::min (m - 2, c + W);
        cd s (0, 0);
        for (int j = lo; j <= hi; ++j) s += an[(size_t) j + 1] * std::conj (an[(size_t) j]);
        double hz = std::abs (s) > 1.0e-18 ? std::arg (s) * sr / (2.0 * kPi) : fprev;
        hz = std::clamp (hz, opt.minHz, opt.maxHz);
        fHz[(size_t) f] = hz;
        fprev = hz;
    }
    { // median-of-3 smoothing + early-edge fix
        auto copy = fHz;
        for (int f = firstV + 1; f < lastV; ++f)
        {
            double a = copy[(size_t) f - 1], b = copy[(size_t) f], c = copy[(size_t) f + 1];
            fHz[(size_t) f] = std::max (std::min (a, b), std::min (std::max (a, b), c));
        }
        const int e0 = std::min (lastV, std::max (firstV, (int) std::ceil (0.0025 * sr / hop)));
        for (int f = firstV; f < e0; ++f) fHz[(size_t) f] = fHz[(size_t) e0];
    }
    // tail frequency: envelope-weighted median over the last quarter of the voiced span
    double fRef;
    {
        const int from = lastV - std::max (3, (lastV - firstV) / 4);
        std::vector<std::pair<double, double>> v;
        for (int f = std::max (firstV, from); f <= lastV; ++f)
            if (envF[(size_t) f] >= 0.05 * envPeak) v.push_back ({ fHz[(size_t) f], envF[(size_t) f] });
        if (v.empty())
            for (int f = std::max (firstV, from); f <= lastV; ++f) v.push_back ({ fHz[(size_t) f], envF[(size_t) f] });
        fRef = weightedMedian (v);
    }
    for (int f = 0; f < firstV; ++f) fHz[(size_t) f] = fHz[(size_t) firstV];
    for (int f = lastV + 1; f < nf; ++f) fHz[(size_t) f] = fRef;

    // root + tune so that the tail lands on fRef
    const double midi = 69.0 + 12.0 * std::log2 (fRef / opt.a4);
    const int root = std::clamp ((int) std::lround (midi), 12, 96);
    const double tune = std::clamp (std::round ((midi - root) * 100.0) / 100.0, -24.0, 24.0);
    const double fq = opt.a4 * std::exp2 ((root + tune - 69.0) / 12.0);

    // per-sample tracked phase
    std::vector<double> psi ((size_t) m + 1, 0.0);
    for (int i = 0; i < m; ++i)
    {
        const double fp = (double) i / hop;
        const int f0 = std::min (nf - 1, (int) fp), f1 = std::min (nf - 1, f0 + 1);
        const double fr = std::min (1.0, fp - f0);
        const double hz = fHz[(size_t) f0] * (1 - fr) + fHz[(size_t) f1] * fr;
        psi[(size_t) i + 1] = psi[(size_t) i] + 2.0 * kPi * hz / sr;
    }

    // ---- 2. fundamental amplitude / phase by heterodyne
    const double cutFast = std::clamp (0.8 * fRef, 15.0, 60.0), cutSlow = std::clamp (0.3 * fRef, 6.0, 25.0);
    const auto zFast = demod (x, psi, 1, cutFast, sr);
    const auto zSlow = demod (x, psi, 1, cutSlow, sr);
    std::vector<cd> z ((size_t) m);
    std::vector<double> a ((size_t) m);
    double aPeak = 0;
    for (int i = 0; i < m; ++i)
    {
        const double t = i / sr, w = std::clamp (1.0 - (t - 0.04) / 0.02, 0.0, 1.0);
        z[(size_t) i] = zFast[(size_t) i] * w + zSlow[(size_t) i] * (1.0 - w);
        a[(size_t) i] = 2.0 * std::abs (z[(size_t) i]);
        aPeak = std::max (aPeak, a[(size_t) i]);
    }
    if (aPeak < 1.0e-6) return fail ("No usable low-frequency energy");
    R.peakAmp = aPeak;

    int lastAbove = 0;
    for (int i = m - 1; i >= 0; --i)
        if (a[(size_t) i] >= 0.012 * aPeak) { lastAbove = i; break; }
    const double lengthS = std::clamp ((lastAbove) / sr + 0.008, 0.03, std::min (4.0, m / sr));
    const double lengthSamples = lengthS * sr;

    // ---- 3. curve fits
    std::vector<double> tAmp, yAmp, tPit, yPit;
    for (int f = 0; f < nf; ++f)
    {
        const double ts = f * hop / sr;
        if (ts > lengthS) break;
        const double u = ts / lengthS;
        tAmp.push_back (u);
        yAmp.push_back (a[(size_t) f * (size_t) hop] / aPeak);
        tPit.push_back (u);
        yPit.push_back (12.0 * std::log2 (fHz[(size_t) f] / fq));
    }
    auto closeEnd = [] (std::vector<double>& t, std::vector<double>& y, double endVal) {
        if (t.back() >= 1.0 - 1.0e-6) { t.back() = 1.0; y.back() = endVal; }
        else { t.push_back (1.0); y.push_back (endVal); }
    };
    closeEnd (tAmp, yAmp, 0.0);
    closeEnd (tPit, yPit, 0.0);
    R.amp = fitBreakpoints (tAmp, yAmp, 0.04, 14, 0.1);
    R.pitch = fitBreakpoints (tPit, yPit, 0.3, 16, 0.0);
    R.harmAmount = Curve ({ { 0.0, 1.0, 0.0 }, { 1.0, 1.0, 0.0 } });

    // fit quality
    {
        double se = 0, sw = 0;
        std::vector<double> A, B;
        for (size_t i = 0; i + 1 < tPit.size(); ++i)
        {
            const double ts = tPit[i] * lengthS, av = a[(size_t) (ts * sr)] / aPeak;
            if (av >= 0.1) { const double d = R.pitch.eval (tPit[i]) - yPit[i]; se += d * d; sw += 1; }
            A.push_back (R.amp.eval (tAmp[i]));
            B.push_back (yAmp[i]);
        }
        R.pitchFitRmsSemis = sw > 0 ? std::sqrt (se / sw) : 0.0;
        double ma = 0, mb = 0;
        for (size_t i = 0; i < A.size(); ++i) { ma += A[i]; mb += B[i]; }
        ma /= (double) std::max<size_t> (1, A.size()); mb /= (double) std::max<size_t> (1, B.size());
        double sab = 0, saa = 0, sbb = 0;
        for (size_t i = 0; i < A.size(); ++i)
        {
            sab += (A[i] - ma) * (B[i] - mb); saa += (A[i] - ma) * (A[i] - ma); sbb += (B[i] - mb) * (B[i] - mb);
        }
        R.ampFitCorrelation = saa > 0 && sbb > 0 ? sab / std::sqrt (saa * sbb) : 0.0;
    }

    // ---- 4. start phase: match the fitted-curve oscillator to the measured fundamental over the first 20 ms
    double startPhaseDeg = 0.0;
    {
        cd sum (0, 0);
        double psiFit = 0.0;
        const int nPh = std::min (m, (int) (0.02 * sr));
        for (int i = 0; i < nPh; ++i)
        {
            const double u = std::min (1.0, i / lengthSamples);
            const double dpsi = psi[(size_t) i] - psiFit;
            sum += a[(size_t) i] * z[(size_t) i] * cd (std::cos (dpsi), std::sin (dpsi));
            psiFit += 2.0 * kPi * fq * std::exp2 (R.pitch.eval (u) / 12.0) / sr;
        }
        double th = std::arg (sum) + kPi / 2.0; // cos(x) = sin(x + 90 deg)
        th = std::fmod (th, 2.0 * kPi);
        if (th < 0) th += 2.0 * kPi;
        startPhaseDeg = th * 180.0 / kPi;
    }

    // ---- 5. sub settings
    SubSettings& S = R.sub;
    S.gainDb = (float) std::clamp (20.0 * std::log10 (aPeak), -60.0, 6.0);
    S.wave = SubWave::Sine;
    S.pitchMode = PitchMode::Semitones;
    S.rootNote = (float) root;
    S.tuneSemis = (float) tune;
    S.fineCents = 0.0f;
    S.lengthMs = (float) std::clamp (std::round (lengthS * 1000.0), 20.0, 4000.0);
    S.startPhaseDeg = (float) startPhaseDeg;
    S.drive = 0.0f;
    S.phaseLock = false;
    S.delayMs = 0.0f;
    R.startHz = fq * std::exp2 (R.pitch.eval (0.0) / 12.0);
    R.tailHz = fq;

    // ---- 6. optional harmonics 2..10 (+ their measured components, subtracted from the top end)
    std::vector<double> comp ((size_t) m);
    for (int i = 0; i < m; ++i)
        comp[(size_t) i] = 2.0 * (z[(size_t) i].real() * std::cos (psi[(size_t) i]) - z[(size_t) i].imag() * std::sin (psi[(size_t) i]));
    if (opt.harmonics)
    {
        for (int hIdx = 0; hIdx < SubSettings::kNumHarmonics; ++hIdx)
        {
            const int k = hIdx + 2;
            S.harm[(size_t) hIdx].semis = (float) (12.0 * std::log2 ((double) k));
            S.harm[(size_t) hIdx].gainDb = -60.0f;
            S.harm[(size_t) hIdx].decayMs = 5000.0f;
            if (fHz[(size_t) firstV] * k > 0.45 * sr) continue;
            const auto zk = demod (x, psi, k, cutSlow, sr);
            double sw = 0, sx = 0, sy = 0;
            struct Pt { double t, L, w; };
            std::vector<Pt> pts;
            for (int f = 0; f < nf; ++f)
            {
                const double ts = f * hop / sr;
                if (ts < 0.008 || ts > 0.6 * lengthS) continue;
                const int c = f * hop;
                const double a1 = 2.0 * std::abs (zSlow[(size_t) c]);
                if (a1 < 0.1 * aPeak) continue;
                const double L = 20.0 * std::log10 (2.0 * std::abs (zk[(size_t) c]) / a1 + 1.0e-6);
                pts.push_back ({ ts, L, a1 * a1 });
                sw += a1 * a1; sx += ts * a1 * a1; sy += L * a1 * a1;
            }
            if (pts.size() < 6) continue;
            const double xb = sx / sw, yb = sy / sw;
            double sxy = 0, sxx = 0;
            for (auto& p : pts) { sxy += p.w * (p.t - xb) * (p.L - yb); sxx += p.w * (p.t - xb) * (p.t - xb); }
            double slope = sxx > 0 ? sxy / sxx : 0.0;
            if (yb < -36.0) continue;
            double L0 = yb - slope * xb;
            double decayMs = 5000.0;
            if (slope < -2.0) decayMs = std::clamp (60.0 / -slope * 1000.0, 5.0, 4999.0);
            else L0 = yb;
            S.harm[(size_t) hIdx].gainDb = (float) std::clamp (L0, -59.0, 6.0);
            S.harm[(size_t) hIdx].decayMs = (float) decayMs;
            ++R.numHarmonicsUsed;
            for (int i = 0; i < m; ++i)
                comp[(size_t) i] += 2.0 * (zk[(size_t) i].real() * std::cos (k * psi[(size_t) i])
                                           - zk[(size_t) i].imag() * std::sin (k * psi[(size_t) i]));
        }
    }

    // ---- 7. residual = original - extracted sub, high-passed, trimmed
    {
        std::vector<double> res ((size_t) m);
        for (int i = 0; i < m; ++i) res[(size_t) i] = x[(size_t) i] - comp[(size_t) i];
        R.residualHpHz = std::clamp (2.5 * fRef, 100.0, 300.0);
        filtfilt (butter4 (true, R.residualHpHz, sr), res, (int) (sr * 0.25));
        const int h5 = std::max (1, (int) (0.005 * sr)), nb = m / h5;
        std::vector<double> rms ((size_t) nb);
        double pk = 0;
        for (int b = 0; b < nb; ++b)
        {
            double s = 0;
            for (int j = 0; j < h5; ++j) s += res[(size_t) (b * h5 + j)] * res[(size_t) (b * h5 + j)];
            rms[(size_t) b] = std::sqrt (s / h5);
            pk = std::max (pk, rms[(size_t) b]);
        }
        if (pk > 1.0e-5)
        {
            int lastB = 0;
            for (int b = nb - 1; b >= 0; --b)
                if (rms[(size_t) b] >= 0.002 * pk) { lastB = b; break; }
            const int end = std::min (m, (lastB + 1) * h5 + (int) (0.01 * sr));
            R.residual.assign ((size_t) end, 0.0f);
            for (int i = 0; i < end; ++i) R.residual[(size_t) i] = (float) res[(size_t) i];
            const int fade = std::min (end, (int) (0.003 * sr));
            for (int i = 0; i < fade; ++i)
                R.residual[(size_t) (end - 1 - i)] *= (float) i / (float) std::max (1, fade);
        }
    }

    R.ok = true;
    return R;
}

std::vector<float> renderSub (const Result& r, double sr, int numSamples, double a4)
{
    CurveSlot pitch (r.pitch), amp (r.amp), harm (r.harmAmount);
    SubVoice v;
    GlobSettings g;
    g.velSens = 0.0f;
    g.a4 = (float) a4;
    v.prepare (sr, 256);
    v.setCurveSlots (&pitch, &amp, nullptr, &harm, nullptr);
    v.setSettings (r.sub, g);
    v.noteOn ((int) std::lround (r.sub.rootNote), 1.0f);
    std::vector<float> out;
    out.reserve ((size_t) numSamples);
    int left = numSamples;
    while (left > 0)
    {
        const int b = std::min (left, 256);
        juce::AudioBuffer<float> buf (1, b);
        buf.clear();
        v.process (buf, 0, b);
        for (int i = 0; i < b; ++i) out.push_back (buf.getSample (0, i));
        left -= b;
    }
    return out;
}
} // namespace kf::analysis
