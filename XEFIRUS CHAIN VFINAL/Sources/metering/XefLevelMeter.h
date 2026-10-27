#pragma once
#include "XefMeterCommon.h"
#include <array>

namespace xef
{
// Stereo sample-peak + RMS meter (dBFS). NOT true-peak, NOT loudness.
//  - Peak:  per-block sample peak, instant attack, release in dB/s (configurable), separate hold value.
//  - RMS:   exponentially-smoothed mean square (time constant configurable, default 300 ms).
//           Plain RMS: a full-scale sine reads -3.01 dBFS (no +3 dB calibration offset).
//  - Clip:  latched when a sample reaches >= 0.99999 (cleared by reset).
// process() is allocation-free and lock-free. Results are published through atomics.
class LevelMeter
{
public:
    LevelMeter() { reset(); }

    void prepare (double sr) noexcept { sampleRate = sr; reset(); }
    void setBallistics (float rmsWindowMsIn, float peakReleaseDbPerSecIn) noexcept
    {
        rmsWindowMs.store (std::max (10.0f, rmsWindowMsIn));
        peakReleaseDbPerSec.store (std::max (1.0f, peakReleaseDbPerSecIn));
    }
    void requestHoldReset() noexcept { holdReset.store (true); }

    void process (const float* l, const float* r, int n) noexcept
    {
        if (n <= 0) return;
        if (holdReset.exchange (false))
            for (int c = 0; c < 2; ++c) { st[c].hold = 0.0f; st[c].clipped = false; }

        const float* in[2] = { l, r };
        const double tau   = rmsWindowMs.load (std::memory_order_relaxed) * 0.001;
        const float  decay = std::pow (10.0f, -peakReleaseDbPerSec.load (std::memory_order_relaxed) * (float) n / (float) sampleRate / 20.0f);
        const double alpha = std::exp (-(double) n / (sampleRate * tau));

        for (int c = 0; c < 2; ++c)
        {
            float pk = 0.0f; double sumSq = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const float x = sanitize (in[c][i]);
                pk = std::max (pk, std::fabs (x));
                sumSq += (double) x * x;
            }
            auto& s = st[c];
            s.peakDisp = std::max (pk, s.peakDisp * decay);
            s.ms       = alpha * s.ms + (1.0 - alpha) * (sumSq / n);
            s.hold     = std::max (s.hold, pk);
            if (pk >= 0.99999f) s.clipped = true;

            peakDb[c].store (ampToDb (s.peakDisp), std::memory_order_relaxed);
            rmsDb[c].store  (powerToDb (s.ms),     std::memory_order_relaxed);
            holdDb[c].store (ampToDb (s.hold),     std::memory_order_relaxed);
            clip[c].store   (s.clipped,            std::memory_order_relaxed);
        }
    }

    std::array<std::atomic<float>, 2> peakDb, rmsDb, holdDb;
    std::array<std::atomic<bool>, 2>  clip;

private:
    struct State { float peakDisp = 0, hold = 0; double ms = 0; bool clipped = false; };
    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            st[c] = {};
            peakDb[c].store (kFloorDb); rmsDb[c].store (kFloorDb); holdDb[c].store (kFloorDb); clip[c].store (false);
        }
    }

    State st[2];
    double sampleRate = 48000.0;
    std::atomic<float> rmsWindowMs { 300.0f }, peakReleaseDbPerSec { 20.0f };
    std::atomic<bool>  holdReset { false };
};
} // namespace xef