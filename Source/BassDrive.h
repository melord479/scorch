// Scorch - bass distortion DSP core.
// Header-only, no JUCE dependency, so it can be unit-tested with plain g++.
#pragma once

#include <algorithm>
#include <cmath>

namespace bd
{
constexpr double kPi = 3.14159265358979323846;

enum Mode { Tube = 0, Fuzz, Fold, Hard, Rage, NumModes };

// All values in plain (user) units.
struct Params
{
    float driveDb   = 12.0f;   // 0..48   input gain into the shaper
    int   mode      = Tube;    // see Mode
    float bias      = 0.0f;    // -1..1   asymmetry (even harmonics)
    float fold      = 0.0f;    // 0..1    extra wavefolder stage
    float crush     = 0.0f;    // 0..1    bit-depth reduction (16 -> 3 bit)
    float decimate  = 0.0f;    // 0..1    sample-rate reduction
    float punchDb   = 0.0f;    // -12..12 low shelf (90 Hz) before the shaper
    float splitHz   = 120.0f;  // 40..300 crossover for the clean sub
    float sub       = 0.5f;    // 0..1    how much clean low band is kept
    bool  monoSub   = true;    // low band summed to mono
    float lowCutHz  = 20.0f;   // post high-pass
    float highCutHz = 20000.0f;// post low-pass
    float ceilDb    = -1.0f;   // -24..0  soft ceiling of the wet signal
    float width     = 1.0f;    // 0..2    stereo width of the distorted highs
    float mix       = 1.0f;    // 0..1
    float outDb     = 0.0f;    // -24..12
    int   os        = 1;       // 0 = off, 1 = 2x, 2 = 4x
};

// ---------------------------------------------------------------- shapers --

inline float fuzzCurve(float u)
{
    // asymmetric "diode" curve: soft knee up top, harder and lower below
    return u >= 0.0f ? 1.0f - std::exp(-u) : -0.8f * (1.0f - std::exp(1.4f * u));
}

inline float modeCurve(int mode, float u)
{
    switch (mode)
    {
        case Tube: return std::tanh(u);
        case Fuzz: return fuzzCurve(u);
        case Fold: return std::sin(u * 1.5707963f);
        case Hard: return std::clamp(u, -1.0f, 1.0f);
        default:   return std::tanh(1.6f * fuzzCurve(u) + 0.5f * std::sin(2.2f * u));
    }
}

// soft knee ceiling: transparent below 75 %, smoothly reaches exactly 1.0
inline float softCeil(float z)
{
    const float a = std::fabs(z);
    constexpr float k = 0.75f;
    if (a <= k) return z;
    const float y = k + (1.0f - k) * std::tanh((a - k) / (1.0f - k));
    return z < 0.0f ? -y : y;
}

struct Shape
{
    int   mode    = 0;
    float gain    = 1.0f;
    float offset  = 0.0f;   // bias, in input domain
    float dc      = 0.0f;   // curve value at the offset (subtracted)
    float foldAmt = 0.0f;
    float levels  = 0.0f;   // 0 = no bit crush
    float ceil    = 1.0f;
    float invCeil = 1.0f;
};

inline Shape makeShape(const Params& p)
{
    Shape s;
    s.mode    = p.mode;
    s.gain    = std::pow(10.0f, p.driveDb / 20.0f);
    s.offset  = p.bias * 0.5f;
    s.dc      = modeCurve(s.mode, s.gain * s.offset);
    s.foldAmt = p.fold;
    s.levels  = p.crush > 0.001f ? std::exp2(15.0f - p.crush * 13.0f) : 0.0f; // 16 -> 3 bit
    s.ceil    = std::pow(10.0f, p.ceilDb / 20.0f);
    s.invCeil = 1.0f / s.ceil;
    return s;
}

// drive -> curve -> fold -> bit crush   (no ceiling, no sample-rate reduction)
inline float shapeSample(const Shape& s, float x)
{
    const float u = s.gain * (x + s.offset);
    float y = modeCurve(s.mode, u) - s.dc;
    if (s.foldAmt > 0.0f)
    {
        const float m = 1.0f + 4.0f * s.foldAmt;
        y += s.foldAmt * (std::sin(y * m * 1.5707963f) - y);
    }
    if (s.levels > 0.0f) y = std::floor(y * s.levels + 0.5f) / s.levels;
    return y;
}

// what the UI plots: whole static transfer curve
inline float curveSample(const Shape& s, float x)
{
    return softCeil(shapeSample(s, x) * s.invCeil) * s.ceil;
}

// ---------------------------------------------------------------- filters --

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    inline float process(float x)
    {
        const double xd = x;
        const double y = b0 * xd + z1;
        z1 = b1 * xd - a1 * y + z2;
        z2 = b2 * xd - a2 * y;
        return (float) y;
    }
    void reset() { z1 = z2 = 0.0; }

    void set(double nb0, double nb1, double nb2, double a0, double na1, double na2)
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }

    static double clampF(double fs, double f) { return std::clamp(f, 10.0, 0.45 * fs); }

    void lowpass(double fs, double f, double q)
    {
        const double w = 2.0 * kPi * clampF(fs, f) / fs, c = std::cos(w), al = std::sin(w) / (2.0 * q);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highpass(double fs, double f, double q)
    {
        const double w = 2.0 * kPi * clampF(fs, f) / fs, c = std::cos(w), al = std::sin(w) / (2.0 * q);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void lowShelf(double fs, double f, double gainDb)
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w = 2.0 * kPi * clampF(fs, f) / fs, c = std::cos(w), s = std::sin(w);
        const double al = s / 2.0 * std::sqrt(2.0);         // shelf slope S = 1
        const double sq = 2.0 * std::sqrt(A) * al;
        set(A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
            (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
};

// 4th order Linkwitz-Riley: low + high sum to an all-pass
struct Crossover
{
    Biquad lp1, lp2, hp1, hp2;
    void set(double fs, double f)
    {
        constexpr double q = 0.70710678118654752;
        lp1.lowpass(fs, f, q);  lp2.lowpass(fs, f, q);
        hp1.highpass(fs, f, q); hp2.highpass(fs, f, q);
    }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    inline void run(float x, float& lo, float& hi)
    {
        lo = lp2.process(lp1.process(x));
        hi = hp2.process(hp1.process(x));
    }
};

// ------------------------------------------------------------ oversampler --

class Oversampler
{
public:
    static constexpr int K = 16;            // half length of the FIR (taps = 2K+1)
    static constexpr int N = 2 * K + 1;
    static constexpr int kMaxIn = 32;       // max samples per call

    // round-trip latency in samples at the base rate
    static int latencyFor(int osIdx) { return osIdx == 0 ? 0 : (osIdx == 1 ? K : K + K / 2); }

    void setMode(int osIdx)
    {
        fac = osIdx == 0 ? 1 : (osIdx == 1 ? 2 : 4);
        reset();
    }
    int factor() const { return fac; }
    void reset() { upA.reset(); upB.reset(); downA.reset(); downB.reset(); }

    // returns n * factor samples; caller may modify them in place, then calls down()
    float* up(const float* in, int n)
    {
        const float* k = kernel();
        if (fac == 1)
        {
            for (int i = 0; i < n; ++i) buf1[i] = in[i];
            return buf1;
        }
        for (int i = 0; i < n; ++i) upA.push(in[i], buf2[2 * i], buf2[2 * i + 1], k);
        if (fac == 2) return buf2;
        for (int j = 0; j < 2 * n; ++j) upB.push(buf2[j], buf4[2 * j], buf4[2 * j + 1], k);
        return buf4;
    }

    void down(float* out, int n)
    {
        const float* k = kernel();
        if (fac == 1)
        {
            for (int i = 0; i < n; ++i) out[i] = buf1[i];
            return;
        }
        if (fac == 4)
            for (int j = 0; j < 2 * n; ++j) buf2[j] = downB.process(buf4[2 * j], buf4[2 * j + 1], k);
        for (int i = 0; i < n; ++i) out[i] = downA.process(buf2[2 * i], buf2[2 * i + 1], k);
    }

private:
    struct Kernel
    {
        float h[N];
        static double i0(double x)
        {
            double s = 1.0, t = 1.0;
            for (int k = 1; k < 60; ++k) { const double q = x / (2.0 * k); t *= q * q; s += t; }
            return s;
        }
        Kernel()
        {
            constexpr double beta = 8.0;
            double hd[N], sum = 0.0;
            for (int i = 0; i < N; ++i)
            {
                const int m = i - K;
                const double s = m == 0 ? 0.5 : 0.5 * std::sin(kPi * 0.5 * m) / (kPi * 0.5 * m);
                const double r = (double) m / K;
                const double w = i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0(beta);
                hd[i] = s * w;
                sum += hd[i];
            }
            for (int i = 0; i < N; ++i) h[i] = (float) (hd[i] / sum);
        }
    };
    static const float* kernel() { static const Kernel k; return k.h; }

    struct Up2
    {
        static constexpr int M = K + 1;
        float hist[M];
        int pos = 0;
        Up2() { reset(); }
        void reset() { for (auto& v : hist) v = 0.0f; pos = 0; }
        inline void push(float x, float& o0, float& o1, const float* h)
        {
            pos = (pos + 1) % M;
            hist[pos] = x;
            float a = 0.0f, b = 0.0f;
            int idx = pos;
            for (int j = 0; j < M; ++j)
            {
                a += h[2 * j] * hist[idx];
                if (j < M - 1) b += h[2 * j + 1] * hist[idx];
                idx = idx == 0 ? M - 1 : idx - 1;
            }
            o0 = 2.0f * a;
            o1 = 2.0f * b;
        }
    };

    struct Down2
    {
        float hist[N];
        int pos = 0;
        Down2() { reset(); }
        void reset() { for (auto& v : hist) v = 0.0f; pos = 0; }
        inline float process(float a0, float a1, const float* h)
        {
            pos = (pos + 1) % N;
            hist[pos] = a0;
            float y = 0.0f;
            int idx = pos;
            for (int i = 0; i < N; ++i)
            {
                y += h[i] * hist[idx];
                idx = idx == 0 ? N - 1 : idx - 1;
            }
            pos = (pos + 1) % N;
            hist[pos] = a1;
            return y;
        }
    };

    int fac = 1;
    Up2 upA, upB;
    Down2 downA, downB;
    float buf1[kMaxIn] = {};
    float buf2[kMaxIn * 2] = {};
    float buf4[kMaxIn * 4] = {};
};

// ----------------------------------------------------------------- engine --

class Engine
{
public:
    static constexpr int kSub = Oversampler::kMaxIn;   // processing granularity

    void prepare(double sampleRate)
    {
        fs = sampleRate;
        smoothA = (float) (1.0 - std::exp(-(double) kSub / (0.02 * fs)));
        dcR = 1.0 - 2.0 * kPi * 8.0 / fs;
        inited = false;
        reset();
    }

    void reset()
    {
        for (auto& c : ch) c.reset();
        peakL = peakR = 0.0f;
        coefsDirty = true;
    }

    void setParams(const Params& p)
    {
        target = p;
        if (! inited) snap();
    }

    void snap()
    {
        cur = target;
        inited = true;
        applyOs();
        coefsDirty = true;
    }

    int latency() const { return Oversampler::latencyFor(cur.os); }

    // peaks since last call
    void takePeaks(float& l, float& r) { l = peakL; r = peakR; peakL = peakR = 0.0f; }

    void process(float* l, float* r, int n)
    {
        int pos = 0;
        while (pos < n)
        {
            const int c = std::min(kSub, n - pos);
            sub(l + pos, r + pos, c);
            pos += c;
        }
    }

private:
    static constexpr int kBuf = 64, kMask = kBuf - 1;

    struct Chan
    {
        Biquad punch, hp, lp;
        Crossover xw, xd;
        Oversampler os;
        float dryBuf[kBuf] = {};
        int w = 0;
        float held = 0.0f;
        int holdCnt = 0;
        double dcx = 0.0, dcy = 0.0;

        void reset()
        {
            punch.reset(); hp.reset(); lp.reset(); xw.reset(); xd.reset(); os.reset();
            for (auto& v : dryBuf) v = 0.0f;
            w = 0; held = 0.0f; holdCnt = 0; dcx = dcy = 0.0;
        }
    };

    void applyOs()
    {
        for (auto& c : ch) c.os.setMode(cur.os);
        for (auto& c : ch)
        {
            for (auto& v : c.dryBuf) v = 0.0f;
            c.w = 0;
        }
        delay = Oversampler::latencyFor(cur.os);
    }

    void smoothStep()
    {
        auto sm = [this](float& c, float t)
        {
            c += (t - c) * smoothA;
            if (std::fabs(t - c) < 1e-5f * (1.0f + std::fabs(t))) c = t;
        };
        sm(cur.driveDb, target.driveDb);
        sm(cur.bias, target.bias);
        sm(cur.fold, target.fold);
        sm(cur.crush, target.crush);
        sm(cur.decimate, target.decimate);
        sm(cur.punchDb, target.punchDb);
        sm(cur.splitHz, target.splitHz);
        sm(cur.sub, target.sub);
        sm(cur.lowCutHz, target.lowCutHz);
        sm(cur.highCutHz, target.highCutHz);
        sm(cur.ceilDb, target.ceilDb);
        sm(cur.width, target.width);
        sm(cur.mix, target.mix);
        sm(cur.outDb, target.outDb);
        cur.mode = target.mode;
        cur.monoSub = target.monoSub;
        if (target.os != cur.os)
        {
            cur.os = target.os;
            applyOs();
        }

        shape = makeShape(cur);
        const float f = (float) ch[0].os.factor();
        holdN = cur.decimate > 0.002f ? 1 + (int) std::lround(cur.decimate * cur.decimate * 31.0f * f) : 1;
        outLin = std::pow(10.0f, cur.outDb / 20.0f);

        punchActive = std::fabs(cur.punchDb) > 0.05f;
        hpActive = cur.lowCutHz > 21.0f;
        lpActive = cur.highCutHz < 19000.0f;

        if (coefsDirty || cur.punchDb != aPunch)
        {
            for (auto& c : ch) c.punch.lowShelf(fs, 90.0, cur.punchDb);
            aPunch = cur.punchDb;
        }
        if (coefsDirty || cur.splitHz != aSplit)
        {
            for (auto& c : ch) { c.xw.set(fs, cur.splitHz); c.xd.set(fs, cur.splitHz); }
            aSplit = cur.splitHz;
        }
        if (coefsDirty || cur.lowCutHz != aLow)
        {
            for (auto& c : ch) c.hp.highpass(fs, cur.lowCutHz, 0.70710678);
            aLow = cur.lowCutHz;
        }
        if (coefsDirty || cur.highCutHz != aHigh)
        {
            for (auto& c : ch) c.lp.lowpass(fs, cur.highCutHz, 0.70710678);
            aHigh = cur.highCutHz;
        }
        coefsDirty = false;
    }

    void sub(float* l, float* r, int n)
    {
        smoothStep();
        float* io[2] = { l, r };
        float wet[2][kSub], dry[2][kSub];

        for (int c = 0; c < 2; ++c)
        {
            Chan& s = ch[c];
            float pre[kSub];
            for (int i = 0; i < n; ++i)
            {
                const float x = io[c][i];
                s.dryBuf[s.w] = x;
                dry[c][i] = s.dryBuf[(s.w + kBuf - delay) & kMask];
                s.w = (s.w + 1) & kMask;
                pre[i] = punchActive ? s.punch.process(x) : x;
            }

            float* hi = s.os.up(pre, n);
            const int m = n * s.os.factor();
            for (int j = 0; j < m; ++j)
            {
                float y = shapeSample(shape, hi[j]);
                if (holdN > 1)
                {
                    if (s.holdCnt <= 0) { s.held = y; s.holdCnt = holdN; }
                    --s.holdCnt;
                    y = s.held;
                }
                hi[j] = softCeil(y * shape.invCeil) * shape.ceil;
            }
            s.os.down(wet[c], n);

            for (int i = 0; i < n; ++i) // DC blocker (bias creates offset)
            {
                const double x = wet[c][i];
                const double y = x - s.dcx + dcR * s.dcy;
                s.dcx = x; s.dcy = y;
                wet[c][i] = (float) y;
            }
        }

        float wLo[2][kSub], wHi[2][kSub], dLo[2][kSub], dHi[2][kSub];
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                ch[c].xw.run(wet[c][i], wLo[c][i], wHi[c][i]);
                ch[c].xd.run(dry[c][i], dLo[c][i], dHi[c][i]);
            }

        const float sub = cur.sub, mix = cur.mix, width = cur.width;
        for (int i = 0; i < n; ++i)
        {
            float loL = wLo[0][i] * (1.0f - sub) + dLo[0][i] * sub;
            float loR = wLo[1][i] * (1.0f - sub) + dLo[1][i] * sub;
            if (cur.monoSub) loL = loR = 0.5f * (loL + loR);

            const float mid  = 0.5f * (wHi[0][i] + wHi[1][i]);
            const float side = 0.5f * (wHi[0][i] - wHi[1][i]) * width;

            float wL = loL + mid + side;
            float wR = loR + mid - side;
            if (hpActive) { wL = ch[0].hp.process(wL); wR = ch[1].hp.process(wR); }
            if (lpActive) { wL = ch[0].lp.process(wL); wR = ch[1].lp.process(wR); }

            // final ceiling: the crossover is all-pass, which can make flat tops overshoot,
            // and the clean sub is added on top - so the ceiling is enforced on the wet sum
            wL = softCeil(wL * shape.invCeil) * shape.ceil;
            wR = softCeil(wR * shape.invCeil) * shape.ceil;

            const float aL = dLo[0][i] + dHi[0][i];   // phase-matched dry (all-pass)
            const float aR = dLo[1][i] + dHi[1][i];

            float oL = (aL * (1.0f - mix) + wL * mix) * outLin;
            float oR = (aR * (1.0f - mix) + wR * mix) * outLin;
            if (! std::isfinite(oL)) oL = 0.0f;
            if (! std::isfinite(oR)) oR = 0.0f;
            l[i] = oL; r[i] = oR;
            peakL = std::max(peakL, std::fabs(oL));
            peakR = std::max(peakR, std::fabs(oR));
        }
    }

    double fs = 44100.0, dcR = 0.999;
    float smoothA = 0.03f, outLin = 1.0f;
    bool inited = false, coefsDirty = true, punchActive = false, hpActive = false, lpActive = false;
    int delay = 0, holdN = 1;
    float aPunch = 0.0f, aSplit = 0.0f, aLow = 0.0f, aHigh = 0.0f;
    float peakL = 0.0f, peakR = 0.0f;
    Params cur, target;
    Shape shape;
    Chan ch[2];
};

} // namespace bd
