#pragma once
#include "XefMeterCommon.h"
#include <array>

namespace xef
{
// ITU-R BS.1770-4 / EBU R128 loudness (stereo, L/R weights = 1.0).
//  - K-weighting: 2 biquads (high shelf + RLB high-pass), coefficients derived for the actual sample rate.
//  - Momentary  : 400 ms window, updated every 100 ms.
//  - Short-term : 3 s window, updated every 100 ms.
//  - Integrated : gated (absolute -70 LUFS, relative -10 LU) over 400 ms blocks with 75 % overlap.
//  - LRA        : EBU Tech 3342 style on 3 s short-term blocks (abs gate -70, rel gate -20 LU, P10..P95).
// Approximations (documented on purpose):
//  * Gating statistics use 0.1 LU histogram bins, so integrated/LRA carry up to ~0.05 LU quantisation.
//  * The 100 ms hop is rounded to an integer sample count.
//  * No true-peak measurement here (sample peak lives in LevelMeter).
// Everything is allocation-free after construction; reset is requested via an atomic and executed on the audio thread.
class LoudnessMeter
{
public:
    static constexpr int    kBins    = 801;     // -70.0 .. +10.0 LUFS in 0.1 LU steps
    static constexpr double kGateAbs = -70.0;

    LoudnessMeter() { publishInvalid(); }

    void prepare (double sampleRate)
    {
        hopSamples = std::max (1, (int) std::lround (sampleRate * 0.1));
        design (sampleRate);
        for (auto& ch : st) for (auto& s : ch) s = {};
        resetStats();
    }

    void requestReset() noexcept { resetRequested.store (true); }

    void process (const float* l, const float* r, int n) noexcept
    {
        if (resetRequested.exchange (false)) resetStats();
        for (int i = 0; i < n; ++i)
        {
            const double xl = kWeight (0, l[i]);
            const double xr = kWeight (1, r[i]);
            acc += xl * xl + xr * xr;
            if (++hopCounter >= hopSamples) { onHop (acc / hopSamples); acc = 0.0; hopCounter = 0; }
        }
    }

    // LUFS / LU values. kFloorDb (-120) means "not available yet / silence".
    float getMomentary()  const noexcept { return momentary.load  (std::memory_order_relaxed); }
    float getShortTerm()  const noexcept { return shortTerm.load  (std::memory_order_relaxed); }
    float getIntegrated() const noexcept { return integrated.load (std::memory_order_relaxed); }
    float getLra()        const noexcept { return lra.load        (std::memory_order_relaxed); }

private:
    struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct Z      { double z1 = 0, z2 = 0; };
    struct Histogram
    {
        std::array<double, kBins>   e {};
        std::array<uint32_t, kBins> c {};
        void clear() noexcept { e.fill (0.0); c.fill (0); }
        void add (double lufs, double energy) noexcept
        {
            int i = (int) std::lround ((lufs - kGateAbs) * 10.0);
            i = std::max (0, std::min (kBins - 1, i));
            e[(size_t) i] += energy; ++c[(size_t) i];
        }
    };

    static double lufsOf (double energy) noexcept { return energy > 1.0e-10 ? -0.691 + 10.0 * std::log10 (energy) : (double) kFloorDb; }
    static double binLufs (int i) noexcept { return kGateAbs + 0.1 * i; }

    void design (double fs)
    {
        {   // stage 1: high shelf
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPi * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            c1.b0 = (Vh + Vb * K / Q + K * K) / a0;  c1.b1 = 2.0 * (K * K - Vh) / a0;  c1.b2 = (Vh - Vb * K / Q + K * K) / a0;
            c1.a1 = 2.0 * (K * K - 1.0) / a0;        c1.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {   // stage 2: RLB high-pass
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (kPi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            c2.b0 = 1.0; c2.b1 = -2.0; c2.b2 = 1.0;
            c2.a1 = 2.0 * (K * K - 1.0) / a0;  c2.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }

    static double run (const Coeffs& c, Z& z, double x) noexcept   // transposed direct form II
    {
        const double y = c.b0 * x + z.z1;
        z.z1 = c.b1 * x - c.a1 * y + z.z2;
        z.z2 = c.b2 * x - c.a2 * y;
        return y;
    }
    double kWeight (int ch, float x) noexcept { return run (c2, st[ch][1], run (c1, st[ch][0], (double) sanitize (x))); }

    void onHop (double hopEnergy) noexcept
    {
        hop[(size_t) hopIdx] = hopEnergy;
        hopIdx = (hopIdx + 1) % 30;
        hopsFilled = std::min (hopsFilled + 1, 30);

        auto meanLast = [this] (int count) { double s = 0; for (int k = 0; k < count; ++k) s += hop[(size_t) ((hopIdx - 1 - k + 60) % 30)]; return s / count; };

        if (hopsFilled >= 4)
        {
            const double e = meanLast (4), L = lufsOf (e);
            momentary.store ((float) L, std::memory_order_relaxed);
            if (L > kGateAbs) histInt.add (L, e);
            integrated.store ((float) computeIntegrated(), std::memory_order_relaxed);
        }
        if (hopsFilled >= 30)
        {
            const double e = meanLast (30), L = lufsOf (e);
            shortTerm.store ((float) L, std::memory_order_relaxed);
            if (L > kGateAbs) histLra.add (L, e);
            lra.store ((float) computeLra(), std::memory_order_relaxed);
        }
    }

    double computeIntegrated() const noexcept
    {
        double sum = 0; uint64_t cnt = 0;
        for (int i = 0; i < kBins; ++i) { sum += histInt.e[(size_t) i]; cnt += histInt.c[(size_t) i]; }
        if (cnt == 0) return kFloorDb;
        const double rel = lufsOf (sum / (double) cnt) - 10.0;
        double e2 = 0; uint64_t c2n = 0;
        for (int i = 0; i < kBins; ++i) if (binLufs (i) >= rel) { e2 += histInt.e[(size_t) i]; c2n += histInt.c[(size_t) i]; }
        return c2n ? lufsOf (e2 / (double) c2n) : (double) kFloorDb;
    }

    double computeLra() const noexcept
    {
        double sum = 0; uint64_t cnt = 0;
        for (int i = 0; i < kBins; ++i) { sum += histLra.e[(size_t) i]; cnt += histLra.c[(size_t) i]; }
        if (cnt < 2) return 0.0;
        const double rel = lufsOf (sum / (double) cnt) - 20.0;
        uint64_t g = 0;
        for (int i = 0; i < kBins; ++i) if (binLufs (i) >= rel) g += histLra.c[(size_t) i];
        if (g < 2) return 0.0;
        const uint64_t t10 = (uint64_t) std::ceil (0.10 * (double) g), t95 = (uint64_t) std::ceil (0.95 * (double) g);
        uint64_t cum = 0; double lo = 0, hi = 0; bool gotLo = false;
        for (int i = 0; i < kBins; ++i)
        {
            if (binLufs (i) < rel) continue;
            cum += histLra.c[(size_t) i];
            if (! gotLo && cum >= t10) { lo = binLufs (i); gotLo = true; }
            if (cum >= t95) { hi = binLufs (i); break; }
        }
        return std::max (0.0, hi - lo);
    }

    void resetStats() noexcept
    {
        acc = 0; hopCounter = 0; hopIdx = 0; hopsFilled = 0; hop.fill (0.0);
        histInt.clear(); histLra.clear();
        publishInvalid();
    }
    void publishInvalid() noexcept
    {
        momentary.store (kFloorDb); shortTerm.store (kFloorDb); integrated.store (kFloorDb); lra.store (0.0f);
    }

    Coeffs c1, c2;  Z st[2][2];
    int hopSamples = 4800, hopCounter = 0, hopIdx = 0, hopsFilled = 0;
    double acc = 0.0;
    std::array<double, 30> hop {};
    Histogram histInt, histLra;
    std::atomic<float> momentary { kFloorDb }, shortTerm { kFloorDb }, integrated { kFloorDb }, lra { 0.0f };
    std::atomic<bool>  resetRequested { false };
};
} // namespace xef