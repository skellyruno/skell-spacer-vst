#pragma once
#include <algorithm>
#include <cmath>

namespace skelly {
// What the host may compensate, rather than an estimate of the ages of samples
// recycled by a periodic pitch shifter.  Both modes share one fixed dry/transient
// reference, while their continuous tonal output is time-warped and therefore
// has NO single transport delay.  Never use source-cycle age as host PDC.
struct DelayContract {
    int reportedSamples = 0;
    bool drySampleExact = true;
    bool transientAnchored = true;
    bool fixedWetWaveformDelay = false;
};
// A second STUDIO product offers a shorter fixed timebase without changing
// an existing plugin's identity or its saved-session latency behavior.
// This 40 ms profile uses unchanged full-period analysis/synthesis windows.
inline DelayContract studioFastDelayContract(double sampleRate) noexcept {
    const double fs = std::clamp(std::isfinite(sampleRate) ? sampleRate : 48000.0,
                                 8000.0, 192000.0);
    return {int(std::lround(fs * 0.040)), true, true, false};
}
// Separate STUDIO plugin instance: fixed synthesis timeline, 50 ms host PDC.
// The PSOLA grains are anchored to this timeline within one pitch-mark search
// radius; tonal waveform samples are time-warped, not simple delayed copies.
inline DelayContract studioDelayContract(double sampleRate) noexcept {
    const double fs = std::clamp(std::isfinite(sampleRate) ? sampleRate : 48000.0,
                                 8000.0, 192000.0);
    return {int(std::lround(fs*0.050)), true, true, false};
}
inline DelayContract delayContract(double sampleRate, bool /*experimentalCycle*/) noexcept {
    const double fs = std::clamp(std::isfinite(sampleRate) ? sampleRate : 48000.0,
                                 8000.0, 192000.0);
    return {int(std::lround(fs * 0.003)), true, true, false};
}
} // namespace skelly
