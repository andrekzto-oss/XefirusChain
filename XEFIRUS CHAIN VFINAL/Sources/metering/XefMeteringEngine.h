#pragma once
#include "XefCorrelationMeter.h"
#include "XefLevelMeter.h"
#include "XefLoudnessMeter.h"
#include "XefSpectrum.h"
#include <array>

namespace xef
{
//==============================================================================
// UI-adjustable monitoring settings. Plain atomics: the UI writes, the audio thread reads. Persist them in your plugin state.
struct MeteringSettings
{
    std::atomic<float> lufsTarget { -14.0f };          // monitoring reference only - the plugin never "aims" at it
    std::atomic<float> rmsWindowMs { 300.0f }, peakReleaseDbPerSec { 20.0f };
    std::atomic<float> spectrumAveraging { 0.5f }, spectrumFallDb { 1.0f }, gonioIntensity { 0.6f };
    std::atomic<int>   fftOrder { 12 };                // 11 = 2048, 12 = 4096, 13 = 8192
    std::atomic<bool>  peakHoldTrace { true }, inputOverlay { true }, collapsed { false };
    std::atomic<bool>  levelMatch { false }, monoMonitor { false };
};

//==============================================================================
// Gain reduction values reported by the REAL DSP stages (positive dB = amount of reduction).
// Call from the audio thread once per block with the smallest linear gain that stage applied in that block.
struct GainReductionState
{
    GainReductionState() { compDb = 0; limiterDb = 0; numBands = 0; for (auto& b : bandDb) b = 0; resetCounter = 0; }
    static float gainToGrDb (float minGainLinear) noexcept { return minGainLinear >= 1.0f ? 0.0f : -20.0f * std::log10 (std::max (minGainLinear, 1.0e-6f)); }

    void reportCompressor (float minGainLinear) noexcept { compDb.store (gainToGrDb (minGainLinear), std::memory_order_relaxed); }
    void reportLimiter    (float minGainLinear) noexcept { limiterDb.store (gainToGrDb (minGainLinear), std::memory_order_relaxed); }
    void reportBand (int band, float minGainLinear) noexcept
    {
        if (band < 0 || band >= 4) return;
        bandDb[(size_t) band].store (gainToGrDb (minGainLinear), std::memory_order_relaxed);
        int n = numBands.load (std::memory_order_relaxed); if (band + 1 > n) numBands.store (band + 1, std::memory_order_relaxed);
    }

    std::atomic<float> compDb, limiterDb;
    std::array<std::atomic<float>, 4> bandDb;
    std::atomic<int> numBands, resetCounter;
};

//==============================================================================
class MeteringEngine
{
public:
    MeteringSettings   settings;
    GainReductionState gr;
    LevelMeter         inLevel, outLevel;
    LoudnessMeter      inLoud, outLoud;     // outLoud is measured BEFORE bypass / level-match so matching never feeds back
    CorrelationMeter   corr;
    SpectrumTap        inSpec, outSpec;
    ScopeTap           scope;
    std::atomic<bool>  duckRequested { false }, duckAck { false };
    std::atomic<double> sampleRate { 48000.0 };

    // Call from prepareToPlay (message thread / not in the audio callback): this is where memory is allocated.
    void prepare (double sr)
    {
        sampleRate.store (sr); fs = sr;
        inLevel.prepare (sr); outLevel.prepare (sr); inLoud.prepare (sr); outLoud.prepare (sr); corr.prepare (sr);
        inSpec.init(); outSpec.init(); scope.init();
        matchDb = 0.0;
    }

    // ---- audio thread, in this order (see INTEGRATION.md):
    void measureInput  (const float* l, const float* r, int n) noexcept   // before the mastering DSP
    {
        syncSettings();
        inLevel.process (l, r, n); inLoud.process (l, r, n); inSpec.push (l, r, n);
    }
    void measureWet    (const float* l, const float* r, int n) noexcept { outLoud.process (l, r, n); }   // after DSP
    void measureOutput (const float* l, const float* r, int n) noexcept   // after bypass / match / duck
    {
        outLevel.process (l, r, n); corr.process (l, r, n); outSpec.push (l, r, n); scope.push (l, r, n);
    }

    // Slowly-slewed gain (dB) that makes the output short-term loudness equal the input's. Monitoring aid only (default off, +/-12 dB max).
    float updateMatchGainDb (int n) noexcept
    {
        double target = 0.0;
        if (settings.levelMatch.load (std::memory_order_relaxed))
        {
            const float i = inLoud.getShortTerm(), o = outLoud.getShortTerm();
            if (i > -70.0f && o > -70.0f) target = std::max (-12.0, std::min (12.0, (double) (i - o)));
        }
        matchDb += (target - matchDb) * (1.0 - std::exp (-(double) n / (fs * 1.0)));
        return (float) matchDb;
    }

    // ---- UI thread
    void resetLoudness() noexcept { inLoud.requestReset(); outLoud.requestReset(); }
    void resetHolds() noexcept    { inLevel.requestHoldReset(); outLevel.requestHoldReset(); gr.resetCounter.fetch_add (1); }

private:
    void syncSettings() noexcept
    {
        const float w = settings.rmsWindowMs.load (std::memory_order_relaxed), rel = settings.peakReleaseDbPerSec.load (std::memory_order_relaxed);
        inLevel.setBallistics (w, rel); outLevel.setBallistics (w, rel);
    }
    double fs = 48000.0, matchDb = 0.0;
};

//==============================================================================
// Final output stage: delay-compensated global bypass crossfade, A/B "duck", level-match gain and mono monitor,
// every transition ramped (no clicks). Allocation happens in prepare() only.
class OutputStage
{
public:
    void prepare (double sr, int maxBlock, int latencySamples)
    {
        fs = sr; cap = maxBlock; lat = std::max (0, latencySamples);
        for (int c = 0; c < 2; ++c) { dry[c].assign ((size_t) maxBlock, 0.0f); dly[c].assign ((size_t) std::max (1, lat), 0.0f); }
        pos = 0; bypassMix = { 0.0f, 0.0f, 0.0f }; duckG = { 1.0f, 1.0f, 0.0f }; matchG = { 1.0f, 1.0f, 0.0f }; monoMix = { 0.0f, 0.0f, 0.0f };
    }

    // Call BEFORE the mastering DSP, with the untouched input: stores latency-aligned dry signal.
    void pushDry (const float* l, const float* r, int n) noexcept
    {
        if (n > cap) return;
        const float* in[2] = { l, r };
        for (int c = 0; c < 2; ++c)
        {
            if (lat == 0) { std::copy (in[c], in[c] + n, dry[c].begin()); continue; }
            int p = pos;
            for (int i = 0; i < n; ++i) { dry[c][(size_t) i] = dly[c][(size_t) p]; dly[c][(size_t) p] = in[c][i]; if (++p >= lat) p = 0; }
        }
        if (lat > 0) pos = (pos + n) % lat;
    }

    // Call AFTER the mastering DSP. Returns true when the output is fully ducked (acknowledges an A/B swap request).
    bool apply (float* l, float* r, int n, bool bypass, bool duck, float matchDb) noexcept
    {
        if (n > cap) return false;
        const int rampMs8 = std::max (1, (int) (fs * 0.008)), rampMs10 = std::max (1, (int) (fs * 0.010));
        bypassMix.set (bypass ? 1.0f : 0.0f, rampMs10);
        duckG.set (duck ? 0.0f : 1.0f, rampMs8);
        matchG.set (dbToAmp (matchDb), n);
        for (int i = 0; i < n; ++i)
        {
            const float m = bypassMix.next(), d = duckG.next(), g = matchG.next();
            l[i] = (l[i] * g * (1.0f - m) + dry[0][(size_t) i] * m) * d;
            r[i] = (r[i] * g * (1.0f - m) + dry[1][(size_t) i] * m) * d;
        }
        return duckG.cur == 0.0f;
    }

    // Call LAST (after measureOutput): monitor-only mono fold-down with a short ramp.
    void applyMonoMonitor (float* l, float* r, int n, bool mono) noexcept
    {
        monoMix.set (mono ? 1.0f : 0.0f, std::max (1, (int) (fs * 0.010)));
        for (int i = 0; i < n; ++i)
        {
            const float m = monoMix.next(), mid = 0.5f * (l[i] + r[i]);
            l[i] += (mid - l[i]) * m; r[i] += (mid - r[i]) * m;
        }
    }

private:
    struct Ramp
    {
        float cur, tgt, step;
        void set (float t, int samples) noexcept { if (t == tgt) return; tgt = t; step = (tgt - cur) / (float) std::max (1, samples); }
        float next() noexcept
        {
            if (cur != tgt) { cur += step; if ((step >= 0.0f && cur >= tgt) || (step < 0.0f && cur <= tgt)) cur = tgt; }
            return cur;
        }
    };
    double fs = 48000.0; int cap = 0, lat = 0, pos = 0;
    std::vector<float> dry[2], dly[2];
    Ramp bypassMix { 0, 0, 0 }, duckG { 1, 1, 0 }, matchG { 1, 1, 0 }, monoMix { 0, 0, 0 };
};
} // namespace xef