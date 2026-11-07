#pragma once
// SkellCore portable C++17 DSP building blocks.
// No heap allocation occurs in process*() after prepare().
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace skellcore {
namespace detail {
inline double finite(double v) noexcept { return std::isfinite(v) ? v : 0.0; }
inline double clamp(double v, double lo, double hi) noexcept {
    return std::max(lo, std::min(hi, finite(v)));
}
inline double safeSampleRate(double value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 1000.0, 192000.0) : 48000.0;
}
inline double pole(double rate, double ms) noexcept {
    return std::exp(-1.0 / (std::max(1000.0, rate) * ms * 0.001));
}
constexpr double pi = 3.1415926535897932384626433832795;
} // namespace detail

// The host is the authority: SkellCore does not try to detect musical BPM from audio.
class TempoClock {
public:
    void prepare(double sampleRate) noexcept { rate_ = detail::safeSampleRate(sampleRate); reset(); }
    void reset() noexcept { ppq_ = 0; samples_ = 0; playing_ = false; }
    void update(double bpm, double hostPpq, bool playing) noexcept {
        if (std::isfinite(bpm) && bpm >= 20.0 && bpm <= 400.0) bpm_ = bpm;
        if (std::isfinite(hostPpq)) ppq_ = hostPpq;
        playing_ = playing;
    }
    void advance(std::size_t numSamples) noexcept {
        samples_ += static_cast<std::uint64_t>(numSamples);
        if (playing_) ppq_ += static_cast<double>(numSamples) * bpm_ / (60.0 * rate_);
    }
    double samplesPerBeat() const noexcept { return rate_ * 60.0 / bpm_; }
    double beatPosition() const noexcept { return ppq_; }
    double bpm() const noexcept { return bpm_; }
    bool isPlaying() const noexcept { return playing_; }
    std::uint64_t elapsedSamples() const noexcept { return samples_; }
private:
    double rate_ = 48000.0, bpm_ = 120.0, ppq_ = 0.0;
    std::uint64_t samples_ = 0;
    bool playing_ = false;
};

// Envelope-based activity segmentation; useful for vocal phrases, not linguistic recognition.
class PhraseTracker {
public:
    void prepare(double sampleRate) noexcept {
        rate_ = detail::safeSampleRate(sampleRate);
        attack_ = detail::pole(rate_, 7.0); release_ = detail::pole(rate_, 60.0);
        gapSamples_ = static_cast<int>(rate_ * 0.42); reset();
    }
    void reset() noexcept { envelope_ = 0; gap_ = 0; active_ = false; phraseId_ = 0; }
    void setThreshold(float linear) noexcept { threshold_ = detail::clamp(linear, 0.001, 0.5); }
    // Returns true only at the beginning of a new phrase.
    bool processSample(float value) noexcept {
        const double level = detail::clamp(std::abs(static_cast<double>(value)), 0, 32);
        const double coeff = level > envelope_ ? attack_ : release_;
        envelope_ = coeff * envelope_ + (1.0 - coeff) * level;
        if (envelope_ > threshold_) {
            gap_ = 0;
            if (!active_) { active_ = true; ++phraseId_; return true; }
        } else if (active_ && ++gap_ >= gapSamples_) {
            active_ = false; gap_ = 0;
        }
        return false;
    }
    bool active() const noexcept { return active_; }
    std::uint64_t phraseId() const noexcept { return phraseId_; }
    float envelope() const noexcept { return static_cast<float>(envelope_); }
private:
    double rate_ = 48000.0, attack_ = 0.99, release_ = 0.999, envelope_ = 0;
    double threshold_ = 0.02;
    int gap_ = 0, gapSamples_ = 20160;
    bool active_ = false;
    std::uint64_t phraseId_ = 0;
};

// Broadband onset cue. A spectral flux or pitch-aware implementation can be added later.
class OnsetDetector {
public:
    void prepare(double sampleRate) noexcept {
        const double r = detail::safeSampleRate(sampleRate);
        fastPole_ = detail::pole(r, 1.5); slowPole_ = detail::pole(r, 30.0);
        cooldownSamples_ = std::max(1, static_cast<int>(r * 0.045)); reset();
    }
    void reset() noexcept { fast_ = slow_ = 0; cooldown_ = 0; }
    bool processSample(float value) noexcept {
        const double v = detail::clamp(std::abs(static_cast<double>(value)), 0, 32);
        fast_ = fastPole_ * fast_ + (1.0 - fastPole_) * v;
        slow_ = slowPole_ * slow_ + (1.0 - slowPole_) * v;
        if (cooldown_ > 0) --cooldown_;
        if (cooldown_ == 0 && fast_ > 0.009 && fast_ > slow_ * 2.35) {
            cooldown_ = cooldownSamples_; return true;
        }
        return false;
    }
private:
    double fastPole_ = .99, slowPole_ = .999, fast_ = 0, slow_ = 0;
    int cooldown_ = 0, cooldownSamples_ = 2160;
};

// Four-line feedback delay network; allocation happens only in prepare().
class FDNReverb {
public:
    void prepare(double sampleRate) {
        ready_ = false;
        rate_ = detail::safeSampleRate(sampleRate);
        constexpr std::array<double, 4> delays { .0297, .0371, .0411, .0437 };
        for (std::size_t i = 0; i < 4; ++i) {
            lengths_[i] = std::max<std::size_t>(31, static_cast<std::size_t>(rate_ * delays[i]) | 1U);
            lines_[i].assign(lengths_[i], 0.0f);
        }
        reset();
        feedbackValid_ = false; // delay lengths changed with sample rate
        setParameters(decaySeconds_, damping_, wet_);
        ready_ = true;
    }
    void reset() noexcept { positions_.fill(0); dampingState_.fill(0); for (auto& l : lines_) std::fill(l.begin(), l.end(), 0); }
    void setParameters(float decaySeconds, float damping, float wet) noexcept {
        const float nextDecay = static_cast<float>(detail::clamp(decaySeconds, .15, 20));
        damping_ = static_cast<float>(detail::clamp(damping, 0, .98));
        wet_ = static_cast<float>(detail::clamp(wet, 0, 1));
        dryGain_ = 1.f - wet_;
        wetGain_ = wet_ * .45f;
        dampingDrive_ = 1.f - damping_;
        // FL Studio can repeat the same parameter value every audio block.
        // Skip four costly exp() calls unless the decay or sample rate changes.
        if (!feedbackValid_ || nextDecay != decaySeconds_) {
            decaySeconds_ = nextDecay;
            constexpr double logMinus60dB = -6.907755278982137052;
            for (std::size_t i = 0; i < 4; ++i)
                feedback_[i] = static_cast<float>(
                    std::exp(logMinus60dB * static_cast<double>(lengths_[i]) / (rate_ * decaySeconds_)));
            feedbackValid_ = true;
        }
    }
    std::array<float, 2> processFrame(float left, float right) noexcept {
        const float inL = std::isfinite(left) ? left : 0, inR = std::isfinite(right) ? right : 0;
        if (!ready_) return {inL, inR};
        std::array<float, 4> x {};
        for (std::size_t i = 0; i < 4; ++i) {
            x[i] = lines_[i][positions_[i]];
            dampingState_[i] = dampingDrive_ * x[i] + damping_ * dampingState_[i];
            if (std::abs(dampingState_[i]) < 1.e-20f) dampingState_[i] = 0.f;
        }
        // Orthogonal normalized Hadamard feedback matrix.
        const std::array<float, 4> h {
            (dampingState_[0] + dampingState_[1] + dampingState_[2] + dampingState_[3]) * .5f,
            (dampingState_[0] - dampingState_[1] + dampingState_[2] - dampingState_[3]) * .5f,
            (dampingState_[0] + dampingState_[1] - dampingState_[2] - dampingState_[3]) * .5f,
            (dampingState_[0] - dampingState_[1] - dampingState_[2] + dampingState_[3]) * .5f
        };
        const std::array<float, 4> drive {inL, inR, (inL + inR) * .7071f, (inL - inR) * .7071f};
        for (std::size_t i = 0; i < 4; ++i) {
            float next = drive[i] * .22f + h[i] * feedback_[i];
            if (!std::isfinite(next) || std::abs(next) < 1e-20f) next = 0;
            lines_[i][positions_[i]] = next;
            if (++positions_[i] >= lengths_[i]) positions_[i] = 0;
        }
        const float wetL = x[0] + x[1] - x[2] - x[3];
        const float wetR = x[0] - x[1] + x[2] - x[3];
        return {inL * dryGain_ + wetGain_ * wetL, inR * dryGain_ + wetGain_ * wetR};
    }
private:
    double rate_ = 48000;
    float decaySeconds_ = 2.5f, damping_ = .3f, wet_ = .25f;
    float dryGain_ = .75f, wetGain_ = .1125f, dampingDrive_ = .7f;
    bool ready_ = false, feedbackValid_ = false;
    std::array<std::vector<float>, 4> lines_;
    std::array<std::size_t, 4> lengths_ {31,31,31,31}, positions_ {};
    std::array<float, 4> feedback_ {}, dampingState_ {};
};

// Interpolated-peak lookahead limiter (Standard 4x, Economy 2x).
// Neither quality is BS.1770-certified.
// Host must report latencySamples() for delay compensation. Processes mono/stereo.
class TruePeakLimiter {
public:
    // Economy: 2 interpolated phases x 8 taps. Standard: 4 x 16.
    // Neither mode is yet BS.1770 true-peak certified.
    enum class Quality { Economy, Standard };
    void prepare(double sampleRate, int channels = 2, Quality quality = Quality::Standard) {
        ready_ = false;
        rate_ = detail::safeSampleRate(sampleRate);
        channels_ = std::clamp(channels, 1, 2);
        quality_ = quality;
        activeTaps_ = quality == Quality::Economy ? 8 : taps;
        activePhases_ = quality == Quality::Economy ? 2 : 4;
        delaySamples_ = std::max(48, static_cast<int>(rate_ * .003));
        for (auto& b : delay_) b.assign(static_cast<std::size_t>(delaySamples_) + 1, 0.f);
        for (auto& k : kernel_) k.fill(0.0);
        for (int p = 0; p < activePhases_; ++p) {
            const double offsetFraction = static_cast<double>(p) / activePhases_;
            const double centre = (activeTaps_ - 1) * .5;
            double sum = 0.0;
            for (int j = 0; j < activeTaps_; ++j) {
                const double offset = j - centre + offsetFraction;
                const double sinc = std::abs(offset) < 1.e-10 ?
                    1.0 : std::sin(detail::pi * offset) / (detail::pi * offset);
                const double window = .5 - .5 * std::cos(2 * detail::pi * j / (activeTaps_ - 1));
                kernel_[p][j] = sinc * window;
                sum += kernel_[p][j];
            }
            for (int j = 0; j < activeTaps_; ++j) kernel_[p][j] /= sum;
        }
        releasePole_ = detail::pole(rate_, 70.0);
        reset();
        ready_ = true;
    }
    void reset() noexcept {
        for (auto& b : delay_) std::fill(b.begin(), b.end(), 0.f);
        for (auto& h : history_) h.fill(0.0);
        write_ = 0; historyWrite_ = 0; gain_ = 1.0; hold_ = 0;
    }
    void setCeilingDb(float db) noexcept {
        const float clamped = static_cast<float>(detail::clamp(db, -24, 0));
        if (!ceilingValid_ || clamped != ceilingDb_) {
            ceilingDb_ = clamped;
            ceiling_ = std::pow(10.0, ceilingDb_ / 20.0);
            ceilingValid_ = true;
        }
    }
    int latencySamples() const noexcept { return ready_ ? delaySamples_ : 0; }
    bool isPrepared() const noexcept { return ready_; }
    Quality quality() const noexcept { return quality_; }
    std::size_t estimatedWorkingBytes() const noexcept {
        return 2 * (static_cast<std::size_t>(delaySamples_) + 1) * sizeof(float)
             + sizeof(history_) + sizeof(kernel_);
    }
    void processFrame(const float* input, float* output, int channels) noexcept {
        if (!input || !output) return;
        const int count = std::clamp(channels, 0, std::min(channels_, 2));
        if (count == 0) return;
        if (!ready_) {
            for (int c = 0; c < count; ++c)
                output[c] = static_cast<float>(detail::finite(input[c]));
            return;
        }
        const int h = historyWrite_;
        double peak = 0.0;
        for (int c = 0; c < count; ++c) {
            const float sample = static_cast<float>(detail::finite(input[c]));
            delay_[c][write_] = sample;
            auto& hist = history_[c];
            hist[h] = sample;
            hist[h + taps] = sample; // mirrored delay: zero modulo in hot FIR loop
            peak = std::max(peak, std::abs(static_cast<double>(sample)));
            for (int phase = 0; phase < activePhases_; ++phase) {
                const double* coeff = kernel_[phase].data();
                double value = 0.0;
                for (int j = 0; j < activeTaps_; ++j)
                    value += coeff[j] * hist[h + taps - j];
                peak = std::max(peak, std::abs(value));
            }
        }
        historyWrite_ = (h + 1) & (taps - 1);
        const double desired = peak > ceiling_ ? ceiling_ / peak : 1.0;
        if (desired < gain_) { gain_ = desired; hold_ = delaySamples_ + activeTaps_; }
        else if (hold_ > 0) --hold_;
        else gain_ = releasePole_ * gain_ + (1.0 - releasePole_) * desired;
        const std::size_t next = write_ + 1 == delay_[0].size() ? 0 : write_ + 1;
        for (int c = 0; c < count; ++c)
            output[c] = static_cast<float>(delay_[c][next] * gain_);
        write_ = next;
    }
private:
    static constexpr int taps = 16;
    double rate_ = 48000.0, ceiling_ = .891250938, gain_ = 1.0, releasePole_ = .999;
    int channels_ = 2, delaySamples_ = 144, hold_ = 0, historyWrite_ = 0;
    int activeTaps_ = taps, activePhases_ = 4;
    std::size_t write_ = 0;
    bool ready_ = false, ceilingValid_ = false;
    float ceilingDb_ = -1.f;
    Quality quality_ = Quality::Standard;
    std::array<std::vector<float>, 2> delay_;
    std::array<std::array<double, taps * 2>, 2> history_ {};
    std::array<std::array<double, taps>, 4> kernel_ {};
};

// Stable adapter boundary for a validated pitch/formant backend (e.g. SkellyTune).
// Does NOT silently substitute an unverified tuning algorithm.
struct PitchFormantBackend {
    virtual ~PitchFormantBackend() = default;
    virtual void prepare(double sampleRate, int channels) = 0; // never from audio callback
    virtual void reset() noexcept = 0;
    virtual void processFrame(const float* input, float* output, int channels) noexcept = 0;
    virtual int latencySamples() const noexcept = 0;
    // The block adapter is preferred for heavier tuned engines. The fallback
    // implements it with fixed-size stack arrays and no allocation.
    virtual void processBlock(const float* const* input, float* const* output, int frames,
                              int channels) noexcept {
        if (!input || !output || frames <= 0 || channels < 1 || channels > 2) return;
        for (int i = 0; i < frames; ++i) {
            float in[2] {}, out[2] {};
            for (int c = 0; c < channels; ++c) in[c] = input[c][i];
            processFrame(in, out, channels);
            for (int c = 0; c < channels; ++c) output[c][i] = out[c];
        }
    }
};
class PitchFormantEngine {
public:
    void prepare(double rate, int channels) {
        rate_ = rate; channels_ = std::clamp(channels, 1, 2);
        if (backend_) backend_->prepare(rate_, channels_);
    }
    // Only call while audio is suspended; backend lifetime is owned by the plugin.
    void setBackend(PitchFormantBackend* backend) noexcept { backend_ = backend; }
    void reset() noexcept { if (backend_) backend_->reset(); }
    bool hasBackend() const noexcept { return backend_ != nullptr; }
    int latencySamples() const noexcept { return backend_ ? backend_->latencySamples() : 0; }
    void processFrame(const float* input, float* output, int channels) noexcept {
        if (!input || !output || channels < 1 || channels > 2) return;
        if (backend_) backend_->processFrame(input, output, channels);
        else for (int c = 0; c < channels; ++c) output[c] = input[c];
    }
    void processBlock(const float* const* input, float* const* output, int frames,
                      int channels) noexcept {
        if (!input || !output || frames <= 0 || channels < 1 || channels > 2) return;
        if (backend_) backend_->processBlock(input, output, frames, channels);
        else for (int c = 0; c < channels; ++c)
            std::copy_n(input[c], frames, output[c]);
    }
private:
    PitchFormantBackend* backend_ = nullptr;
    double rate_ = 48000;
    int channels_ = 2;
};
} // namespace skellcore
