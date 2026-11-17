// Offline checks for the Scorch DSP core.
//   g++ -O2 -std=c++17 tests/dsp_test.cpp -o dsp_test && ./dsp_test
//   ./dsp_test --demo demo_808.wav      (also renders a listening demo)
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <complex>
#include <random>
#include <string>
#include <vector>

#include "../Source/BassDrive.h"
#include "../Source/Presets.h"

using namespace bd;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf("  FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static std::vector<float> run(Engine& e, std::vector<float> l, std::vector<float> r)
{
    // feed in odd-sized blocks on purpose
    size_t pos = 0; const size_t n = l.size();
    const int sizes[] = { 37, 64, 5, 100, 1, 255 };
    int k = 0;
    while (pos < n)
    {
        size_t c = std::min<size_t>(sizes[k++ % 6], n - pos);
        e.process(l.data() + pos, r.data() + pos, (int) c);
        pos += c;
    }
    std::vector<float> out(2 * n);
    for (size_t i = 0; i < n; ++i) { out[i] = l[i]; out[n + i] = r[i]; }
    return out;
}

static double rms(const float* x, size_t n) { double s = 0; for (size_t i = 0; i < n; ++i) s += (double) x[i] * x[i]; return std::sqrt(s / n); }

// ----- tiny FFT -----
static void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2 * kPi / len;
        std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1);
            for (size_t j = 0; j < len / 2; ++j)
            {
                auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

static void testOversamplerLatencyAndGain()
{
    std::printf("[oversampler latency / gain]\n");
    for (int os = 0; os < 3; ++os)
    {
        Oversampler o; o.setMode(os);
        // impulse
        std::vector<float> in(256, 0.f), out(256, 0.f);
        in[10] = 1.f;
        for (int p = 0; p < 256; p += 32) { float* hi = o.up(&in[p], 32); (void) hi; o.down(&out[p], 32); }
        int peak = 0; for (int i = 0; i < 256; ++i) if (std::fabs(out[i]) > std::fabs(out[peak])) peak = i;
        const int lat = Oversampler::latencyFor(os);
        std::printf("  os=%d  impulse peak at +%d (reported latency %d), value %.4f\n", os, peak - 10, lat, out[peak]);
        CHECK(peak - 10 == lat, "latency mismatch for os=%d", os);
        CHECK(out[peak] > 0.9f && out[peak] < 1.02f, "impulse amplitude off (%.3f)", out[peak]); // band-limited impulse is slightly below 1

        // sine gain
        for (double f : { 100.0, 1000.0, 10000.0 })
        {
            Oversampler q; q.setMode(os);
            const int N = 8192; std::vector<float> a(N), b(N);
            for (int i = 0; i < N; ++i) a[i] = (float) std::sin(2 * kPi * f * i / 44100.0);
            for (int p = 0; p < N; p += 32) { q.up(&a[p], 32); q.down(&b[p], 32); }
            const double g = rms(&b[2048], 4096) / rms(&a[2048], 4096);
            const double db = 20 * std::log10(g);
            CHECK(std::fabs(db) < 0.3, "gain at %g Hz os=%d is %.3f dB", f, os, db);
            if (os == 2) std::printf("  4x  %5.0f Hz gain %+.3f dB\n", f, db);
        }
    }
}

static void testCrossoverFlat()
{
    std::printf("[crossover sums flat]\n");
    const double fs = 44100;
    double worst = 0;
    for (double split : { 60.0, 120.0, 250.0 })
        for (double f : { 30.0, 80.0, 120.0, 200.0, 500.0, 3000.0, 12000.0 })
        {
            Crossover x; x.set(fs, split);
            const int N = 44100; std::vector<float> s(N);
            for (int i = 0; i < N; ++i)
            {
                float lo, hi, v = (float) std::sin(2 * kPi * f * i / fs);
                x.run(v, lo, hi); s[i] = lo + hi;
            }
            double in = std::sqrt(0.5);
            double db = 20 * std::log10(rms(&s[22050], 20000) / in);
            worst = std::max(worst, std::fabs(db));
        }
    std::printf("  worst deviation %.3f dB\n", worst);
    CHECK(worst < 0.1, "crossover not flat (%.3f dB)", worst);
}

static void testEngineLatencyAlignment()
{
    std::printf("[engine: dry path latency alignment]\n");
    int firstPeak[3] = {};
    for (int os = 0; os < 3; ++os)
    {
        Engine e; e.prepare(44100);
        Params p; p.mix = 0.0f; p.os = os; p.sub = 0.0f;
        e.setParams(p);
        std::vector<float> l(4096, 0.f), r(4096, 0.f); l[100] = 1.f; r[100] = 1.f;
        auto o = run(e, l, r);
        int pk = 0; for (int i = 0; i < 4096; ++i) if (std::fabs(o[i]) > std::fabs(o[pk])) pk = i;
        firstPeak[os] = pk;
        CHECK(e.latency() == Oversampler::latencyFor(os), "latency() wrong");
    }
    std::printf("  allpass peak position: off=%d 2x=%d 4x=%d\n", firstPeak[0], firstPeak[1], firstPeak[2]);
    CHECK(firstPeak[1] - firstPeak[0] == Oversampler::latencyFor(1), "2x dry delay misaligned");
    CHECK(firstPeak[2] - firstPeak[0] == Oversampler::latencyFor(2), "4x dry delay misaligned");
}

static void testStability()
{
    std::printf("[stability: noise + extreme settings]\n");
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> u(-1.f, 1.f);
    double worstPeak = 0;
    for (int mode = 0; mode < NumModes; ++mode)
        for (int os = 0; os < 3; ++os)
            for (int extreme = 0; extreme < 2; ++extreme)
            {
                Engine e; e.prepare(48000);
                Params p; p.mode = mode; p.os = os;
                if (extreme)
                {
                    p.driveDb = 48; p.bias = 1; p.fold = 1; p.crush = 1; p.decimate = 1; p.punchDb = 12;
                    p.splitHz = 40; p.sub = 1; p.lowCutHz = 500; p.highCutHz = 1000; p.ceilDb = 0;
                    p.width = 2; p.outDb = 12;
                }
                e.setParams(p);
                std::vector<float> l(48000), r(48000);
                for (auto& v : l) v = u(rng);
                for (auto& v : r) v = u(rng);
                auto o = run(e, l, r);
                bool ok = true; double pk = 0;
                for (float v : o) { if (!std::isfinite(v)) ok = false; pk = std::max(pk, (double) std::fabs(v)); }
                worstPeak = std::max(worstPeak, pk);
                CHECK(ok, "non finite output mode=%d os=%d extreme=%d", mode, os, extreme);
                CHECK(pk < 4.2, "peak too high %.2f mode=%d os=%d extreme=%d", pk, mode, os, extreme);
            }
    std::printf("  worst peak across 30 runs: %.2f\n", worstPeak);
}

static void testAliasing()
{
    std::printf("[aliasing: hard-clipped sine]\n");
    const int N = 16384; const double fs = 48000; const int k0 = 1001; // coherent: f0 = 2932.6 Hz
    double altDb[3] = {};
    for (int os = 0; os < 3; ++os)
    {
        Engine e; e.prepare(fs);
        Params p; p.mode = Hard; p.driveDb = 40; p.os = os; p.sub = 0.f; p.ceilDb = 0.f; p.mix = 1.f;
        e.setParams(p);
        std::vector<float> l(8 * N), r;
        for (int i = 0; i < 8 * N; ++i) l[i] = 0.5f * (float) std::sin(2 * kPi * k0 * (double) i / N);
        r = l;
        auto o = run(e, l, r);
        const int lat = e.latency();
        std::vector<std::complex<double>> a(N);
        for (int i = 0; i < N; ++i) a[i] = o[6 * (size_t) N + lat + i]; // steady state, one full period window
        fft(a);
        double harm = 0, alias = 0;
        for (int b = 1; b < N / 2; ++b)
        {
            const double pw = std::norm(a[b]);
            if (b % k0 == 0) harm += pw; else alias += pw;
        }
        altDb[os] = 10 * std::log10(alias / harm);
        std::printf("  os=%d  non-harmonic/harmonic energy: %.1f dB\n", os, altDb[os]);
    }
    CHECK(altDb[1] < altDb[0] - 6.0, "2x oversampling should cut aliasing by >6 dB");
    CHECK(altDb[2] < altDb[1] - 3.0, "4x oversampling should cut aliasing further");
}

// ---- 808 generator for presets / demo ----
static std::vector<float> make808(double fs, double f0, double dur)
{
    const size_t n = (size_t) (fs * dur);
    std::vector<float> x(n);
    double ph = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const double f = f0 * (1.0 + 3.0 * std::exp(-t / 0.03));
        ph += 2 * kPi * f / fs;
        const double env = std::exp(-t / 0.55) * (1.0 - std::exp(-t / 0.002));
        x[i] = (float) (0.9 * env * std::sin(ph));
    }
    return x;
}

static std::vector<float> pattern(double fs)
{
    std::vector<float> out((size_t) (fs * 4.0), 0.f);
    const double notes[4] = { 41.2, 41.2, 49.0, 36.7 };
    for (int k = 0; k < 4; ++k)
    {
        auto n = make808(fs, notes[k], 1.4);
        const size_t off = (size_t) (k * 0.9 * fs);
        for (size_t i = 0; i < n.size() && off + i < out.size(); ++i) out[off + i] += n[i];
    }
    return out;
}

static void testPresets(std::vector<std::vector<float>>* rendered)
{
    std::printf("[presets on a synthetic 808]\n");
    const double fs = 44100;
    auto src = pattern(fs);
    if (rendered) rendered->push_back(src);
    std::printf("  %-14s %7s %7s %7s %9s\n", "preset", "peak", "rms dB", "dc", "hf/lf");
    for (const auto& pr : presets())
    {
        Engine e; e.prepare(fs); e.setParams(pr.p);
        auto o = run(e, src, src);
        const size_t n = src.size();
        double pk = 0; for (size_t i = 0; i < n; ++i) pk = std::max(pk, (double) std::fabs(o[i]));
        double dc = 0; for (size_t i = 0; i < n; ++i) dc += o[i]; dc /= n;
        // crude brightness: energy of first-difference vs signal
        double d = 0, s = 0; for (size_t i = 1; i < n; ++i) { double df = o[i] - o[i - 1]; d += df * df; s += (double) o[i] * o[i]; }
        std::printf("  %-14s %7.3f %7.1f %7.4f %9.3f\n", pr.name, pk, 20 * std::log10(rms(o.data(), n) + 1e-12), dc, std::sqrt(d / (s + 1e-12)));
        CHECK(std::isfinite(pk) && pk < 1.0, "%s peak %.2f", pr.name, pk);
        CHECK(std::fabs(dc) < 0.02, "%s dc %.4f", pr.name, dc);
        if (rendered && std::string(pr.name) != "Init" && std::string(pr.name) != "Sub Saver"
            && std::string(pr.name) != "Warm Tube" && std::string(pr.name) != "Vamp Fuzz" && std::string(pr.name) != "Blown Speaker")
            rendered->push_back(std::vector<float>(o.begin(), o.begin() + n));
    }
}

static void writeWav(const char* path, const std::vector<float>& mono, int fs)
{
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    const uint32_t n = (uint32_t) mono.size(), dataBytes = n * 2 * 2;
    auto w32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(36 + dataBytes); std::fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(2); w32(fs); w32(fs * 4); w16(4); w16(16);
    std::fwrite("data", 1, 4, f); w32(dataBytes);
    for (float v : mono)
    {
        const int16_t s = (int16_t) std::lround(std::clamp(v, -1.f, 1.f) * 32767.f);
        std::fwrite(&s, 2, 1, f); std::fwrite(&s, 2, 1, f);
    }
    std::fclose(f);
}

int main(int argc, char** argv)
{
    testOversamplerLatencyAndGain();
    testCrossoverFlat();
    testEngineLatencyAlignment();
    testStability();
    testAliasing();

    std::vector<std::vector<float>> segs;
    const bool demo = argc >= 3 && std::strcmp(argv[1], "--demo") == 0;
    testPresets(demo ? &segs : nullptr);

    if (demo)
    {
        // clean, then 5 presets, each peak-normalised to -1 dBFS with a short gap
        std::vector<float> all;
        for (auto& s : segs)
        {
            float pk = 0; for (float v : s) pk = std::max(pk, std::fabs(v));
            const float g = pk > 0 ? 0.891f / pk : 1.f;
            for (float v : s) all.push_back(v * g);
            all.insert(all.end(), (size_t) (0.4 * 44100), 0.f);
        }
        writeWav(argv[2], all, 44100);
        std::printf("demo written: %s (%zu segments)\n", argv[2], segs.size());
    }

    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
