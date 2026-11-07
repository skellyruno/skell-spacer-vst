#pragma once
// Copyright 2026. Original research reference implementation.
// Allocation-free, causal note target decision model; NOT a pitch detector or shifter.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace skelly_research {
class NoteDecision final {
public:
    struct Result {
        int targetMidi = -1;
        bool changed = false;
        bool voiced = false;
        bool pending = false;
        int provisionalMidi = -1;
    };

    void prepare(double hostRate) noexcept {
        sampleRate_ = std::clamp(hostRate, 8000.0, 384000.0);
        reset();
    }
    void reset() noexcept {
        activeNote_ = -1;
        pendingNote_ = -1;
        pendingSamples_ = 0;
        centeredCandidateSamples_ = 0;
        untrustedSamples_ = 0;
    }
    // Invoke at detector hop boundaries. elapsedSamples is the number of HOST samples
    // since the last update, NOT the observation window duration.
    Result update(double f0Hz, double confidence, double onsetScore,
                  uint32_t elapsedSamples, uint16_t allowedPitchClasses = 0x0FFF,
                  double a4Hz = 440.0, bool aggressiveRetune = false) noexcept {
        const uint32_t delta = std::min<uint32_t>(elapsedSamples, 384000);
        confidence = std::clamp(std::isfinite(confidence) ? confidence : 0.0, 0.0, 1.0);
        onsetScore = std::clamp(std::isfinite(onsetScore) ? onsetScore : 0.0, 0.0, 1.0);
        const bool usable = std::isfinite(f0Hz) && f0Hz >= 50.0 && f0Hz <= 1500.0 &&
                            confidence >= 0.58 && (allowedPitchClasses & 0x0FFF) &&
                            std::isfinite(a4Hz) && a4Hz >= 400.0 && a4Hz <= 480.0;
        Result result{};
        if (!usable) {
            // Hold through a brief fricative or low-confidence estimate, but don't
            // latch a note indefinitely after silence.
            untrustedSamples_ = satAdd(untrustedSamples_, delta);
            pendingNote_ = -1;
            pendingSamples_ = 0;
            centeredCandidateSamples_ = 0;
            if (untrustedSamples_ >= toSamples(0.18)) {
                result.changed = (activeNote_ != -1);
                activeNote_ = -1;
            }
            result.targetMidi = activeNote_;
            result.voiced = false;
            return result;
        }
        // A confirmed new phrase after a genuinely unvoiced gap should not
        // inherit a stale target's 65ms confirmation delay. This path requires
        // both >=20ms of invalid tracking and a confident re-entry. It does
        // not change continuous legato/vibrato or passing-note behavior.
        const bool confidentReentry = untrustedSamples_ >= toSamples(0.020) &&
                                      confidence >= 0.82;
        untrustedSamples_ = 0;
        const int candidate = closestAllowed(f0Hz, allowedPitchClasses, a4Hz);
        // Explicit key/scale/individual-note edits invalidate any held target
        // immediately. Waiting the usual 65 ms confirmation can keep tuning to
        // a note that is no longer permitted by the user.
        const bool activeNowForbidden = activeNote_ >= 0 &&
            (allowedPitchClasses & (1u << (activeNote_ % 12))) == 0;
        if (activeNote_ == -1 || activeNowForbidden ||
            (confidentReentry && candidate != activeNote_)) {
            activeNote_ = candidate;
            pendingNote_ = -1;
            pendingSamples_ = 0;
            centeredCandidateSamples_ = 0;
            result.changed = true;
        } else if (candidate == activeNote_) {
            pendingNote_ = -1;
            pendingSamples_ = 0;
            centeredCandidateSamples_ = 0;
        } else {
            const bool strongNewOnset = onsetScore >= 0.72 && confidence >= 0.72;
            if (strongNewOnset) {
                activeNote_ = candidate;
                pendingNote_ = -1;
                pendingSamples_ = 0;
                centeredCandidateSamples_ = 0;
                result.changed = true;
            } else {
                if (pendingNote_ != candidate) {
                    pendingNote_ = candidate;
                    pendingSamples_ = delta;
                    centeredCandidateSamples_ = 0;
                } else {
                    pendingSamples_ = satAdd(pendingSamples_, delta);
                }
                // Hard tuning: an adjacent note that stabilizes *at its center*
                // is stronger evidence than merely crossing a note boundary.
                // A sustained 85-cent vibrato centered on the old note still
                // remains >=15 cents away from the adjacent note center.
                const double centerCents = std::abs(1200.0 *
                    std::log2(f0Hz / (a4Hz * std::pow(2.0, (candidate - 69) / 12.0))));
                if (aggressiveRetune && confidence >= 0.85 && centerCents < 11.0)
                    centeredCandidateSamples_ = satAdd(centeredCandidateSamples_, delta);
                else
                    centeredCandidateSamples_ = 0;
                const bool centeredAdjacent = std::abs(candidate - activeNote_) == 1 &&
                    centeredCandidateSamples_ >= toSamples(0.015);
                // Wait for evidence that persists beyond a short vibrato excursion;
                // strong onsets bypass this timeout, preserving genuine short notes.
                // v2.7: maximum-speed hard tuning must not inherit a 65 ms
                // musical-note hold. Large, high-confidence changes commit after
                // 12 ms of persistent evidence; adjacent semitone changes require
                // 32 ms unless accompanied by a genuine attack. Fast behavior is
                // enabled ONLY for explicit maximum-speed/full-wet settings.
                const int distance = std::abs(candidate - activeNote_);
                const double seconds = (aggressiveRetune && confidence >= 0.82)
                    ? (distance >= 2 ? 0.008 : 0.095) : 0.065;
                if ((centeredAdjacent || pendingSamples_ >= toSamples(seconds)) &&
                    confidence >= (aggressiveRetune ? 0.82 : 0.65)) {
                    activeNote_ = candidate;
                    pendingNote_ = -1;
                    pendingSamples_ = 0;
                    centeredCandidateSamples_ = 0;
                    result.changed = true;
                }
            }
        }
        result.targetMidi = activeNote_;
        result.voiced = true;
        result.pending = pendingNote_ != -1;
        // Optional caller-side preview target for *confirmed periodic* but
        // still pending large leaps. Never commits the underlying note state.
        if (result.pending && aggressiveRetune && confidence >= 0.92 &&
            std::abs(pendingNote_ - activeNote_) >= 2 &&
            pendingSamples_ >= toSamples(0.005))
            result.provisionalMidi = pendingNote_;
        return result;
    }
private:
    static uint32_t satAdd(uint32_t x, uint32_t y) noexcept {
        return (std::numeric_limits<uint32_t>::max() - x < y)
             ? std::numeric_limits<uint32_t>::max() : x + y;
    }
    uint32_t toSamples(double seconds) const noexcept {
        return static_cast<uint32_t>(std::ceil(sampleRate_ * seconds));
    }
    static int closestAllowed(double f0, uint16_t mask, double a4) noexcept {
        const double midi = 69.0 + 12.0 * std::log2(f0/a4);
        int best = -1;
        double bestError = 1000.0;
        const int center = static_cast<int>(std::round(midi));
        for (int n = std::max(0, center-12); n <= std::min(127, center+12); ++n) {
            if ((mask & (1u << (n % 12))) == 0) continue;
            const double error = std::abs(midi - n);
            if (error < bestError) { bestError = error; best = n; }
        }
        return best;
    }
    double sampleRate_ = 48000.0;
    int activeNote_ = -1;
    int pendingNote_ = -1;
    uint32_t pendingSamples_ = 0;
    uint32_t centeredCandidateSamples_ = 0;
    uint32_t untrustedSamples_ = 0;
};
} // namespace skelly_research
