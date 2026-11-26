/**
 * @file util.h
 * Small numeric & math helpers shared by UI animations, gauge interpolation, and telemetry smoothing.
 */
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace core {

template <typename T>
constexpr T clamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

constexpr float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/** Maps `v` from [in_lo, in_hi] to [out_lo, out_hi] with clamping. */
inline float map_clamped(float v, float in_lo, float in_hi, float out_lo, float out_hi) {
    if (in_hi == in_lo) return out_lo;
    const float t = clamp((v - in_lo) / (in_hi - in_lo), 0.0f, 1.0f);
    return lerp(out_lo, out_hi, t);
}

/** First order low-pass filter step. `alpha` in (0, 1], 1 = no filtering. */
inline float ema(float state, float sample, float alpha) {
    return state + alpha * (sample - state);
}

/** Exponential smoothing factor for a time constant `tau_ms` and step `dt_ms`. */
inline float ema_alpha(uint32_t dt_ms, uint32_t tau_ms) {
    if (tau_ms == 0) return 1.0f;
    return 1.0f - expf(-static_cast<float>(dt_ms) / static_cast<float>(tau_ms));
}

/** Elapsed milliseconds, safe across 32-bit wrap. */
constexpr uint32_t elapsed(uint32_t now, uint32_t since) {
    return now - since;
}

/**
 * Rounds a range outwards to "nice" round numbers for stable chart axis scaling.
 */
inline void nice_range(float lo, float hi, int32_t& out_lo, int32_t& out_hi, float minBound = -9999.0f, float maxBound = 9999.0f) {
    float span = hi - lo;
    if (span < 1.0f) span = 1.0f;
    const float magnitude = powf(10.0f, floorf(log10f(span)));
    const float step = (span / magnitude < 3.0f) ? (magnitude / 2.0f) : magnitude;
    out_lo = static_cast<int32_t>(floorf((lo - span * 0.1f) / step) * step);
    out_hi = static_cast<int32_t>(ceilf((hi + span * 0.1f) / step) * step);
    if (minBound > -9990.0f && out_lo < static_cast<int32_t>(minBound)) {
        out_lo = static_cast<int32_t>(minBound);
    }
    if (maxBound < 9990.0f && out_hi > static_cast<int32_t>(maxBound)) {
        out_hi = static_cast<int32_t>(maxBound);
    }
    if (out_hi <= out_lo) out_hi = out_lo + 1;
}

/**
 * Vehicle UI safety policy: Determines if stationary and parked safely for menu interaction.
 *
 * Rules:
 * 1. Speed must be 0 km/h. If moving (> 0), always false (menu hidden, touch locked).
 * 2. If vehicle reports gear ('P', 'R', 'N', 'D'):
 *    Safe if gear is 'P' or 'N' OR handbrake is active.
 * 3. If vehicle lacks gear PID (gear is ' ' or '\0'):
 *    Fallback: handbrake is active OR vehicle is stationary for >= 3000 ms.
 */
inline bool is_safe_to_operate_ui(uint8_t speedKmh, char gear, bool handbrakeActive, uint32_t stillDurationMs) {
    if (speedKmh > 0) return false;

    const bool hasGear = (gear != ' ' && gear != '\0');
    if (hasGear) {
        return (gear == 'P' || gear == 'N' || handbrakeActive);
    }
    return handbrakeActive || (stillDurationMs >= 3000);
}

struct DrivingLockState {
    bool isLocked = false;
    uint32_t motionStartMs = 0;
    uint32_t stationaryStartMs = 0;
    uint32_t lockFlashUntilMs = 0;

    bool update(uint8_t speedKmh, uint32_t nowMs) {
        if (speedKmh >= 5) {
            stationaryStartMs = 0;
            if (motionStartMs == 0) {
                motionStartMs = nowMs;
            } else if (nowMs - motionStartMs >= 2000) {
                isLocked = true;
            }
        } else if (speedKmh == 0) {
            motionStartMs = 0;
            if (stationaryStartMs == 0) {
                stationaryStartMs = nowMs;
            } else if (nowMs - stationaryStartMs >= 3000) {
                isLocked = false;
            }
        } else {
            stationaryStartMs = 0;
        }
        return isLocked;
    }

    void triggerTouchAttempt(uint32_t nowMs) {
        if (isLocked) {
            lockFlashUntilMs = nowMs + 600;
        }
    }

    bool isFlashing(uint32_t nowMs) const {
        return (nowMs < lockFlashUntilMs);
    }
};

}  // namespace core
