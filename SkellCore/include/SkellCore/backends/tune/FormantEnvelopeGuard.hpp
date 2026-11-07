#pragma once
// Causal, no-lookahead, stereo-linked spectral-envelope correction for
// the pitch-synchronous wet signal. Copyright 2026 Skelly Audio.
// This is a broad-band formant-envelope *proxy*, not an LPC formant tracker.
// The delayed input is used for envelope measurement only: no dry samples
// are mixed into the corrected wet output by this component.
#include <algorithm>
#include <array>
#include <cmath>

namespace skelly {
class FormantEnvelopeGuard {
public:
    static constexpr int bands = 5;
    void prepare(double sampleRate, int channels) noexcept {
        channels_ = std::clamp(channels, 1, 2);
        const double rate=std::clamp(sampleRate, 8000.0, 192000.0);
        constexpr double edges[]{350.0, 800.0, 1550.0, 3150.0};
        for(int i=0;i<4;++i) poles_[i]=std::exp(-2.0*3.14159265358979323846*edges[i]/rate);
        powerStep_=1.0-std::exp(-1.0/(rate*0.040));
        gainStep_=1.0-std::exp(-1.0/(rate*0.015));
        reset();
    }
    void reset() noexcept {
        for(auto& channel: dryLow_) channel.fill(0.0);
        for(auto& channel: wetLow_) channel.fill(0.0);
        dryPower_.fill(0.0); wetPower_.fill(0.0);
        gains_.fill(1.0); maximumGain_=1.0; minimumGain_=1.0;
    }
    // All channels must be processed together so shared gains preserve L/R.
    // strength=0 is a strict bit-identical wet pass-through; values up to 1
    // allow only moderate (+/-3.5dB) spectral correction. When unvoiced,
    // continue tracking but smoothly return the gain to unity.
    void process(const float delayedDry[2], const float wet[2], float output[2],
                 int channels, double strength, bool voiced) noexcept {
        channels=std::clamp(channels,1,channels_);
        strength=std::clamp(std::isfinite(strength)?strength:0.0,0.0,1.0);
        std::array<std::array<double,bands>,2> wetBands{};
        std::array<double,bands> dp{},wp{};
        for(int c=0;c<channels;++c){
            const double x=std::isfinite(delayedDry[c])?delayedDry[c]:0.0;
            const double y=std::isfinite(wet[c])?wet[c]:0.0;
            double dryPrev=0.0,wetPrev=0.0;
            for(int k=0;k<4;++k){
                dryLow_[c][k] = poles_[k]*dryLow_[c][k]+(1.0-poles_[k])*x;
                wetLow_[c][k] = poles_[k]*wetLow_[c][k]+(1.0-poles_[k])*y;
                const double db=dryLow_[c][k]-dryPrev;
                const double wb=wetLow_[c][k]-wetPrev;
                dp[k]+=db*db; wp[k]+=wb*wb;
                wetBands[c][k]=wb;
                dryPrev=dryLow_[c][k]; wetPrev=wetLow_[c][k];
            }
            const double db=x-dryPrev,wb=y-wetPrev;
            dp[4]+=db*db; wp[4]+=wb*wb;
            wetBands[c][4]=wb;
        }
        // Shared channel energy rather than mono summing: antiphase
        // stereo vocals cannot cancel the controller's analysis.
        for(int k=0;k<bands;++k){
            dryPower_[k] += powerStep_*(dp[k]/channels-dryPower_[k]);
            wetPower_[k] += powerStep_*(wp[k]/channels-wetPower_[k]);
            double intended=1.0;
            // Avoid aggressive correction at fundamental and breath bands.
            // Require meaningful energy in both input and wet bands so gaps
            // cannot trigger arbitrary boost from a tiny denominator.
            if(voiced && strength>0.0 && k>=1 && k<=3 &&
               dryPower_[k]>2.e-7 && wetPower_[k]>2.e-7){
                const double envelopeRatio=std::sqrt(dryPower_[k]/wetPower_[k]);
                // Capped to 3.5dB regardless of spectral mismatch.
                const double limited=std::clamp(envelopeRatio,0.668,1.497);
                intended=1.0+strength*(limited-1.0);
            }
            gains_[k] += gainStep_*(intended-gains_[k]);
            gains_[k]=std::clamp(gains_[k],0.668,1.497);
            maximumGain_=std::max(maximumGain_,gains_[k]);
            minimumGain_=std::min(minimumGain_,gains_[k]);
        }
        for(int c=0;c<channels;++c){
            // Critical: exact bypass and no extra phase coloration with zero
            // correction. Output is wet plus the equalized wet *residual*.
            if(strength<=0.0){output[c]=std::isfinite(wet[c])?wet[c]:0.0f;continue;}
            double result=double(wet[c]);
            for(int k=1;k<=3;++k) result += (gains_[k]-1.0)*wetBands[c][k];
            output[c]=std::isfinite(result)?float(std::clamp(result,-8.0,8.0)):0.0f;
        }
        for(int c=channels;c<2;++c) output[c]=0.0f;
    }
    double maximumObservedGain() const noexcept { return maximumGain_; }
    double minimumObservedGain() const noexcept { return minimumGain_; }
    double currentGain(int band) const noexcept { return gains_[std::clamp(band,0,bands-1)]; }
private:
    int channels_=2;
    std::array<double,4> poles_{};
    std::array<std::array<double,4>,2> dryLow_{},wetLow_{};
    std::array<double,bands> dryPower_{},wetPower_{},gains_{};
    double powerStep_=0.0,gainStep_=0.0;
    double maximumGain_=1.0,minimumGain_=1.0;
};
} // namespace skelly
