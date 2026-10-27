#pragma once
#include "XefMeterCommon.h"

namespace xef
{
// Stereo correlation (Pearson, zero-lag) between L and R, smoothed with an exponential window (default 300 ms).
//   +1 = identical (mono), 0 = uncorrelated, -1 = anti-phase.
// Silence / near-silence / a digitally silent channel make the value undefined: isValid() is false and getValue() is 0,
// so the UI can show "--" instead of a misleading number. This meter never alters the audio.
class CorrelationMeter
{
public:
    void prepare (double sr) noexcept { fs = sr; mLL = mRR = mLR = 0.0; value.store (0.0f); valid.store (false); }
    void setWindowMs (float ms) noexcept { tau = std::max (20.0f, ms) * 0.001; }

    void process (const float* l, const float* r, int n) noexcept
    {
        if (n <= 0) return;
        double sLL = 0, sRR = 0, sLR = 0;
        for (int i = 0; i < n; ++i)
        {
            const double a = sanitize (l[i]), b = sanitize (r[i]);
            sLL += a * a; sRR += b * b; sLR += a * b;
        }
        const double alpha = std::exp (-(double) n / (fs * tau)), k = 1.0 - alpha;
        mLL = alpha * mLL + k * sLL / n;
        mRR = alpha * mRR + k * sRR / n;
        mLR = alpha * mLR + k * sLR / n;

        const bool ok = (mLL + mRR) > 1.0e-7 && mLL > 1.0e-12 && mRR > 1.0e-12;   // ~ -70 dBFS combined energy
        float v = 0.0f;
        if (ok) v = (float) std::max (-1.0, std::min (1.0, mLR / std::sqrt (mLL * mRR)));
        value.store (v, std::memory_order_relaxed);
        valid.store (ok, std::memory_order_relaxed);
    }

    float getValue() const noexcept { return value.load (std::memory_order_relaxed); }
    bool  isValid()  const noexcept { return valid.load (std::memory_order_relaxed); }

private:
    double fs = 48000.0, tau = 0.3, mLL = 0, mRR = 0, mLR = 0;
    std::atomic<float> value { 0.0f };
    std::atomic<bool>  valid { false };
};
} // namespace xef