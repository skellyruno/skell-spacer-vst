#pragma once
#include "DelayContract.hpp"
// SkellyTune Realtime: original, causal pitch-correction proof-of-concept.
// Copyright 2026. See LICENSE. All audio-thread operations are allocation-free after prepare().
#include "NoteDecision.hpp"
#include "CausalCycleSynth.hpp"
#include "CoherentMixGuard.hpp"
#include "FormantEnvelopeGuard.hpp"
#include "HarmonicAirGuard.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace skelly {
static constexpr double kPi = 3.14159265358979323846;

struct Parameters {
    float speedMs = 25.0f;       // correction slew 0..200 ms; zero bypasses cent smoothing
    float amount = 1.0f;         // 0..1
    float mix = 1.0f;            // 0..1
    float referenceA = 440.0f;   // Hz
    bool phaseAlignedJumps = true; // compare actual waveform cycles at shifter wraps
    float transientProtection = 1.0f; // 0..1, protect sibilants and attacks
    float airPreservation = 0.65f; // 0..1, preserve causal unshifted HF noise during mixed sibilants
    float vibratoPreservation = 0.0f; // 0..1: preserve fast pitch expression while correcting slow pitch drift
    float flexCents = 0.0f; // 0..60 cents: soften correction around the target note
    float cycleLevelStabilization = 0.8f; // 0..1: correlation-aware grain overlap normalization
    float formantPreservation = 0.0f; // 0..1; broad-envelope wet-only restoration in PSOLA mode
    bool experimentalCycleSynthesis = false; // selectable, not certified fixed-latency
    uint16_t pitchClasses = 0x0fff; // C=bit0, C#=bit1, ... B=bit11
};

struct PitchEstimate {
    float hertz = 0.0f;
    float confidence = 0.0f;
    bool valid = false;
};

// A pair of narrowly tuned, fixed-coefficient analysis-only hum rejection
// filters. All state uses double precision because 50/60 Hz notch poles lie
// close to the unit circle at common DAW sampling rates. This is never in the
// audible output path and never allocates in the callback.
class AnalysisHumNotch {
public:
    void prepare(double sampleRate, double hertz, double q = 18.0) noexcept {
        const double omega = 2.0 * kPi * hertz / sampleRate;
        const double cosine = std::cos(omega);
        const double alpha = std::sin(omega) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0_ = 1.0 / a0;
        b1_ = -2.0 * cosine / a0;
        b2_ = b0_;
        a1_ = b1_;
        a2_ = (1.0 - alpha) / a0;
        reset();
    }
    void reset() noexcept { z1_ = z2_ = 0.0; }
    float process(float input) noexcept {
        const double x = std::isfinite(input) ? double(input) : 0.0;
        const double y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return std::isfinite(y) ? float(y) : 0.0f;
    }
private:
    double b0_=1.0,b1_=0.0,b2_=0.0,a1_=0.0,a2_=0.0,z1_=0.0,z2_=0.0;
};

// A causal, narrow-band spectral concentration estimate. It distinguishes
// actual electrical mains hum from a low male voice near the filter centers.
// The one-pole complex demodulator has no FFT/window allocation or lookahead.
class AnalysisHumMeter {
public:
    void prepare(double rate, double hertz) noexcept {
        const double omega = 2.0 * kPi * hertz / rate;
        stepCos_ = std::cos(omega); stepSin_ = std::sin(omega);
        alpha_ = 1.0 - std::exp(-1.0 / (rate * .080));
        reset();
    }
    void reset() noexcept {
        oscCos_=1.0; oscSin_=0.0; renormalize_=0;
        inPhase_=quadrature_=power_=0.0;
    }
    double push(double input) noexcept {
        const double s=oscSin_, c=oscCos_;
        // Recurrence avoids two trigonometric functions per decimated sample.
        oscCos_ = c*stepCos_ - s*stepSin_;
        oscSin_ = s*stepCos_ + c*stepSin_;
        if (++renormalize_ >= 8192) {
            const double magnitude=std::hypot(oscCos_,oscSin_);
            oscCos_ /= magnitude; oscSin_ /= magnitude; renormalize_=0;
        }
        inPhase_ += alpha_ * (input*c-inPhase_);
        quadrature_ += alpha_ * (input*s-quadrature_);
        power_ += alpha_ * (input*input-power_);
        // A pure sine contributes a ratio approaching 1.0.
        return std::clamp(2.0*(inPhase_*inPhase_+quadrature_*quadrature_) /
                           std::max(1.0e-10,power_),0.0,1.0);
    }
private:
    double stepCos_=1.0,stepSin_=0.0,alpha_=0.0;
    double oscCos_=1.0,oscSin_=0.0,inPhase_=0,quadrature_=0,power_=0;
    int renormalize_=0;
};

class CausalPitchTracker {
public:
    void setFastVocalAcquisitionEnabled(bool enabled) noexcept { fastVocalAcquisitionEnabled_ = enabled; }
    void setMinimumConfidence(float confidence) noexcept {
        minimumConfidence_ = std::clamp(confidence, 0.48f, 0.75f);
    }
    void prepare(double rate) noexcept {
        factor_ = std::clamp(int(std::lround(rate / 12000.0)), 1, 16);
        reducedRate_ = rate / double(factor_);
        lowLag_ = std::clamp(int(reducedRate_ / 850.0), 2, 250);
        highLag_ = std::clamp(int(std::ceil(reducedRate_ / 65.0)), lowLag_ + 1, 500);
        fastWindow_ = std::clamp(int(std::round(reducedRate_ * 0.012)), 112, 384);
        steadyWindow_ = std::clamp(int(std::round(reducedRate_ * 0.014)), fastWindow_, 384);
        stableHops_ = 0; stableLastF0_ = 0.0f;
        slowWindow_ = std::clamp(int(std::round(reducedRate_ * 0.024)), 160, 384);
        window_ = fastWindow_;
        useSlowWindow_ = false;
        hop_ = std::max(1, int(std::lround(64.0 / factor_)));
        // Causal FIR low-pass before decimation. 33 taps and no dynamically allocated state.
        constexpr int count = 33;
        double norm = 0.0;
        const double cutoff = 0.43 / double(factor_); // cycles/input sample
        for (int i = 0; i < count; ++i) {
            const int offset = i - count / 2;
            const double sinc = offset == 0 ? 2.0 * cutoff :
                std::sin(2.0 * kPi * cutoff * offset) / (kPi * offset);
            const double hann = 0.5 - 0.5 * std::cos(2.0 * kPi * i / double(count - 1));
            taps_[i] = float(sinc * hann);
            norm += taps_[i];
        }
        for (auto& t : taps_) t = float(t / norm);
        rawPowerAlpha_ = std::exp(-1.0 / (rate * 0.025));
        filteredPowerAlpha_ = std::pow(rawPowerAlpha_, factor_);
        // Three-pole causal safety path only used when strong out-of-band energy
        // threatens the baseband detector. Not applied to ordinary voiced input.
        noiseLowpassAlpha_ = 1.0 - std::exp(-2.0 * kPi * 850.0 / reducedRate_);
        contaminationSmooth_ = 1.0 - std::exp(-double(factor_) / (rate * 0.005));
        hum50_.prepare(reducedRate_, 50.0);
        hum60_.prepare(reducedRate_, 60.0);
        meter50_.prepare(reducedRate_, 50.0);
        meter60_.prepare(reducedRate_, 60.0);
        humBlendStep_ = 1.0 - std::exp(-1.0/(reducedRate_*0.012));
        reset();
    }

    void reset() noexcept {
        rawPower_ = filteredPower_ = 0.0;
        safeLowpass_.fill(0.0); contaminationBlend_ = 0.0;
        hum50_.reset(); hum60_.reset();
        meter50_.reset(); meter60_.reset(); humBlend_=0.0;
        samples_.fill(0.0f); fir_.fill(0.0f);
        firIndex_ = 0; write_ = 0; downCount_ = 0; decimated_ = 0;
        sinceUpdate_ = 0; estimate_ = {}; useSlowWindow_ = false; window_ = fastWindow_;
        initializedDifference_ = false; differenceAge_ = 0;
        stableHops_ = 0; stableLastF0_ = 0.0f;
    }

    // Returns true only when a new estimate is available; the estimate may be unvoiced.
    bool push(float in, PitchEstimate& estimate) noexcept {
        if (!std::isfinite(in)) in = 0.0f;
        rawPower_ = rawPowerAlpha_ * rawPower_ + (1.0 - rawPowerAlpha_) * double(in)*in;
        fir_[firIndex_] = in;
        firIndex_ = (firIndex_ + 1) % int(fir_.size());
        if (++downCount_ < factor_) return false;
        downCount_ = 0;
        float filtered = 0.0f;
        int pos = firIndex_;
        for (int i = 0; i < int(taps_.size()); ++i) {
            filtered += taps_[i] * fir_[pos];
            pos = (pos + 1) % int(fir_.size());
        }
        filteredPower_ = filteredPowerAlpha_ * filteredPower_ +
            (1.0 - filteredPowerAlpha_) * double(filtered)*filtered;
        // A few high-frequency harmonics can remain above the 33-tap FIR
        // stopband and bias YIN, especially at 96 kHz. When the baseband
        // energy fraction collapses, temporarily select an additional
        // causal, 3-pole low-pass analysis stream. The actual audio path is
        // never filtered. Smooth mode switching to avoid spurious note edges.
        double low = filtered;
        for (auto& stage: safeLowpass_) {
            stage += noiseLowpassAlpha_ * (low - stage);
            low = stage;
        }
        const double fraction = filteredPower_ / std::max(rawPower_, 1.0e-10);
        const double desiredBlend = std::clamp((0.80 - fraction) / 0.10, 0.0, 1.0);
        contaminationBlend_ += contaminationSmooth_ * (desiredBlend - contaminationBlend_);
        // The user's audio stays untouched. Mains hum in a microphone can
        // dominate YIN despite a louder higher-pitched voice; notch only the
        // pitch-analysis signal at both common electrical mains frequencies.
        const float analysis = float((1.0 - contaminationBlend_)*filtered + contaminationBlend_*low);
        const float clean = hum60_.process(hum50_.process(analysis));
        const double humShare = std::max(meter50_.push(analysis),meter60_.push(analysis));
        const double wanted = std::clamp((humShare - .13)/.07,0.0,1.0);
        humBlend_ += humBlendStep_ * (wanted-humBlend_);
        const float detectorSample = float((1.0-humBlend_)*analysis + humBlend_*clean);
        samples_[write_] = detectorSample;
        write_ = (write_ + 1) & (int(samples_.size()) - 1);
        ++decimated_;
        if (++sinceUpdate_ < hop_) return false;
        sinceUpdate_ = 0;
        // Multi-resolution tracker: 14ms observation for rapid mid/high
        // syllables, 24ms for pitch periods below ~145 Hz. Hysteresis keeps
        // vibrato near the boundary from repeatedly changing YIN windows.
        if (estimate_.valid) {
            if (estimate_.hertz < 142.0f) useSlowWindow_ = true;
            if (estimate_.hertz > 162.0f) useSlowWindow_ = false;
        }
        // v3.2: use 12ms history during initial mid/high-voice acquisition,
        // then restore the 14ms window on stable voiced passages. This avoids
        // sacrificing cycle stability to accelerate short syllable recognition.
        if (estimate_.valid && estimate_.hertz > 162.0f &&
            (stableLastF0_ <= 0.0f || std::abs(std::log2(estimate_.hertz/stableLastF0_))*1200.0 < 70.0)) {
            if (stableHops_ < 100000) ++stableHops_;
        } else stableHops_ = 0;
        stableLastF0_ = estimate_.valid ? estimate_.hertz : 0.0f;
        const bool settled = stableHops_*hop_ >= int(reducedRate_*0.150);
        const int selectedWindow = useSlowWindow_ ? slowWindow_ :
            ((!fastVocalAcquisitionEnabled_ || settled) ? steadyWindow_ : fastWindow_);
        if (window_ != selectedWindow) {
            window_ = selectedWindow;
            initializedDifference_ = false;
            differenceAge_ = 0;
        }
        estimate_ = analyze();
        // v0.8 out-of-band rejection: a strongly attenuated source above the
        // decimator cutoff can still alias into a high-confidence *subharmonic*
        // under CMND. Compare causal input-band energy with the decimated
        // analysis-band energy; reject candidates dominated by out-of-band
        // material (sibilants, cymbals, high-frequency pure tones). This
        // preserves the v0.7 estimator/response for ordinary voiced audio.
        const double bandRatio = filteredPower_ / std::max(1.0e-10, rawPower_);
        if (bandRatio < 0.09) {
            estimate_.valid = false;
            estimate_.hertz = 0.0f;
            estimate_.confidence = 0.0f;
        }
        estimate = estimate_;
        return true;
    }
    int hopHostSamples() const noexcept { return factor_ * hop_; }
    PitchEstimate last() const noexcept { return estimate_; }
    double reducedRate() const noexcept { return reducedRate_; }

private:
    float past(int lag) const noexcept {
        return samples_[(write_ - 1 - lag) & (int(samples_.size()) - 1)];
    }
    PitchEstimate analyze() noexcept {
        if (decimated_ < uint64_t(window_ + highLag_ + 3)) return {};
        double energy = 0.0;
        for (int i = 0; i < window_; ++i) {
            const double s = past(i);
            energy += s * s;
        }
        if (energy / window_ < 0.000004) {
            // v1.6: silence can last for thousands of hops. Rolling YIN
            // differences no longer describe the current ring after an early
            // return: force a complete recalculation on the next voiced hop.
            initializedDifference_ = false;
            differenceAge_ = 0;
            return {};
        }

        // YIN cumulative mean normalized difference function, computed on causal history.
        // We avoid a 2*N FFT and keep the history short. Worst-case bounded work/hop.
        // Rolling causal YIN. The preceding window and current window differ
        // by only hop_ samples. Update the lag difference in O(hop_*maxLag)
        // rather than O(window_*maxLag), with periodic exact recomputation to
        // avoid floating-point drift over long audio streams.
        // A full recomputation is also required on the first valid analysis.
        const bool full = !initializedDifference_ || ++differenceAge_ >= 256;
        difference_[0] = 0.0;
        for (int lag = 1; lag <= highLag_; ++lag) {
            double diff = full ? 0.0 : difference_[lag];
            if (full) {
                for (int j = 0; j < window_; ++j) {
                    const double e = double(past(j)) - double(past(j + lag));
                    diff += e*e;
                }
            } else {
                for (int j = 0; j < hop_; ++j) {
                    const double entered = double(past(j)) - double(past(j+lag));
                    const double departed = double(past(window_+j)) - double(past(window_+j+lag));
                    diff += entered*entered - departed*departed;
                }
            }
            difference_[lag] = std::max(0.0, diff);
        }
        if (full) { initializedDifference_ = true; differenceAge_ = 0; }
        double running = 0.0;
        cmnd_[0] = 1.0f;
        for (int lag = 1; lag <= highLag_; ++lag) {
            running += difference_[lag];
            cmnd_[lag] = float(difference_[lag] * lag / std::max(1e-20, running));
        }
        int best = -1;
        for (int lag = lowLag_; lag < highLag_ - 1; ++lag) {
            if (cmnd_[lag] < 0.16f && cmnd_[lag] <= cmnd_[lag+1]) {
                best = lag;
                break;
            }
        }
        if (best < 0) {
            best = lowLag_;
            for (int lag = lowLag_+1; lag < highLag_-1; ++lag)
                if (cmnd_[lag] < cmnd_[best]) best = lag;
        }
        // v1.7: YIN's first-below-threshold strategy can select an octave-high
        // period when the 2nd and 4th harmonics dominate a weak fundamental.
        // If a *distinctly stronger* periodicity exists at twice the chosen
        // lag, the longer period has support from otherwise hidden odd
        // harmonics. Do not force an octave down for pure even-harmonic
        // signals: a 220 Hz sinusoid and a 110 Hz signal containing only
        // even harmonics are mathematically indistinguishable.
        const int twice = best * 2;
        if (twice + 2 < highLag_ && cmnd_[best] < 0.16f) {
            int slower = twice;
            for (int candidate = twice - 2; candidate <= twice + 2; ++candidate) {
                if (candidate > best + 2 && candidate < highLag_ - 1 &&
                    cmnd_[candidate] < cmnd_[slower]) slower = candidate;
            }
            const double octaveEvidence = double(cmnd_[best]) - double(cmnd_[slower]);
            // Require meaningful evidence, not a fractional-lag interpolation
            // difference or rounding noise between equal minima.
            if (octaveEvidence > 0.035 && cmnd_[slower] < 0.13f &&
                cmnd_[slower] <= cmnd_[slower-1] && cmnd_[slower] <= cmnd_[slower+1]) {
                best = slower;
            }
        }
        const float confidence = std::clamp(1.0f - cmnd_[best], 0.0f, 1.0f);
        if (confidence < minimumConfidence_) return {0.0f, confidence, false};
        // Quadratic peak interpolation in the CMND valley.
        double lag = best;
        if (best > 1 && best < highLag_-1) {
            const double a = cmnd_[best-1], b = cmnd_[best], c = cmnd_[best+1];
            const double denom = a - 2.0*b + c;
            if (std::abs(denom) > 1e-9) lag += std::clamp(0.5 * (a-c)/denom, -0.5, 0.5);
        }
        const float f0 = float(reducedRate_ / lag);
        return {f0, confidence, std::isfinite(f0) && f0 >= 65.0f && f0 <= 850.0f};
    }
    float minimumConfidence_ = 0.62f;
    int factor_ = 4, lowLag_ = 14, highLag_ = 200, window_ = 288, hop_ = 16;
    int fastWindow_ = 168, steadyWindow_ = 168, slowWindow_ = 288;
    int stableHops_ = 0;
    float stableLastF0_ = 0.0f;
    bool fastVocalAcquisitionEnabled_ = false;
    bool useSlowWindow_ = false;
    int firIndex_ = 0, write_ = 0, downCount_ = 0, sinceUpdate_ = 0;
    uint64_t decimated_ = 0;
    double reducedRate_ = 12000.0;
    double rawPowerAlpha_ = 0.999, filteredPowerAlpha_ = 0.996;
    double noiseLowpassAlpha_ = 0.3, contaminationSmooth_ = 0.02;
    double contaminationBlend_ = 0.0;
    std::array<double,3> safeLowpass_{};
    double rawPower_ = 0.0, filteredPower_ = 0.0;
    AnalysisHumNotch hum50_, hum60_;
    AnalysisHumMeter meter50_, meter60_;
    double humBlend_=0.0, humBlendStep_=0.0;
    std::array<float,33> taps_{};
    std::array<float,33> fir_{};
    std::array<float,4096> samples_{};
    std::array<double,512> difference_{};
    bool initializedDifference_ = false;
    int differenceAge_ = 0;
    std::array<float,512> cmnd_{};
    PitchEstimate estimate_{};
};

// Selects a coherent pitch-analysis signal without changing audio output.
// Mid-channel cancellation is a common failure mode for opposite-polarity stereo
// recordings; the original average(L,R) silently discarded the fundamental.
// Uses exponentially smoothed energy/covariance, no allocations or locks.
class StereoPitchRouter {
public:
    void prepare(double rate) noexcept {
        const double tau = 0.012;
        alpha_ = std::exp(-1.0 / (rate * tau));
        reset();
    }
    void reset() noexcept { leftPower_ = rightPower_ = covariance_ = 0.0; }
    float push(float left, float right) noexcept {
        const double k = 1.0 - alpha_;
        leftPower_ = alpha_ * leftPower_ + k * double(left)*left;
        rightPower_ = alpha_ * rightPower_ + k * double(right)*right;
        covariance_ = alpha_ * covariance_ + k * double(left)*right;
        // Near-inverted stereo will cancel in the mid signal. Use a single
        // channel for analysis, but preserve original stereo in the output.
        const double joint = std::sqrt(leftPower_*rightPower_);
        if (joint > 1.0e-12 && covariance_ < -0.60 * joint)
            return (leftPower_ >= rightPower_) ? left : right;
        // If almost all of the program exists on one channel, prefer it.
        if (leftPower_ > rightPower_ * 16.0) return left;
        if (rightPower_ > leftPower_ * 16.0) return right;
        return 0.5f * (left + right);
    }
private:
    double alpha_ = 0.998, leftPower_ = 0.0, rightPower_ = 0.0, covariance_ = 0.0;
};

// Original causal attack cue: combines fast/slow energy envelopes, a refractory
// counter, and a brief onset hold. It never alters the audio samples and never
// allocates. A sudden level increase is only used to speed note decisions when
// the pitch tracker independently produces a high-confidence new candidate.
class CausalOnsetCue {
public:
    void prepare(double rate) noexcept {
        fastA_ = 1.0 - std::exp(-1.0 / (rate * 0.002));
        slowA_ = 1.0 - std::exp(-1.0 / (rate * 0.045));
        holdLength_ = std::max(1, int(std::round(rate * 0.050)));
        refractoryLength_ = std::max(1, int(std::round(rate * 0.075)));
        reset();
    }
    void reset() noexcept {
        fast_ = slow_ = 0.0;
        hold_ = refractory_ = 0;
    }
    void push(float sample) noexcept {
        const double x = std::abs(std::isfinite(sample) ? sample : 0.0f);
        fast_ += fastA_ * (x - fast_);
        slow_ += slowA_ * (x - slow_);
        if (hold_ > 0) --hold_;
        if (refractory_ > 0) --refractory_;
        if (refractory_ == 0 && fast_ > 0.045 &&
            fast_ > 2.30 * (slow_ + 0.0010)) {
            hold_ = holdLength_;
            refractory_ = refractoryLength_;
        }
    }
    float score() const noexcept { return hold_ > 0 ? 1.0f : 0.0f; }
private:
    double fastA_ = 0.01, slowA_ = 0.001;
    double fast_ = 0.0, slow_ = 0.0;
    int hold_ = 0, refractory_ = 0, holdLength_ = 1, refractoryLength_ = 1;
};

// Per-sample causal voiced/noise discriminator. The high-pass energy ratio
// helps suppress pitch processing across broadband sibilants even when an
// earlier pitch estimate remains latched. These envelopes are not FFT based.
class TransientNoiseCue {
public:
    void prepare(double rate) noexcept {
        highpassA_ = std::exp(-2.0 * kPi * 1700.0 / rate);
        envelopeA_ = 1.0 - std::exp(-1.0 / (rate * 0.004));
        reset();
    }
    void reset() noexcept { lowpass_ = 0; highPower_ = lowPower_ = 0; }
    float push(float x) noexcept {
        lowpass_ = highpassA_ * lowpass_ + (1.0 - highpassA_) * x;
        const double h = x - lowpass_;
        highPower_ += envelopeA_ * (h*h - highPower_);
        lowPower_ += envelopeA_ * (x*x - lowPower_);
        const double ratio = highPower_ / (lowPower_ + 1e-8);
        // Sensitive causal HF/noise likelihood; rising above ~9% highpass energy.
        return float(std::clamp((ratio - 0.09) * 2.8, 0.0, 1.0));
    }
private:
    double highpassA_ = 0, envelopeA_ = 0, lowpass_ = 0;
    double highPower_ = 0, lowPower_ = 0;
};

class RealtimeTuner {
public:
    // prepare() / reset() are called with the audio callback suspended.
    // Select studio latency only while the host is stopped/prepareToPlay.
    // Never change host PDC from the realtime process callback.
    // Existing calls preserve the original realtime/50ms profiles. Fast Studio
    // must be chosen before preparing; it is never an automatable mode.
    void prepare(double sampleRate, int maxChannels = 2, bool studioClock = false, bool fastStudio = false) {
        rate_ = std::clamp(sampleRate, 8000.0, 192000.0);
        channels_ = std::clamp(maxChannels, 1, 2);
        // Fixed, nominal audio delay ~= 3 ms, rounded to integer samples.
        studioClock_ = studioClock;
        fastStudio_ = studioClock && fastStudio;
        latency_ = studioClock_ ? (fastStudio_ ? studioFastDelayContract(rate_).reportedSamples
                                              : studioDelayContract(rate_).reportedSamples)
                                 : delayContract(rate_, false).reportedSamples;
        int capacity = 1;
        while (capacity < int(rate_ * 0.20) + 8192) capacity <<= 1;
        mask_ = capacity - 1;
        history_.assign(size_t(capacity) * size_t(channels_), 0.0f);
        transientMark_.assign(size_t(capacity), uint8_t(0));
        tracker_.prepare(rate_);
        stereoRouter_.prepare(rate_);
        onsetCue_.prepare(rate_);
        noiseCue_.prepare(rate_);
        notes_.prepare(rate_);
        cycleSynth_.prepare(rate_, channels_);
        mixGuard_.prepare(rate_);
        formantGuard_.prepare(rate_, channels_);
        airGuard_.prepare(rate_, channels_);
        cycleSynth_.setClockLockedDelay(studioClock_ ? latency_ : 0);
        highBandPole_ = std::exp(-2.0*kPi*3400.0/rate_);
        voicedEnvelopeStep_ = 1.0 - std::exp(-1.0 / (rate_ * 0.003));
        reset();
    }
    void reset() noexcept {
        std::fill(history_.begin(), history_.end(), 0.0f);
        std::fill(transientMark_.begin(), transientMark_.end(), uint8_t(0));
        previousTransientInput_.fill(0.0f);
        transientDiffPower_ = 0.0; transientHold_ = 0; transientRelease_ = 0.0;
        preservedTransientSamples_ = 0;
        write_ = 0;
        readDelay_ = latency_ + 6.0;
        jumpFrom_ = jumpTo_ = 0.0;
        jumpPosition_ = jumpLength_ = 0;
        jumpCorrelation_ = 1.0; minActiveSourceDelay_ = 1.0e30; maxActiveSourceDelay_ = 0.0; maxOverlapGain_ = 1.0;
        cents_ = 0.0;
        desiredCents_ = 0.0;
        voiceMix_ = 0.0;
        effectiveMix_ = 0.0;
        dryCenteredSamples_ = 0;
        f0_ = smoothF0_ = 220.0;
        slowInputMidi_ = 0.0; slowMidiReady_ = false; vibratoTarget_ = -1;
        hasF0_ = false;
        dryLow_.fill(0.0);
        wetLow_.fill(0.0);
        cycleLow_.fill(0.0);
        phaseJumpCount_ = 0;
        cycleWrapCount_ = 0; activeWrapCount_ = 0; sourceAgeGuardHits_ = 0;
        phaseJumpError_ = 0.0;
        mismatchBefore_ = mismatchAfter_ = 0.0;
        targetNote_ = -1;
        voiced_ = false;
        trustedVoiceSamples_ = 0;
        bridgedHops_ = 0;
        continueCorrectionState_ = false;
        voicedEnvelope_ = 0.0;
        tracker_.reset();
        stereoRouter_.reset();
        onsetCue_.reset();
        onsetHoldSamples_ = 0;
        onsetAssistedTransitions_ = onsetMemoryAppliedHops_ = 0;
        noiseCue_.reset();
        notes_.reset();
        targetPending_ = false;
        params_ = {};
        cycleSynth_.reset();
        mixGuard_.reset();
        formantGuard_.reset();
        formantActive_ = false;
        airGuard_.reset();
        cycleBlend_ = 0.0;
    }
    int latencySamples() const noexcept { return latency_; }
    DelayContract outputDelayContract(bool experimentalMode) const noexcept {
        return studioClock_ ? (fastStudio_ ? studioFastDelayContract(rate_) : studioDelayContract(rate_))
                            : delayContract(rate_, experimentalMode);
    }
    uint64_t preservedTransientSamples() const noexcept { return preservedTransientSamples_; }
    PitchEstimate lastPitch() const noexcept { return tracker_.last(); }
    CausalCycleSynth::Status cycleSynthStatus() const noexcept { return cycleSynth_.status(); }
    // v3.1 controlled research switch: preserve a trusted voiced correction
    // across a *brief* unreliable tracker hop, never across new-note pending state.
    // Enabled by default only after validation on recorded vocals.
    void setShortVoicedBridgeEnabled(bool enabled) noexcept { shortVoicedBridgeEnabled_ = enabled; }
    // Research knob, deliberately not exposed as an automation parameter.
    void setMinimumTrackerConfidence(float confidence) noexcept {
        tracker_.setMinimumConfidence(confidence);
    }
    uint64_t bridgedHops() const noexcept { return bridgedHops_; }
    void setShortVoicedBridgeDurationMs(float ms) noexcept {
        maxShortVoicedBridgeMs_ = std::clamp(double(ms), 0.0, 35.0);
    }
    // Offline A/B guard used to verify v2.9, not a user-facing automation ID.
    void setHarmonicAwareWetGateEnabled(bool enabled) noexcept { harmonicWetGateEnabled_ = enabled; }
    // Experimental A/B only. Enable only after real-vocal benchmarking.
    void setProvisionalTransitionEnabled(bool enabled) noexcept { provisionalTransitionEnabled_ = enabled; }
    void setGlottalCycleEvidenceEnabled(bool enabled) noexcept { cycleSynth_.setClosureEvidenceEnabled(enabled); }
    void setStudioPhaseLockResearchEnabled(bool enabled) noexcept {
        cycleSynth_.setStrongerConcurrentPhaseEnabled(enabled);
    }
    // Offline A/B toggle for v2.5 overlap-coherence validation; not a user parameter.
    void setCycleOverlapCoherenceEnabled(bool enabled) noexcept { cycleSynth_.setOverlapCoherenceEnabled(enabled); }
    double currentCycleBlend() const noexcept { return cycleBlend_; }
    double currentMixLevelGain() const noexcept { return mixGuard_.gain(); }
    double maximumFormantGain() const noexcept { return formantGuard_.maximumObservedGain(); }
    double highBandPeriodicity() const noexcept { return airGuard_.periodicity(); }
    int currentTargetNote() const noexcept { return targetNote_; }
    bool targetPending() const noexcept { return targetPending_; }
    double currentOnsetScore() const noexcept { return onsetCue_.score(); }
    void setHardTuneOnsetMemoryEnabled(bool enabled) noexcept {
        hardTuneOnsetMemoryEnabled_ = enabled;
        if (!enabled) onsetHoldSamples_ = 0;
    }
    uint64_t onsetAssistedTransitions() const noexcept { return onsetAssistedTransitions_; }
    uint64_t onsetMemoryAppliedHops() const noexcept { return onsetMemoryAppliedHops_; }
    double appliedCents() const noexcept { return cents_; }
    double currentReadDelaySamples() const noexcept { return readDelay_; }
    double effectiveWetMix() const noexcept { return effectiveMix_; }
    // These measure SOURCE AGE for nontrivial wet output, not an exact constant transport delay.
    double minimumWetSourceAgeSamples() const noexcept { return minActiveSourceDelay_ == 1.0e30 ? 0.0 : minActiveSourceDelay_; }
    double maximumWetSourceAgeSamples() const noexcept { return maxActiveSourceDelay_; }
    uint64_t phaseAlignedJumpCount() const noexcept { return phaseJumpCount_; }
    uint64_t cycleWrapCount() const noexcept { return cycleWrapCount_; }
    uint64_t activeWrapCount() const noexcept { return activeWrapCount_; }
    uint64_t sourceAgeGuardHits() const noexcept { return sourceAgeGuardHits_; }
    double lastCycleCorrelation() const noexcept { return jumpCorrelation_; }
    double maximumOverlapGain() const noexcept { return maxOverlapGain_; }
    double phaseMatchGainPercent() const noexcept {
        return mismatchBefore_ > 0.0 ? 100.0 * (1.0 - mismatchAfter_/mismatchBefore_) : 0.0;
    }
    double averageJumpMismatchBefore() const noexcept { return phaseJumpCount_ ? mismatchBefore_/phaseJumpCount_ : 0.0; }
    double averageJumpMismatchAfter() const noexcept { return phaseJumpCount_ ? mismatchAfter_/phaseJumpCount_ : 0.0; }
    double averageJumpPeriodError() const noexcept {
        return phaseJumpCount_ ? phaseJumpError_/double(phaseJumpCount_) : 0.0;
    }
    void setParameters(Parameters params) noexcept {
        params.speedMs = std::clamp(params.speedMs, 0.0f, 200.0f);
        params.amount = std::clamp(params.amount, 0.0f, 1.0f);
        params.mix = std::clamp(params.mix, 0.0f, 1.0f);
        params.referenceA = std::clamp(params.referenceA, 400.0f, 480.0f);
        params.pitchClasses &= 0x0fff;
        params.transientProtection = std::clamp(params.transientProtection, 0.0f, 1.0f);
        params.airPreservation = std::clamp(params.airPreservation, 0.0f, 1.0f);
        params.vibratoPreservation = std::clamp(params.vibratoPreservation, 0.0f, 1.0f);
        params.flexCents = std::clamp(params.flexCents, 0.0f, 60.0f);
        params.cycleLevelStabilization = std::clamp(params.cycleLevelStabilization, 0.0f, 1.0f);
        if (!std::isfinite(params.formantPreservation)) params.formantPreservation = 0.0f;
        params.formantPreservation = std::clamp(params.formantPreservation, 0.0f, 1.0f);
        // Hosts can pass non-finite automation values. std::clamp alone does not
        // sanitize NaN; reject them before they enter oscillators or filters.
        if (!std::isfinite(params.speedMs)) params.speedMs = 25.0f;
        if (!std::isfinite(params.amount)) params.amount = 1.0f;
        if (!std::isfinite(params.mix)) params.mix = 1.0f;
        if (!std::isfinite(params.referenceA)) params.referenceA = 440.0f;
        if (!std::isfinite(params.transientProtection)) params.transientProtection = 1.0f;
        if (!std::isfinite(params.airPreservation)) params.airPreservation = .65f;
        if (!std::isfinite(params.vibratoPreservation)) params.vibratoPreservation = 0.0f;
        if (!std::isfinite(params.flexCents)) params.flexCents = 0.0f;
        if (!std::isfinite(params.cycleLevelStabilization)) params.cycleLevelStabilization = 0.8f;
        params_ = params;
        // The shorter 12ms acquisition window is for explicitly requested
        // maximum-speed hard tuning only. Ordinary presets preserve the
        // long-established 14/24ms pitch-analysis behavior.
        tracker_.setFastVocalAcquisitionEnabled(
            params_.speedMs <= 0.0001f && params_.amount >= 0.995f &&
            params_.mix >= 0.995f && params_.flexCents <= 0.01f &&
            params_.vibratoPreservation <= 0.01f);
    }
    // in/out may alias: each sample is captured before writing any output channels.
    // Planar buffers; sample-count and channel count may vary per audio callback.
    void process(const float* const* input, float* const* output, int frames, int numChannels) noexcept {
        if (!input || !output || frames <= 0 || history_.empty()) return;
        numChannels = std::clamp(numChannels, 1, channels_);
        const int capacity = mask_ + 1;
        const double smooth = params_.speedMs <= 0.0001f ? 0.0 :
            std::exp(-1.0 / (rate_ * double(params_.speedMs) * .001));
        const bool hard = params_.speedMs <= 1.5f && params_.amount >= 0.995f &&
            params_.mix >= 0.995f && params_.flexCents <= 0.01f &&
            params_.vibratoPreservation <= 0.01f;
        const bool zeroSpeed = params_.speedMs <= 0.0001f;
        // Zero speed removes cent-smoothing, but keeps a sub-millisecond
        // click-safe signal blend. Prior 1ms presets retain their envelope.
        const double gateSmooth = std::exp(-1.0 / (rate_ * (zeroSpeed && hard ? 0.0008 : (hard ? 0.002 : 0.006))));
        const double unitySmooth = std::exp(-1.0 / (rate_ * (zeroSpeed && hard ? 0.0008 : (hard ? 0.002 : 0.004))));
        const double cycleModeSmooth = std::exp(-1.0 / (rate_ * 0.012));
        const double cycleReleaseSmooth = std::exp(-1.0 / (rate_ * 0.00025));
        const double transientMeterStep = 1.0 - std::exp(-1.0 / (rate_ * 0.030));
        const double transientReleaseStep = std::exp(-1.0 / (rate_ * 0.003));
        for (int i = 0; i < frames; ++i) {
            float in[2] = {0.0f, 0.0f};
            for (int c = 0; c < numChannels; ++c) {
                in[c] = std::isfinite(input[c][i]) ? input[c][i] : 0.0f;
                history_[size_t(c) * capacity + write_] = in[c];
            }
            // v2.0: Preserve sample-aligned sharp attacks before entering either
            // pitch-shifting engine. Detector and resynthesizer are free to use
            // historical voiced cycles, but an attack must be available at the
            // exact transport delay we report to the host. Store the decision
            // alongside its INPUT sample and retrieve at the same delay as dry.
            double diff = 0.0;
            for (int c = 0; c < numChannels; ++c) {
                diff = std::max(diff, std::abs(double(in[c]) - previousTransientInput_[c]));
                previousTransientInput_[c] = in[c];
            }
            const double floor = std::sqrt(std::max(0.0, transientDiffPower_));
            const bool attack = diff > std::max(0.075, 5.0 * floor);
            // Update AFTER testing so the incoming attack cannot mask itself.
            transientDiffPower_ += transientMeterStep *
                (std::min(diff * diff, 1.0) - transientDiffPower_);
            if (attack) transientHold_ = int(std::lround(rate_ * 0.005));
            const bool tagged = transientHold_ > 0;
            if (transientHold_ > 0) --transientHold_;
            transientMark_[size_t(write_)] = tagged ? uint8_t(1) : uint8_t(0);
            const float trackingSample = (numChannels == 2) ? stereoRouter_.push(in[0], in[1]) : in[0];
            onsetCue_.push(trackingSample);
            // A short input attack often precedes the first reliable pitch hop.
            // Retain the onset cue briefly so pitch and note-decision evidence
            // can coincide, without adding any future-looking audio buffer.
            if (hardTuneOnsetMemoryEnabled_ && hard && !voiced_ && onsetCue_.score() >= 0.72)
                onsetHoldSamples_ = int(std::lround(rate_ * 0.032));
            else if (onsetHoldSamples_ > 0)
                --onsetHoldSamples_;
            voicedEnvelope_ += voicedEnvelopeStep_ *
                (std::abs(double(trackingSample)) - voicedEnvelope_);
            const double noiseScore = noiseCue_.push(trackingSample);
            PitchEstimate e;
            if (tracker_.push(trackingSample, e)) {
                const bool rememberedOnset = hardTuneOnsetMemoryEnabled_ && hard &&
                    e.valid && e.confidence >= 0.85f && onsetHoldSamples_ > 0 &&
                    onsetCue_.score() < 0.72f;
                const int priorTarget = targetNote_;
                const auto result = notes_.update(e.valid ? e.hertz : 0.0,
                    e.valid ? e.confidence : 0.0,
                    rememberedOnset ? 0.72 : double(onsetCue_.score()),
                    uint32_t(tracker_.hopHostSamples()), params_.pitchClasses, params_.referenceA,
                    params_.speedMs <= 1.5f && params_.amount >= 0.995f &&
                    params_.mix >= 0.995f && params_.flexCents <= 0.01f &&
                    params_.vibratoPreservation <= 0.01f);
                if (rememberedOnset) {
                    ++onsetMemoryAppliedHops_;
                    if (result.changed && priorTarget >= 0 && result.targetMidi != priorTarget)
                        ++onsetAssistedTransitions_;
                }
                targetNote_ = result.targetMidi;
                targetPending_ = result.pending;
                const int workingTarget = provisionalTransitionEnabled_ && e.valid &&
                    result.provisionalMidi >= 0 ? result.provisionalMidi : targetNote_;
                const bool usableTarget = !result.pending ||
                    (provisionalTransitionEnabled_ && result.provisionalMidi >= 0);
                // Both detector and musical controller must accept this frame.
                // A low-confidence provisional F0 must not be pitched toward a
                // stale target that NoteDecision is explicitly holding.
                if (e.valid && result.voiced && workingTarget >= 0 && usableTarget) {
                    f0_ = e.hertz;
                    if (!hasF0_ || std::abs(std::log2(f0_/smoothF0_)) > 0.65) {
                        smoothF0_ = f0_;
                    } else {
                        // Track pitch period smoothly, but still follow vibrato.
                        const double blend = 1.0 - std::exp(-double(tracker_.hopHostSamples())/(rate_*0.012));
                        smoothF0_ += blend*(f0_-smoothF0_);
                    }
                    hasF0_ = true;
                    const double targetHz = params_.referenceA *
                        std::pow(2.0, (workingTarget - 69) / 12.0);
                    const double rawMidi = 69.0 + 12.0 * std::log2(f0_ / params_.referenceA);
                    // A causal slow trajectory estimates note drift while the residual
                    // carries singer vibrato. Initialize at each newly committed note
                    // so a legitimate jump does not masquerade as vibrato.
                    if (!slowMidiReady_ || vibratoTarget_ != workingTarget) {
                        slowInputMidi_ = rawMidi;
                        slowMidiReady_ = true;
                        vibratoTarget_ = workingTarget;
                    } else {
                        constexpr double trajectorySeconds = 0.085;
                        const double k = 1.0 - std::exp(-double(tracker_.hopHostSamples()) /
                            (rate_ * trajectorySeconds));
                        slowInputMidi_ += k * (rawMidi - slowInputMidi_);
                    }
                    const double error = std::clamp(1200.0 * std::log2(targetHz / f0_), -1200.0, 1200.0);
                    // Soft Flex Tune zone: reduces correction smoothly near the
                    // note center, without allowing a hard dead-zone discontinuity.
                    const double distance = std::abs(error);
                    const double flex = params_.flexCents;
                    const double strength = flex <= 0.0 ? 1.0 :
                        std::clamp((distance - flex * 0.25) / (flex * 0.75 + 1.0), 0.0, 1.0);
                    const double preservedVibrato = params_.vibratoPreservation *
                        std::clamp(100.0 * (rawMidi - slowInputMidi_), -120.0, 120.0);
                    desiredCents_ = std::clamp(error * strength + preservedVibrato, -1200.0, 1200.0);
                    voiced_ = true;
                    trustedVoiceSamples_ = 0;
                } else {
                    // A new pitch candidate has not been accepted yet. The original
                    // engine forced this *new* input pitch toward the *old* target
                    // during the entire confirmation window (up to 65 ms), making
                    // transitions audibly wrong. Temporarily fade to delayed dry
                    // instead, then resume tuning after a confirmed new note.
                    // Also protects unvoiced consonants from stale correction.
                    // Brief YIN confidence dips inside an otherwise voiced vowel
                    // are not necessarily syllable boundaries. Holding the last
                    // confirmed correction for <=12ms prevents wet->dry leakage.
                    // We NEVER hold through a pending *new* musical target,
                    // a new onset, quiet audio, or broadband consonants.
                    // This is causal and adds zero samples of transport delay.
                    const bool bridge = shortVoicedBridgeEnabled_ && !result.pending &&
                        targetNote_ >= 0 && voiced_ &&
                        trustedVoiceSamples_ < int(rate_ * 0.001 * maxShortVoicedBridgeMs_) &&
                        voicedEnvelope_ > 0.014 && noiseScore < 0.22 &&
                        onsetCue_.score() < 0.5;
                    if (bridge) {
                        trustedVoiceSamples_ += tracker_.hopHostSamples();
                        ++bridgedHops_;
                    } else {
                        voiced_ = false;
                    }
                    if (bridge) continueCorrectionState_ = true;
                    else continueCorrectionState_ = false;
                    // v1.6: discard correction intent on ANY unvoiced frame,
                    // not only when a candidate note is pending. Otherwise a
                    // quiet gap can retain the previous phrase's cent error
                    // and steer the attack of the next syllable incorrectly.
                    if (!continueCorrectionState_) {
                        desiredCents_ = 0.0;
                        slowMidiReady_ = false;
                        vibratoTarget_ = -1;
                    }
                }
            }
            cents_ = cents_ * smooth + (desiredCents_ * params_.amount) * (1.0 - smooth);
            // A broadband attack (especially /s/ and /sh/) should not inherit
            // a stale tonal pitch ratio. Keep the dry stream time-aligned.
            // v2.9: the high-frequency energy discriminator alone mistakes
            // bright but periodic voiced harmonics for consonant noise,
            // attenuating the wet correction and leaking original pitch.
            // Reuse the existing harmonic periodicity measurement (from the
            // preceding sample of the same delayed-dry history) to avoid
            // dropping wet gain on a confidently tracked periodic vowel.
            // Aperiodic consonants keep their original protective attenuation;
            // this modifies neither synthesis history nor declared latency.
            const double harmonicEvidence = (harmonicWetGateEnabled_ && voiced_) ?
                std::clamp(airGuard_.periodicity(),0.0,1.0) : 0.0;
            const double adjustedNoise = noiseScore * (1.0-0.85*harmonicEvidence);
            const double noiseAttenuation = std::clamp(1.0 - double(params_.transientProtection) * adjustedNoise, 0.0, 1.0);
            const double targetGate = (voiced_ && targetNote_ >= 0) ? double(params_.mix) * noiseAttenuation : 0.0;
            voiceMix_ = voiceMix_ * gateSmooth + targetGate * (1.0 - gateSmooth);
            // Make unity correction genuinely transparent: no variable-delay
            // playback when ratio approaches 1. Previous releases could leave
            // the read head stranded at an arbitrary source age after tuning.
            // Fade the wet path out before re-centering the grain scheduler.
            const double centsMagnitude = std::abs(cents_);
            const double wetNeeded = zeroSpeed && hard
                ? std::clamp((centsMagnitude - 0.08) / 0.50, 0.0, 1.0)
                : std::clamp((centsMagnitude - 0.30) / 1.70, 0.0, 1.0);
            const double wetTarget = voiceMix_ * wetNeeded;
            effectiveMix_ = effectiveMix_ * unitySmooth + wetTarget * (1.0 - unitySmooth);
            // Only recenter after the residual wet path has become inaudible.
            // This guarantees a fresh stable read head on the next note.
            if (wetTarget < 1.0e-5 && effectiveMix_ < 1.0e-5) {
                if (++dryCenteredSamples_ >= int(rate_*0.090)) {
                    readDelay_ = double(latency_) + 6.0;
                    jumpFrom_ = jumpTo_ = readDelay_;
                    jumpPosition_ = jumpLength_ = 0;
                }
            } else dryCenteredSamples_ = 0;
            const double ratio = std::clamp(std::pow(2.0, cents_ / 1200.0), 0.5, 2.0);
            const double period = rate_ / std::clamp(smoothF0_, 65.0, 850.0);
            const double slope = 1.0 - ratio;
            // Pitch-synchronous variable-delay shifter: a continuous read head moves at
            // ratio * sampleRate; when it must wrap, switch by one input pitch period.
            // Pitch-period-sized resets preserve harmonic phase rather than resetting
            // at arbitrary grain boundaries. Crossfade over <= half a pitch period.
            // The pitch shifter may need older cycles, but it must NEVER sample
            // more recent audio than the plugin's published 3-ms delay.
            // Cubic interpolation uses one younger tap, hence a safety margin.
            const double lowDelay = double(latency_) + 5.0;
            const double highDelay = lowDelay + period;
            const int nextJumpLength = std::clamp(int(std::round(period * 0.48)), 16, 768);
            if (jumpPosition_ < jumpLength_) {
                jumpFrom_ += slope;
                jumpTo_ += slope;
            } else {
                readDelay_ += slope;
                const double margin = std::abs(slope) * nextJumpLength + 0.5;
                if (slope < -1e-6 && readDelay_ < lowDelay + margin) {
                    jumpFrom_ = readDelay_;
                    jumpTo_ = readDelay_ + period;
                    if (params_.phaseAlignedJumps && effectiveMix_ > 0.0005)
                        jumpTo_ = choosePhaseAlignedDestination(jumpFrom_,jumpTo_,period,lowDelay);
                    jumpCorrelation_ = estimateCycleCorrelation(jumpFrom_,jumpTo_,period);
                    ++cycleWrapCount_; if (effectiveMix_ > 0.1) ++activeWrapCount_;
                    jumpLength_ = nextJumpLength;
                    jumpPosition_ = 0;
                } else if (slope > 1e-6 && readDelay_ > highDelay + margin) {
                    jumpFrom_ = readDelay_;
                    jumpTo_ = readDelay_ - period;
                    if (params_.phaseAlignedJumps && effectiveMix_ > 0.0005)
                        jumpTo_ = choosePhaseAlignedDestination(jumpFrom_,jumpTo_,period,lowDelay);
                    jumpCorrelation_ = estimateCycleCorrelation(jumpFrom_,jumpTo_,period);
                    ++cycleWrapCount_; if (effectiveMix_ > 0.1) ++activeWrapCount_;
                    jumpLength_ = nextJumpLength;
                    jumpPosition_ = 0;
                }
            }
            // Invariant even if the user automates speed abruptly halfway
            // through a grain: both interpolation heads remain older than
            // the minimum causal source age. Normally this is inactive.
            auto enforceAge = [this, lowDelay](double& age) noexcept {
                if (age < lowDelay) { age = lowDelay; ++sourceAgeGuardHits_; }
            };
            if (jumpPosition_ < jumpLength_) {
                enforceAge(jumpFrom_);
                enforceAge(jumpTo_);
            } else enforceAge(readDelay_);
            const bool jumping = jumpPosition_ < jumpLength_;
            const double jumpBlend = jumping ?
                (0.5-0.5*std::cos(kPi * (jumpPosition_+1.0)/jumpLength_)) : 0.0;
            // Two grains of equal loudness still cancel when their phases differ.
            // Predict expected power from normalized source correlation, then
            // conservatively compensate a portion of the overlap attenuation.
            // Limit compensation to 1.28x to prevent transient amplification.
            const double weightsPower = (1.0-jumpBlend)*(1.0-jumpBlend)
                + jumpBlend*jumpBlend + 2.0*jumpCorrelation_*jumpBlend*(1.0-jumpBlend);
            const double overlapGain = jumping ?
                (1.0 + params_.cycleLevelStabilization *
                    (std::min(1.28, 1.0 / std::sqrt(std::max(0.15, weightsPower))) - 1.0)) : 1.0;
            maxOverlapGain_ = std::max(maxOverlapGain_, overlapGain);
            if (effectiveMix_ > 0.0005) {
                const double minRead = jumping ? std::min(jumpFrom_, jumpTo_) : readDelay_;
                const double maxRead = jumping ? std::max(jumpFrom_, jumpTo_) : readDelay_;
                minActiveSourceDelay_ = std::min(minActiveSourceDelay_, minRead);
                maxActiveSourceDelay_ = std::max(maxActiveSourceDelay_, maxRead);
            }
            // Experimental HQ cycle generator: separate source-cycle reconstruction
            // fed from the same causal pitch tracker/controller as the realtime shifter.
            // Keep its input history up to date even while disabled, so automation
            // cannot revive stale grains. Its variable source age is NOT a fixed
            // 3 ms transport latency. The default engine remains unchanged.
            float cycleOutput[2]{};
            const bool useCycle = params_.experimentalCycleSynthesis &&
                voiced_ && !targetPending_ && effectiveMix_ > 0.02 &&
                noiseAttenuation > 0.45 && ratio >= 0.67 && ratio <= 1.50;
            cycleSynth_.processSample(in, cycleOutput, period, ratio, useCycle);
            const bool cycleFrame = cycleSynth_.hasOutputFrame();
            // v1.2: after a vocal onset or a switch back into PSOLA, do not
            // fade in residual grain tails from the previous voiced segment.
            // A new generation must actually start before increasing the mix.
            const bool freshCycle = useCycle && cycleFrame && cycleSynth_.freshSynthesisAvailable();
            const double cycleGoal = freshCycle ? 1.0 : 0.0;
            // On exit, retain causal tail grains while reducing their mix over
            // a short interval. v1.1 forced the blend straight to zero on the
            // first unvoiced sample and could produce a click.
            const double cycleSmoothing = freshCycle ? cycleModeSmooth : cycleReleaseSmooth;
            cycleBlend_ = cycleBlend_*cycleSmoothing + cycleGoal*(1.0-cycleSmoothing);
            // If no causal grain remains, the reliable realtime engine always
            // takes over; never generate silence for invalid synthesis frames.
            const double safeCycleBlend = cycleFrame ? cycleBlend_ : 0.0;
            const int dryIdx = (write_ - latency_) & mask_;
            // A tagged transient is copied at the declared host delay, even
            // when both wet modes are at 100%.  Fade back to shifted audio
            // after the protected segment; only the first attack is forced
            // immediately, preserving its exact timing and amplitude.
            const bool protectedNow = transientMark_[size_t(dryIdx)] != 0;
            transientRelease_ = protectedNow ? 1.0 :
                transientRelease_ * transientReleaseStep;
            if (protectedNow) ++preservedTransientSamples_;
            float dryForGuard[2]{};
            float wetForGuard[2]{};
            float synthesizedForGuard[2]{};
            // A bright periodic vowel is not a sibilant: the original air
            // lane used only HF energy and could leak original-pitch upper
            // harmonics into tuned audio. Classify dry HF periodicity at the
            // tracked input period before deciding how much air to restore.
            float dryHigh[2]{};
            for(int c=0;c<numChannels;++c) {
                const float* ring=history_.data()+size_t(c)*capacity;
                const double dry=ring[dryIdx];
                dryLow_[c]=highBandPole_*dryLow_[c]+(1.0-highBandPole_)*dry;
                dryHigh[c]=float(dry-dryLow_[c]);
            }
            const double harmonicAir=airGuard_.push(dryHigh,numChannels,period,voiced_);
            for (int c = 0; c < numChannels; ++c) {
                const float* ring = history_.data() + size_t(c) * capacity;
                const float dry = ring[dryIdx];
                const float wet = jumping ?
                    float(overlapGain*((1.0-jumpBlend)*interpolate(ring,jumpFrom_) +
                          jumpBlend*interpolate(ring,jumpTo_))) :
                    interpolate(ring,readDelay_);
                // Only the broadband excitation is blended back from the
                // same, sample-delayed dry path. The voiced low/mid spectrum
                // remains pitch shifted. This is consonant/air preservation,
                // NOT complete formant-envelope correction.
                wetLow_[c] = highBandPole_*wetLow_[c] + (1.0-highBandPole_)*wet;
                const double noiseBand = std::clamp(noiseScore * 5.0, 0.0, 1.0);
                // Real sibilants remain aperiodic and retain air; strong
                // periodic HF energy receives up to 96% less dry injection.
                const double air = params_.airPreservation * noiseBand *
                    (1.0 - 0.96*harmonicAir);
                const double reconstructed = wet + air*((dry - dryLow_[c]) - (wet - wetLow_[c]));
                const double baseline = dry * (1.0 - effectiveMix_) + reconstructed * effectiveMix_;
                // v1.4: Restore delayed consonant air in the experimental
                // pitch-cycle path. It previously bypassed this protection,
                // even though the realtime shifter already offered it.
                cycleLow_[c] = highBandPole_*cycleLow_[c]
                    + (1.0-highBandPole_)*double(cycleOutput[c]);
                const double cycleReconstructed = double(cycleOutput[c])
                    + air*((dry-dryLow_[c])
                           -(double(cycleOutput[c])-cycleLow_[c]));
                const double experimental = dry * (1.0-effectiveMix_) + cycleReconstructed*effectiveMix_;
                const double synthesized = baseline*(1.0-safeCycleBlend) + experimental*safeCycleBlend;
                dryForGuard[c] = dry;
                wetForGuard[c] = float(reconstructed * (1.0-safeCycleBlend)
                                       + cycleReconstructed*safeCycleBlend);
                synthesizedForGuard[c] = float(synthesized);
            }
            // v2.4: spectral-envelope matching uses delayed dry only as a
            // control reference; reweights *wet PSOLA* bands, never mixing
            // original-pitch voiced harmonics into the output. The default
            // realtime engine stays byte-for-byte unchanged.
            const double envelopeStrength = (params_.experimentalCycleSynthesis &&
                !targetPending_ && voiced_ && noiseScore < 0.25 &&
                transientRelease_ < 0.01) ?
                double(params_.formantPreservation) * safeCycleBlend : 0.0;
            if(params_.experimentalCycleSynthesis && params_.formantPreservation>0.00001f) {
                // Disabled-by-default spectral feature costs no filterbank work.
                formantActive_=true;
                float correctedWet[2]{};
                formantGuard_.process(dryForGuard, wetForGuard, correctedWet,
                    numChannels, envelopeStrength, voiced_ && envelopeStrength>0.001);
                if (envelopeStrength>0.0) {
                    for(int c=0;c<numChannels;++c) {
                        const double delta = double(correctedWet[c])-double(wetForGuard[c]);
                        synthesizedForGuard[c] += float(effectiveMix_ * delta);
                        wetForGuard[c] = correctedWet[c];
                    }
                }
            } else if (formantActive_) {
                // On automation back to zero, discard old spectral gains;
                // next enable starts cleanly and ramps from unity.
                formantGuard_.reset();
                formantActive_=false;
            }
            // Shared stereo gain avoids L/R pumping. Do not change the dry
            // contract, fully wet tuning, or sample-exact protected attacks.
            const bool guardEligible = voiced_ && !targetPending_ &&
                transientRelease_ < 0.01 && noiseScore < 0.3;
            const double mixGain = mixGuard_.process(dryForGuard, wetForGuard,
                numChannels, effectiveMix_, guardEligible);
            for (int c = 0; c < numChannels; ++c) {
                output[c][i] = float(dryForGuard[c] * transientRelease_ +
                    synthesizedForGuard[c] * (1.0 - transientRelease_) *
                    (guardEligible ? mixGain : 1.0));
                if (!std::isfinite(output[c][i])) output[c][i] = 0.0f;
            }
            if (jumping && ++jumpPosition_ == jumpLength_) {
                readDelay_ = jumpTo_;
            }
            write_ = (write_ + 1) & mask_;
        }
    }
private:
    // Search the neighboring source pitch period for coherent overlap. Exact
    // period prediction alone is insufficient on real voices with vibrato,
    // modest tracker error and waveform asymmetry. Search uses ONLY samples
    // already in the ring; no lookahead or audio-thread allocations.
    double choosePhaseAlignedDestination(double from, double nominal, double period, double minDelay) noexcept {
        const int maxAdjust = std::clamp(int(std::lround(period * 0.085)), 2, 32);
        const int length = std::clamp(int(std::lround(period * 0.12)), 12, 42);
        const int capacity = mask_ + 1;
        double bestScore = 1e40, best = nominal, baseline = -1.0, chosenMismatch = 0.0;
        for (int offset = -maxAdjust; offset <= maxAdjust; ++offset) {
            const double to = nominal + offset;
            if (to < minDelay || to > capacity - 8.0) continue;
            double mismatch=0.0, power=1e-8;
            for (int k = 0; k < length; k+=2) {
                // We use recent history slightly behind the candidate position,
                // not the future overlap region, to choose the next read cycle.
                for (int ch=0; ch<channels_; ++ch) {
                    const float* ring=history_.data()+size_t(ch)*capacity;
                    const double a=interpolate(ring,from+k+3.0);
                    const double b=interpolate(ring,to+k+3.0);
                    const double d=a-b;
                    mismatch+=d*d;
                    power+=a*a+b*b;
                }
            }
            const double normalized = mismatch/power;
            if (offset == 0) baseline = normalized;
            const double normalizedOffset = double(offset)/double(maxAdjust);
            const double penalty = 0.028*normalizedOffset*normalizedOffset;
            const double score=normalized+penalty;
            if (score<bestScore) { bestScore=score; best=to; chosenMismatch=normalized; }
        }
        ++phaseJumpCount_;
        phaseJumpError_ += std::abs(best-nominal);
        mismatchBefore_ += std::max(0.0,baseline);
        mismatchAfter_ += chosenMismatch;
        return best;
    }
    // Bounded backward-looking estimate of how two cycles add in their overlap.
    // This shares a phase reference across channels and avoids per-sample analysis.
    double estimateCycleCorrelation(double from, double to, double period) const noexcept {
        const int count = std::clamp(int(std::lround(period * 0.16)), 16, 72);
        const int capacity = mask_ + 1;
        double xy=0.0, xx=1.e-12, yy=1.e-12;
        for (int k=0; k<count; k+=2) {
            for (int ch=0; ch<channels_; ++ch) {
                const float* ring=history_.data()+size_t(ch)*capacity;
                const double x=interpolate(ring,from+k+3.0);
                const double y=interpolate(ring,to+k+3.0);
                xy+=x*y; xx+=x*x; yy+=y*y;
            }
        }
        if (xx<1.e-8 || yy<1.e-8) return 1.0;
        return std::clamp(xy/std::sqrt(xx*yy), -0.85, 1.0);
    }
    float interpolate(const float* ring, double delay) const noexcept {
        // Four-point cubic Lagrange interpolation on the history ring.
        delay = std::clamp(delay, 2.0, double(mask_ - 3));
        const int d = int(std::floor(delay));
        const double u = delay - d;
        const float y0 = ring[(write_ - d + 1) & mask_];
        const float y1 = ring[(write_ - d) & mask_];
        const float y2 = ring[(write_ - d - 1) & mask_];
        const float y3 = ring[(write_ - d - 2) & mask_];
        const double a = -u*(u-1)*(u-2)/6.0;
        const double b = (u+1)*(u-1)*(u-2)/2.0;
        const double c = -(u+1)*u*(u-2)/2.0;
        const double e = (u+1)*u*(u-1)/6.0;
        return float(a*y0 + b*y1 + c*y2 + e*y3);
    }
    double rate_ = 48000.0;
    // Input-time transient tags are read with the same integer-sample delay
    // as the unprocessed signal. All storage is allocated in prepare().
    std::vector<uint8_t> transientMark_;
    std::array<float, 2> previousTransientInput_{};
    double transientDiffPower_ = 0.0, transientRelease_ = 0.0;
    int transientHold_ = 0;
    uint64_t preservedTransientSamples_ = 0;
    bool studioClock_ = false, fastStudio_ = false;
    int channels_ = 2, latency_ = 144, mask_ = 0, write_ = 0;
    double readDelay_ = 144.0, jumpFrom_ = 0, jumpTo_ = 0;
    int jumpPosition_ = 0, jumpLength_ = 0;
    double maxOverlapGain_ = 1.0;
    double jumpCorrelation_ = 1.0, minActiveSourceDelay_ = 1.0e30, maxActiveSourceDelay_ = 0.0;
    double cents_ = 0, desiredCents_ = 0, voiceMix_ = 0, effectiveMix_ = 0, f0_ = 220.0, smoothF0_ = 220.0;
    double slowInputMidi_ = 0.0;
    bool slowMidiReady_ = false;
    int vibratoTarget_ = -1;
    int dryCenteredSamples_ = 0;
    double highBandPole_ = 0.65;
    std::array<double,2> dryLow_{}, wetLow_{}, cycleLow_{};
    bool harmonicWetGateEnabled_ = true;
    bool provisionalTransitionEnabled_ = false;
    CausalCycleSynth cycleSynth_;
    CoherentMixGuard mixGuard_;
    FormantEnvelopeGuard formantGuard_;
    bool formantActive_=false;
    HarmonicAirGuard airGuard_;
    double cycleBlend_ = 0.0;
    uint64_t phaseJumpCount_ = 0;
    uint64_t cycleWrapCount_ = 0, activeWrapCount_ = 0, sourceAgeGuardHits_ = 0;
    double phaseJumpError_ = 0, mismatchBefore_ = 0, mismatchAfter_ = 0;
    bool hasF0_ = false;
    bool voiced_ = false;
    bool shortVoicedBridgeEnabled_ = true;
    double maxShortVoicedBridgeMs_ = 12.0;
    uint64_t bridgedHops_ = 0;
    bool continueCorrectionState_ = false;
    int trustedVoiceSamples_ = 0;
    double voicedEnvelope_ = 0.0;
    double voicedEnvelopeStep_ = 0.01;
    bool targetPending_ = false;
    int targetNote_ = -1;
    std::vector<float> history_;
    Parameters params_{};
    CausalPitchTracker tracker_;
    StereoPitchRouter stereoRouter_;
    CausalOnsetCue onsetCue_;
    int onsetHoldSamples_ = 0;
    bool hardTuneOnsetMemoryEnabled_ = true;
    uint64_t onsetMemoryAppliedHops_ = 0;
    uint64_t onsetAssistedTransitions_ = 0;
    TransientNoiseCue noiseCue_;
    skelly_research::NoteDecision notes_;
};
} // namespace skelly
