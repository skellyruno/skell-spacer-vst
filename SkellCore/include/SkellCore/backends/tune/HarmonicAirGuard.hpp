#pragma once
// Causal harmonic-vs-noisy high-frequency content detector.
// Stops the consonant-air lane from reintroducing original-pitch *tonal*
// harmonics when a vocal is bright but still periodic. Uses delayed-dry
// high-band excitation ONLY for analysis; never resynthesizes those samples.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <vector>
namespace skelly {
class HarmonicAirGuard {
public:
    void prepare(double sampleRate, int channels){
        channels_=std::clamp(channels,1,2);
        rate_=std::clamp(sampleRate,8000.0,192000.0);
        int capacity=1;
        while(capacity<rate_/60.0+64.0)capacity*=2;
        history_.assign(size_t(capacity)*size_t(channels_),0.0f);
        mask_=capacity-1;
        a_=1.0-std::exp(-1.0/(rate_*0.008));
        reset();
    }
    void reset() noexcept {
        std::fill(history_.begin(),history_.end(),0.0f);
        write_=0;time_=0;
        cross_=energyNow_=energyPast_=0.0;
        periodicity_=0.0;
    }
    // float[2] high-band delayed-dry waveform from stereo-matched filters.
    // Uses a causal approximate sample period; no future reads or FFT.
    double push(const float high[2],int channels,double period,bool voiced) noexcept {
        channels=std::clamp(channels,1,channels_);
        const int lag=std::clamp(std::isfinite(period)?int(std::lround(period)):0,1,mask_-4);
        double xy=0,xx=0,yy=0;
        for(int c=0;c<channels;++c){
            const float clean=std::isfinite(high[c])?high[c]:0.0f;
            const auto offset=size_t(c)*size_t(mask_+1);
            const float previous = history_[offset+size_t((write_-lag)&mask_)];
            history_[offset+size_t(write_)]=clean;
            xy+=double(clean)*previous;
            xx+=double(clean)*clean;
            yy+=double(previous)*previous;
        }
        write_=(write_+1)&mask_;
        ++time_;
        if(!voiced || time_<uint64_t(lag+32)){
            cross_*=1.0-a_*4.0;
            energyNow_*=1.0-a_*4.0;
            energyPast_*=1.0-a_*4.0;
        }else {
            cross_+=a_*(xy-cross_);
            energyNow_+=a_*(xx-energyNow_);
            energyPast_+=a_*(yy-energyPast_);
        }
        const double corr = std::abs(cross_) /
            std::sqrt(std::max(1e-12,energyNow_*energyPast_));
        // Very quiet HF signals are uninformative; do not classify them tonal.
        const double evidence=std::clamp(energyNow_/1.e-5,0.0,1.0);
        const double target=evidence*std::clamp((corr-0.35)/0.40,0.0,1.0);
        // Smooth classifier so slight vibrato/period drift doesn't modulate air.
        periodicity_+=a_*(target-periodicity_);
        return periodicity_;
    }
    double periodicity() const noexcept {return periodicity_;}
private:
    std::vector<float> history_;
    int channels_=2,mask_=0,write_=0;
    uint64_t time_=0;
    double rate_=48000.0,a_=0.0;
    double cross_=0.0,energyNow_=0.0,energyPast_=0.0,periodicity_=0.0;
};
} // namespace skelly
