// XEFIRUS CHAIN - metering core (std-only, no JUCE dependency, so it can be unit-tested stand-alone)
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace xef
{
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr float  kFloorDb = -120.0f;   // "silence" sentinel shown as -inf in the UI

inline float ampToDb (float a) noexcept   { return a > 1.0e-6f ? 20.0f * std::log10 (a) : kFloorDb; }
inline float powerToDb (double p) noexcept { return p > 1.0e-12 ? (float) (10.0 * std::log10 (p)) : kFloorDb; }
inline float dbToAmp (float db) noexcept   { return std::pow (10.0f, db * 0.05f); }
inline float sanitize (float x) noexcept   { return std::isfinite (x) ? x : 0.0f; }

//==============================================================================
// Single-producer / single-consumer float ring. Producer = audio thread, consumer = UI thread.
// Storage is allocated in init() (never on the audio thread). Never blocks; a full ring drops samples.
class SpscRing
{
public:
    void init (size_t capacityPow2)
    {
        cap = capacityPow2; mask = cap - 1; data.assign (cap, 0.0f);
        head.store (0); tail.store (0);
    }

    // ---- producer (audio thread)
    size_t beginWrite() noexcept
    {
        wpos = head.load (std::memory_order_relaxed);
        return cap - (wpos - tail.load (std::memory_order_acquire));
    }
    void put (float v) noexcept { data[wpos++ & mask] = v; }
    void commit() noexcept      { head.store (wpos, std::memory_order_release); }

    // ---- consumer (UI thread)
    size_t available() const noexcept { return head.load (std::memory_order_acquire) - tail.load (std::memory_order_relaxed); }
    size_t read (float* dst, size_t n) noexcept
    {
        const size_t t = tail.load (std::memory_order_relaxed);
        n = std::min (n, available());
        for (size_t i = 0; i < n; ++i) dst[i] = data[(t + i) & mask];
        tail.store (t + n, std::memory_order_release);
        return n;
    }
    void skip (size_t n) noexcept { tail.store (tail.load (std::memory_order_relaxed) + std::min (n, available()), std::memory_order_release); }
    void discardAll() noexcept    { tail.store (head.load (std::memory_order_acquire), std::memory_order_release); }

private:
    std::vector<float> data;
    size_t cap = 0, mask = 0, wpos = 0;
    std::atomic<size_t> head { 0 }, tail { 0 };
};
} // namespace xef