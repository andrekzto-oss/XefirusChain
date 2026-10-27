#pragma once
#include "XefMeterCommon.h"
#include <array>

namespace xef
{
//==============================================================================
// Audio-thread side: pushes a mono mix ((L+R)/2) into a lock-free ring. No allocation, no locks. Drops samples if the UI lags.
class SpectrumTap
{
public:
    void init (size_t capacityPow2 = 1u << 15) { ring.init (capacityPow2); }
    void push (const float* l, const float* r, int n) noexcept
    {
        const size_t m = std::min ((size_t) n, ring.beginWrite());
        for (size_t i = 0; i < m; ++i) ring.put (0.5f * (sanitize (l[i]) + sanitize (r[i])));
        ring.commit();
    }
    SpscRing ring;
};

// Audio-thread side: interleaved L,R frames for the goniometer.
class ScopeTap
{
public:
    void init (size_t capacityPow2 = 1u << 14) { ring.init (capacityPow2); }
    void push (const float* l, const float* r, int n) noexcept
    {
        const size_t frames = std::min ((size_t) n, ring.beginWrite() / 2);
        for (size_t i = 0; i < frames; ++i) { ring.put (sanitize (l[i])); ring.put (sanitize (r[i])); }
        ring.commit();
    }
    SpscRing ring;
};

//==============================================================================
// UI-thread side. Pulls samples from a tap, runs a Hann-windowed FFT and maps it to kPoints log-spaced points
// between 20 Hz and 20 kHz in dBFS (a full-scale sine reads 0 dBFS at its peak bin).
// Per display point: the maximum bin inside the band (peaks stay visible at HF), linear dB interpolation where
// a point is narrower than one FFT bin (LF). Averaging is done in the dB domain (an approximation of power averaging).
// All memory is allocated in prepare()/setFftOrder(), which run on the UI thread.
class SpectrumProcessor
{
public:
    static constexpr int   kPoints = 240;
    static constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f, kFloorDbfs = -120.0f;

    void prepare (double sr, int fftOrder = 12) { sampleRate = sr; scratch.assign (4096, 0.0f); setFftOrder (fftOrder, true); }

    void setFftOrder (int order, bool force = false)
    {
        order = std::max (9, std::min (14, order));
        if (! force && order == fftOrder) return;
        fftOrder = order; N = 1 << order;
        history.assign ((size_t) N, 0.0f);
        re.assign ((size_t) N, 0.0); im.assign ((size_t) N, 0.0);
        db.assign ((size_t) N / 2 + 2, kFloorDbfs);
        window.resize ((size_t) N); cosT.resize ((size_t) N / 2); sinT.resize ((size_t) N / 2); rev.resize ((size_t) N);
        for (int i = 0; i < N; ++i) window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * i / N);   // periodic Hann (sum = N/2)
        for (int i = 0; i < N / 2; ++i) { cosT[(size_t) i] = std::cos (2.0 * kPi * i / N); sinT[(size_t) i] = std::sin (2.0 * kPi * i / N); }
        for (int i = 0; i < N; ++i) { int r = 0; for (int b = 0; b < order; ++b) if (i & (1 << b)) r |= 1 << (order - 1 - b); rev[(size_t) i] = r; }
        buildMap();
        resetTraces();
    }

    void setAveraging (float a) noexcept { averaging = std::max (0.0f, std::min (0.98f, a)); }   // 0 = none
    void setFallDbPerFrame (float d) noexcept { fall = std::max (0.1f, d); }
    void setHoldFallDbPerFrame (float d) noexcept { holdFall = std::max (0.0f, d); }
    void resetHold() noexcept { holdTrace.fill (kFloorDbfs); }
    void resetTraces() noexcept { avg.fill (kFloorDbfs); trace.fill (kFloorDbfs); holdTrace.fill (kFloorDbfs); }

    // Returns true if new audio was analysed.
    bool update (SpscRing& ring)
    {
        size_t avail = ring.available();
        if (avail == 0 || N == 0) return false;
        while (avail > 0)
        {
            const size_t m = ring.read (scratch.data(), std::min (avail, scratch.size()));
            if (m == 0) break;
            pushHistory (scratch.data(), m);
            avail -= m;
        }
        analyse();
        return true;
    }

    const std::array<float, kPoints>& getTrace() const noexcept { return trace; }
    const std::array<float, kPoints>& getHold()  const noexcept { return holdTrace; }
    int getFftSize() const noexcept { return N; }
    static float pointFrequency (int p) noexcept { return kMinHz * std::pow (kMaxHz / kMinHz, ((float) p + 0.5f) / (float) kPoints); }

    // Exposed for tests: raw dBFS of one FFT bin after the last update().
    float getBinDb (int k) const noexcept { return db[(size_t) k]; }

private:
    void pushHistory (const float* src, size_t m)
    {
        const size_t n = (size_t) N;
        if (m >= n) { std::copy (src + (m - n), src + m, history.begin()); return; }
        std::copy (history.begin() + (long) m, history.end(), history.begin());
        std::copy (src, src + m, history.end() - (long) m);
    }

    void fft()
    {
        for (int i = 0; i < N; ++i) { const int j = rev[(size_t) i]; if (j > i) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); } }
        for (int len = 2; len <= N; len <<= 1)
        {
            const int half = len / 2, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int k = 0; k < half; ++k)
                {
                    const double wr = cosT[(size_t) (k * step)], wi = -sinT[(size_t) (k * step)];
                    const size_t a = (size_t) (i + k), b = a + (size_t) half;
                    const double tr = wr * re[b] - wi * im[b], ti = wr * im[b] + wi * re[b];
                    re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
                }
        }
    }

    void analyse()
    {
        for (int i = 0; i < N; ++i) { re[(size_t) i] = (double) history[(size_t) i] * window[(size_t) i]; im[(size_t) i] = 0.0; }
        fft();
        const double norm = 4.0 / N;                  // sine of amplitude A -> |X| = A*N/4 with a Hann window
        for (int k = 0; k <= N / 2; ++k)
        {
            const double mag = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]) * norm;
            db[(size_t) k] = ampToDb ((float) mag);
        }
        db[(size_t) N / 2 + 1] = db[(size_t) N / 2];

        for (int p = 0; p < kPoints; ++p)
        {
            float v;
            if (k1[(size_t) p] >= k0[(size_t) p])
            {
                v = kFloorDbfs;
                for (int k = k0[(size_t) p]; k <= k1[(size_t) p]; ++k) v = std::max (v, db[(size_t) k]);
            }
            else
            {
                const int k = (int) pos[(size_t) p]; const float f = (float) (pos[(size_t) p] - k);
                v = db[(size_t) k] * (1.0f - f) + db[(size_t) k + 1] * f;
            }
            avg[(size_t) p] = averaging * avg[(size_t) p] + (1.0f - averaging) * v;
            trace[(size_t) p] = avg[(size_t) p] > trace[(size_t) p] ? avg[(size_t) p] : std::max (avg[(size_t) p], trace[(size_t) p] - fall);
            holdTrace[(size_t) p] = std::max (trace[(size_t) p], holdTrace[(size_t) p] - holdFall);
        }
    }

    void buildMap()
    {
        const int maxBin = N / 2;
        for (int p = 0; p < kPoints; ++p)
        {
            const double f0 = kMinHz * std::pow ((double) kMaxHz / kMinHz, (double) p / kPoints);
            const double f1 = kMinHz * std::pow ((double) kMaxHz / kMinHz, (double) (p + 1) / kPoints);
            const double b0 = f0 * N / sampleRate, b1 = f1 * N / sampleRate;
            k0[(size_t) p] = std::max (1, std::min (maxBin, (int) std::ceil (b0)));
            k1[(size_t) p] = std::max (0, std::min (maxBin, (int) std::floor (b1)));
            pos[(size_t) p] = std::max (1.0, std::min ((double) maxBin - 1.0, 0.5 * (b0 + b1)));
        }
    }

    double sampleRate = 48000.0;
    int fftOrder = 0, N = 0;
    float averaging = 0.5f, fall = 1.0f, holdFall = 0.15f;
    std::vector<float>  scratch, history, db;
    std::vector<double> re, im, window, cosT, sinT;
    std::vector<int>    rev;
    std::array<int, kPoints>    k0 {}, k1 {};
    std::array<double, kPoints> pos {};
    std::array<float, kPoints>  avg {}, trace {}, holdTrace {};
};
} // namespace xef