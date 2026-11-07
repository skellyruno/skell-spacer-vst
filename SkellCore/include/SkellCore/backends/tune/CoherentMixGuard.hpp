#pragma once
// An optional, allocation-free, shared-stereo gain guard for partially mixed
// pitch-shifted audio.  It compensates for *moderate* phase decorrelation, not
// for destructive cancellation or incorrect source/grain timing.  Crucially,
// the detector and both synthesizers remain independent of this envelope tool.
#include <algorithm>
#include <cmath>

namespace skelly {
class CoherentMixGuard {
public:
    void prepare(double sampleRate) noexcept {
        const double rate = std::clamp(std::isfinite(sampleRate) ? sampleRate : 48000.0,
                                       8000.0, 192000.0);
        envelopeCoefficient_ = std::exp(-1.0 / (0.012 * rate));
        gainCoefficient_ = std::exp(-1.0 / (0.018 * rate));
        reset();
    }
    void reset() noexcept {
        dryEnergy_ = wetEnergy_ = covariance_ = 0.0;
        gain_ = 1.0;
    }
    double gain() const noexcept { return gain_; }
    double process(const float dry[2], const float wet[2], int channels,
                   double wetMix, bool eligible) noexcept {
        channels = std::clamp(channels, 1, 2);
        const double mix = std::clamp(std::isfinite(wetMix) ? wetMix : 0.0, 0.0, 1.0);
        double dryPower = 0.0, wetPower = 0.0, cross = 0.0;
        for (int c=0; c<channels; ++c) {
            const double x = std::isfinite(dry[c]) ? dry[c] : 0.0;
            const double y = std::isfinite(wet[c]) ? wet[c] : 0.0;
            dryPower += x*x;
            wetPower += y*y;
            cross += x*y;
        }
        const double norm = 1.0 / channels;
        const double a = 1.0 - envelopeCoefficient_;
        dryEnergy_ += a * (dryPower*norm - dryEnergy_);
        wetEnergy_ += a * (wetPower*norm - wetEnergy_);
        covariance_ += a * (cross*norm - covariance_);
        double targetGain = 1.0;
        // Leave fully dry, fully wet, silent and attack-protected audio unchanged.
        // Avoid trying to compensate near-complete cancellation: that requires
        // phase alignment, not potentially huge gains.
        if (eligible && mix > 0.075 && mix < 0.925 &&
            dryEnergy_ > 1.e-7 && wetEnergy_ > 1.e-7) {
            const double observed = (1.0-mix)*(1.0-mix)*dryEnergy_ +
                                    mix*mix*wetEnergy_ +
                                    2.0*mix*(1.0-mix)*covariance_;
            const double reference = (1.0-mix)*dryEnergy_ + mix*wetEnergy_;
            if (observed > reference*0.14) {
                const double correlation = covariance_ /
                    std::sqrt(dryEnergy_*wetEnergy_);
                if (correlation < 0.75) {
                    // Allow <=3 dB restoration, and fade down smoothly as
                    // waves approach a phase-coherent match.
                    const double g = std::sqrt(reference/observed);
                    targetGain = std::clamp(g, 1.0, 1.4125);
                    const double fade = std::clamp((0.75-correlation)/0.30,0.0,1.0);
                    targetGain = 1.0 + fade*(targetGain - 1.0);
                }
            }
        }
        gain_ = gain_*gainCoefficient_ + targetGain*(1.0-gainCoefficient_);
        if (!std::isfinite(gain_)) gain_ = 1.0;
        const double towardWet = std::clamp((mix-0.075)/0.15,0.0,1.0);
        const double towardDry = std::clamp((0.925-mix)/0.15,0.0,1.0);
        return eligible ? 1.0 + (gain_-1.0)*towardWet*towardDry : 1.0;
    }
private:
    double envelopeCoefficient_ = 0.999, gainCoefficient_ = 0.999;
    double dryEnergy_=0.0, wetEnergy_=0.0, covariance_=0.0, gain_=1.0;
};
}
