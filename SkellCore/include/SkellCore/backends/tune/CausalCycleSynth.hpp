#pragma once
// Experimental original pitch-synchronous overlap-add engine.
// Independent of external project code. It requires historical cycles,
// so it is NOT a 3-ms fixed-delay replacement for RealtimeTuner.
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace skelly {
class CausalCycleSynth {
public:
    void prepare(double sampleRate, int channels) {
        sampleRate_ = std::clamp(sampleRate, 8000.0, 192000.0);
        channels_ = std::clamp(channels, 1, 2);
        int capacity = 1;
        while (capacity < sampleRate_ * .3 + 4096) capacity <<= 1;
        history_.assign(size_t(capacity)*size_t(channels_), 0.0f);
        // Generate once on the non-realtime prepare thread. A small
        // interpolated Hann lookup eliminates trigonometry in the per-sample
        // grain loop without dropping grain-window continuity.
        for (size_t i=0;i<windowTable_.size();++i) {
            const double phase=double(i)/double(windowTable_.size()-1);
            const double h=std::sin(3.14159265358979323846*phase);
            windowTable_[i]=float(h*h);
        }
        mask_ = capacity-1;
        reset();
    }
    // Studio-only: a fixed output timeline measured in host samples. Set before
    // prepare/reset or while audio is stopped. This does not make a tuned
    // waveform sample-identical to an ordinarily delayed original waveform.
    void setClockLockedDelay(int samples) noexcept {
        lockedDelay_ = std::max(0, samples);
    }
    bool clockLocked() const noexcept { return lockedDelay_ > 0; }
    int timelineDelaySamples() const noexcept { return lockedDelay_; }
    void reset() noexcept {
        std::fill(history_.begin(),history_.end(),0.0f);
        for (auto& grain: grains_) grain = {};
        time_ = 0; nextCentre_ = 0; previousSource_ = 0;
        previousPeriod_ = 0; started_ = false; ready_ = false;
        lastSourceAge_ = 0.0; minSourceAge_ = 1.e30; maxSourceAge_ = 0.0;
        validFrames_ = activeFrames_ = 0; frameValid_ = false;
        count_ = 0; rejected_ = 0;
        fractionalMarks_ = 0; sumFractionalShift_ = 0.0;
        summedOutputAge_ = 0.0; outputAgeFrames_ = 0;
        lastOutputAge_ = 0.0;
        lowRegisterReference_=false;
        timelineOffsetSum_=0.0; timelineOffsetMaximum_=0.0;
        timelineGrains_=0; timelineOutOfBounds_=0;
        overlapScoreSum_=0; overlapScoreCount_=0; negativeOverlapCount_=0;
    }
    struct Status {
        uint64_t spawned=0, dropped=0;
        double minimumSourceAge=0, lastSourceAge=0;
        double maximumSourceAge=0;
        uint64_t validFrames=0, activeFrames=0;
        uint64_t refinedPitchMarks=0;
        double meanAbsoluteMarkRefinement=0;
        // Weighted source age across all valid synthesized samples.
        // This is NOT a host-reportable constant latency.
        double meanWeightedSourceAge=0, lastWeightedSourceAge=0;
        double meanAbsoluteTimelineOffset=0, maximumTimelineOffset=0;
        uint64_t timelineGrains=0, timelineOutOfBounds=0;
        double meanConcurrentPhaseAgreement=0.0;
        uint64_t negativeConcurrentAgreementGrains=0;
    };
    bool hasOutputFrame() const noexcept { return frameValid_; }
    bool freshSynthesisAvailable() const noexcept { return ready_; }
    Status status() const noexcept {
        return {count_,rejected_, minSourceAge_ == 1.e30?0:minSourceAge_,lastSourceAge_,
                maxSourceAge_,validFrames_,activeFrames_,fractionalMarks_,
                fractionalMarks_ ? sumFractionalShift_/double(fractionalMarks_) : 0.0,
                outputAgeFrames_ ? summedOutputAge_ / double(outputAgeFrames_) : 0.0,
                lastOutputAge_,
                timelineGrains_ ? timelineOffsetSum_/double(timelineGrains_) : 0.0,
                timelineOffsetMaximum_, timelineGrains_, timelineOutOfBounds_,
                overlapScoreCount_ ? overlapScoreSum_/double(overlapScoreCount_) : 0.0,
                negativeOverlapCount_};
    }
    // Benchmark-only A/B switch. No automation or audio-thread allocation.
    void setFractionalMarksEnabled(bool enabled) noexcept { fractionalMarksEnabled_ = enabled; }
    // Reference switch used only by offline comparisons.
    void setWindowLookupEnabled(bool enabled) noexcept { windowLookupEnabled_ = enabled; }
    // Test-only A/B: false reconstructs the v1.8 short pitch-mark matcher.
    void setWidePhaseReferenceEnabled(bool enabled) noexcept { widePhaseReferenceEnabled_ = enabled; }
    // A/B only: verify whether comparing new grains with simultaneously
    // active grains reduces phase cancellations in a given vocal corpus.
    void setOverlapCoherenceEnabled(bool enabled) noexcept { overlapCoherenceEnabled_ = enabled; }
    // Experimental: closure-impulse evidence from the local waveform derivative.
    // Unlike a neural glottal-closure estimator, this is a bounded causal proxy.
    void setClosureEvidenceEnabled(bool enabled) noexcept { closureEvidenceEnabled_ = enabled; }
    bool closureEvidenceEnabled() const noexcept { return closureEvidenceEnabled_; }
    void setStrongerConcurrentPhaseEnabled(bool enabled) noexcept { strongerConcurrentPhaseEnabled_=enabled; }

    // Pitch period and output ratio are updated causally by a tracker/controller.
    // Return value contains additive PSOLA energy with dynamic overlap normalization.
    // Caller can blend this with an appropriately delayed dry path.
    void processSample(const float input[2], float output[2], double period, double ratio, bool voiced) noexcept {
        output[0]=output[1]=0; frameValid_=false;
        if(history_.empty()) return;
        for(int c=0;c<channels_;++c)
            history_[size_t(c)*(mask_+1) + (int(time_)&mask_)] =
                std::isfinite(input[c]) ? input[c] : 0.0f;
        const bool usable = voiced && std::isfinite(period) && std::isfinite(ratio) &&
            period>=sampleRate_/850.0 && period<=sampleRate_/65.0 &&
            ratio>=0.67 && ratio<=1.50;
        if (usable) ++activeFrames_;
        if (!usable) {
            // Do not abruptly discard active grains when a vowel ends or the
            // user automates the synthesis mode. Their already-available audio
            // forms a strictly causal release tail while the host mixer fades
            // to its stable realtime fallback. No new grains are scheduled.
            started_=false;
            ready_=false;
        } else {
            if (!started_) {
                nextCentre_ = double(time_) + period;
                previousSource_=0;
                previousPeriod_=period;
                started_=true;
                ready_=false;
            }
            // One grain starts one period before its synthesis centre, with all
            // source audio (including the right-hand side) already available.
            const double half = std::clamp(period, 10.0, sampleRate_*.025);
            const double synthesisHop=std::max(4.0, period/ratio);
            while (double(time_) + 0.01 >= nextCentre_-half) {
                if (time_>=uint64_t(std::ceil(2.5*period+8))) {
                    const uint64_t prior=count_;
                    createGrain(nextCentre_,half,period);
                    if(count_>prior) ready_=true;
                }
                nextCentre_ += synthesisHop;
                if(nextCentre_<double(time_)-double(sampleRate_)*.1)
                    nextCentre_=double(time_)+period;
            }
        }
        double weights=0.0;
        double weightedSourceAge=0.0;
        for(auto& g:grains_) {
            if(!g.active)continue;
            const double phase=(double(time_)-g.start)/(2.0*g.half);
            if(phase>=1.0){g.active=false;continue;}
            if(phase<0.0)continue;
            double w=0;
            if (windowLookupEnabled_) {
                const double u=phase*double(windowTable_.size()-1);
                const int index=std::clamp(int(u),0,int(windowTable_.size())-2);
                const double f=u-index;
                w=windowTable_[size_t(index)]*(1.0-f)+windowTable_[size_t(index+1)]*f;
            } else {
                const double sinus=std::sin(3.14159265358979323846*phase);
                w=sinus*sinus;
            }
            const double src=g.sourceCenter + (double(time_)-g.centre);
            // If caller dynamically changes pitch, make sure the sampler
            // *never* reaches a not-yet-received sample.
            const double age=double(time_)-src;
            if(age<3.0 || age>double(mask_-8))continue;
            minSourceAge_=std::min(minSourceAge_,age);
            lastSourceAge_=age;
            maxSourceAge_=std::max(maxSourceAge_,age);
            for(int c=0;c<channels_;++c)
                output[c] += float(w*read(c,src));
            weights+=w;
            weightedSourceAge+=w*age;
        }
        frameValid_ = weights>1.e-7;
        if(frameValid_) {
            ++validFrames_;
            lastOutputAge_=weightedSourceAge/weights;
            summedOutputAge_+=lastOutputAge_;
            ++outputAgeFrames_;
            for(int c=0;c<channels_;++c)output[c]/=float(weights);
        }
        ++time_;
    }
private:
    struct Grain { bool active=false; double centre=0,start=0,half=0,sourceCenter=0; };
    // Four-point cubic Lagrange interpolation, matching the realtime shifter's
    // reconstruction quality. The caller guarantees sample age >= 3, so these
    // taps refer only to already received audio; no future-input dependence.
    float read(int channel,double absolute) const noexcept {
        const auto index=int64_t(std::floor(absolute));
        const double u=absolute-double(index);
        const float* r=history_.data()+size_t(channel)*size_t(mask_+1);
        const double y0=r[int(index-1)&mask_];
        const double y1=r[int(index)&mask_];
        const double y2=r[int(index+1)&mask_];
        const double y3=r[int(index+2)&mask_];
        return float((-u*(u-1.0)*(u-2.0)/6.0)*y0
                    +((u+1.0)*(u-1.0)*(u-2.0)/2.0)*y1
                    -((u+1.0)*u*(u-2.0)/2.0)*y2
                    +((u+1.0)*u*(u-1.0)/6.0)*y3);
    }
    // Compare a prospective grain with concurrently active audio at output
    // times whose source samples are already in the ring. This is *not*
    // lookahead: we evaluate future playback coordinates using past input.
    // The same score is used for both stereo channels, preventing image skew.
    double overlappingPhaseAgreement(double sourceCentre,double centre,double half) const noexcept {
        double dot=0.0,prospectivePower=0.0,oldPower=0.0;
        int observed=0;
        for(int j=0;j<7;++j) {
            const double t=double(time_)+(0.10+0.12*j)*half;
            const double incomingSource=sourceCentre + t-centre;
            const double age=double(time_)-incomingSource;
            if(age<3.0 || age>double(mask_-8))continue;
            for(int c=0;c<channels_;++c) {
                double old=0.0,weight=0.0;
                for(const auto& g:grains_) {
                    if(!g.active)continue;
                    const double u=(t-g.start)/(2.0*g.half);
                    if(u<=0.0 || u>=1.0)continue;
                    const double src=g.sourceCenter+t-g.centre;
                    const double oldAge=double(time_)-src;
                    if(oldAge<3.0 || oldAge>double(mask_-8))continue;
                    const double sinus=std::sin(3.14159265358979323846*u);
                    const double w=sinus*sinus;
                    old+=w*read(c,src);weight+=w;
                }
                if(weight<0.06)continue;
                old/=weight;
                const double incoming=read(c,incomingSource);
                dot+=old*incoming;
                oldPower+=old*old;
                prospectivePower+=incoming*incoming;
                ++observed;
            }
        }
        if(observed<4 || oldPower<1.e-9 || prospectivePower<1.e-9)return 0.0;
        return std::clamp(dot/std::sqrt(oldPower*prospectivePower),-1.0,1.0);
    }
    void createGrain(double centre,double half,double period) noexcept {
        // Source phase follows the previous analysis mark. The latest safe
        // source mark is at least one full cycle older than currently received
        // samples; search a narrow neighborhood for a phase-consistent mark.
        const double upper=double(time_)-half-6.0;
        // In studio profile each output grain has a scheduled source-time
        // anchor at centre - declaredDelay. The pitch mark may move by up to
        // half an input period to preserve cycle phase, but it cannot silently
        // drift through many old input cycles (the legacy PSOLA behaviour).
        // Without this profile the original realtime synthesis is unchanged.
        const bool locked = lockedDelay_ > 0;
        const double target=centre-double(lockedDelay_);
        if (locked && (target-half-3.0 < 0 || target+half+3.0 > double(time_))) {
            ++timelineOutOfBounds_;
            return;
        }
        double nominal=locked ? target : upper;
        if(locked && previousSource_ > 0) {
            const double periods=std::round((target-previousSource_)/period);
            nominal=previousSource_+periods*period;
            // Quantize to the nearest phase-compatible mark around the target.
            // Roundoff in changing periods cannot make it drift indefinitely.
            nominal=std::clamp(nominal,target-period*.50,target+period*.50);
        } else if(!locked && previousSource_>0) {
            const double cycles=std::max(1.0,std::floor((upper-previousSource_)/period));
            nominal=previousSource_+cycles*period;
            while(nominal>upper)nominal-=period;
            if(nominal<upper-2.5*period)nominal=upper-period;
        }
        const int radius=std::clamp(int(period*.08),2,24);
        double source=locked ? nominal : std::min(nominal,upper);
        // Choose an energy peak in a causal neighborhood, but preserve
        // inter-grain pitch-phase continuity by penalizing large deviation.
        // Source search is optional until at least one historical pitch mark exists.
        if(previousSource_>0) {
            // v1.9: compare enough of the waveform to see the *fundamental*.
            // The former 8..32-sample segment spans only 0.67 ms at 48 kHz;
            // on 80..150 Hz vocals it can lock to upper-harmonic phase and
            // choose poorly aligned glottal cycles. Use a bounded 0.7-period
            // symmetric reference, sampled sparsely at <= 48 points. This
            // remains causal and has a constant worst-case inner-loop budget.
            // Preserve v1.8's shorter matcher on higher pitches where the
            // longer vowel-context window brings little benefit and can smear
            // rapidly changing harmonic envelopes.
            // A broader sweep found a regression at 175 Hz: use the longer
            // pitch-mark reference only on genuinely low vocals. Hysteresis
            // prevents vibrato near the crossover from toggling the matcher
            // on every grain and producing unnecessary phase modulation.
            const double pitchHz=sampleRate_/period;
            if(pitchHz<132.0) lowRegisterReference_=true;
            else if(pitchHz>148.0) lowRegisterReference_=false;
            const bool useWide=widePhaseReferenceEnabled_ && lowRegisterReference_;
            const int window = useWide ?
                std::clamp(int(std::lround(period*0.70)), 32, 1024) :
                std::clamp(int(period*.10),8,32);
            const int step = useWide ? std::max(2, (window + 47) / 48) : 2;
            const int first = useWide ? -window/2 : 0;
            double best=-1.e30;
            int bestOffset=0;
            // Correlation is sampled at integer offsets, but the source
            // period usually contains fractional samples. Do not quantize
            // the chosen mark: a consistent fractional-phase error can
            // accumulate into audible detuning or phase jitter over a vowel.
            // This is an independently implemented variant of a technique
            // also discussed in the MIT-licensed HelixTune PSOLA reference.
            std::array<double,49> scores{};
            scores.fill(-1.e30);
            // Source reference does not change across candidates. Cache each
            // channel's reference once per grain to avoid ~50x redundant
            // cubic history interpolations in the hot pitch-mark search.
            std::array<std::array<double,48>,2> reference{};
            double referenceEnergy=1.e-10;
            int referenceCount=0;
            for(int j=0; j<window && referenceCount<48; j+=step) {
                for(int channel=0;channel<channels_;++channel) {
                    const double v=read(channel,previousSource_+first+j);
                    reference[size_t(channel)][size_t(referenceCount)]=v;
                    referenceEnergy+=v*v;
                }
                ++referenceCount;
            }
            for(int d=-radius;d<=radius;++d){
                const double s=nominal+d;
                if(s+half+3>double(time_) || s-half-3<0)continue;
                // No opportunistic drift into very old cycles in clock mode.
                if(locked && std::abs(s-target)>period*.59)continue;
                double xy=0,yy=1.e-10;
                for(int index=0;index<referenceCount;++index){
                    const int relative=first+index*step;
                    for (int channel=0; channel<channels_; ++channel) {
                        const double old=reference[size_t(channel)][size_t(index)];
                        const double now=read(channel,s+relative);
                        xy+=old*now;yy+=now*now;
                    }
                }
                const double corr=xy/std::sqrt(referenceEnergy*yy);
                // The modest position penalty discourages jumping between
                // neighboring harmonic maxima when phase evidence is weak.
                // Before committing a new grain, compare it to the actual
                // already-playing grains at several *future output indices*.
                // The corresponding SOURCE samples are all historical, and
                // every interpolator tap is bounded by what has arrived now.
                // This avoids choosing a pitch mark that looks similar to an
                // earlier cycle but cancels against the current overlap.
                const double concurrent = (overlapCoherenceEnabled_ && locked && pitchHz<140.0) ?
                    overlappingPhaseAgreement(s, centre, half) : 0.0;
                // A glottal-closure candidate often includes a sharp, coherent
                // waveform slope. Compare the slope shape across the old and
                // new grain using already-captured history. This is supporting
                // evidence only: it cannot override poor period correlation.
                double closureMatch=0.0;
                if(closureEvidenceEnabled_ && locked && pitchHz < 200.0) {
                    double dx=0.0, xx=1.e-9, yy=1.e-9;
                    const double stride=std::max(1.0, period/20.0);
                    for(int t=-3;t<=3;++t) {
                        const double off=t*stride;
                        for(int c=0;c<channels_;++c) {
                            const double a=read(c,previousSource_+off+1.0)-read(c,previousSource_+off-1.0);
                            const double b=read(c,s+off+1.0)-read(c,s+off-1.0);
                            dx+=a*b; xx+=a*a; yy+=b*b;
                        }
                    }
                    closureMatch=std::clamp(dx/std::sqrt(xx*yy),-1.0,1.0);
                }
                const double quality=corr + (strongerConcurrentPhaseEnabled_ ? 0.18 : 0.08)*concurrent
                    + 0.045*closureMatch - 0.12*std::abs(d)/radius;
                scores[size_t(d+radius)]=quality;
                if(quality>best){best=quality;source=s;bestOffset=d;}
            }
            // Quadratic interpolation of the local correlation maximum.
            // Only refine an interior, concave, sufficiently reliable peak;
            // never extrapolate beyond searched samples or the causal buffer.
            if (fractionalMarksEnabled_ && bestOffset>-radius && bestOffset<radius && best>0.15) {
                const size_t j=size_t(bestOffset+radius);
                const double l=scores[j-1],m=scores[j],r=scores[j+1];
                const double curvature=l-2.0*m+r;
                if (std::isfinite(l) && std::isfinite(r) && curvature < -1.e-7) {
                    const double fractional=std::clamp(0.5*(l-r)/curvature,-0.5,0.5);
                    const double refined=source+fractional;
                    if(refined-half-3>=0.0 && refined+half+3<=double(time_) &&
                       (!locked || std::abs(refined-target)<=period*.59)) {
                        source=refined;
                        ++fractionalMarks_;
                        sumFractionalShift_+=std::abs(fractional);
                    }
                }
            }
        }
        if (locked && period>sampleRate_/140.0 && previousSource_>0) {
            const double agreement=overlappingPhaseAgreement(source,centre,half);
            overlapScoreSum_+=agreement;
            ++overlapScoreCount_;
            if (agreement < -0.2) ++negativeOverlapCount_;
        }
        if (locked) {
            const double deviation=std::abs(centre-source-double(lockedDelay_));
            if (deviation > period*.60) { ++timelineOutOfBounds_; return; }
            timelineOffsetSum_+=deviation;
            timelineOffsetMaximum_=std::max(timelineOffsetMaximum_,deviation);
            ++timelineGrains_;
        }
        for(auto& g:grains_) {
            if(!g.active){g={true,centre,centre-half,half,source};
                previousSource_=source;previousPeriod_=period;++count_;
                return;}
        }
        ++rejected_;
    }
    std::vector<float> history_;
    std::array<float,4097> windowTable_{};
    std::array<Grain,16> grains_{};
    double sampleRate_=48000.0, nextCentre_=0, previousSource_=0, previousPeriod_=0;
    uint64_t time_=0,count_=0,rejected_=0;
    int channels_=1, mask_=0;
    bool started_=false, frameValid_=false, ready_=false;
    double minSourceAge_=1.e30,lastSourceAge_=0.0,maxSourceAge_=0.0;
    uint64_t validFrames_=0,activeFrames_=0;
    uint64_t fractionalMarks_=0;
    double sumFractionalShift_=0.0;
    double summedOutputAge_=0.0, lastOutputAge_=0.0;
    uint64_t outputAgeFrames_=0;
    bool fractionalMarksEnabled_=true, windowLookupEnabled_=true;
    bool widePhaseReferenceEnabled_=true;
    bool closureEvidenceEnabled_ = false;
    bool strongerConcurrentPhaseEnabled_=false;
    bool overlapCoherenceEnabled_=true;
    bool lowRegisterReference_=false;
    int lockedDelay_=0;
    double timelineOffsetSum_=0.0, timelineOffsetMaximum_=0.0;
    uint64_t timelineGrains_=0, timelineOutOfBounds_=0;
    double overlapScoreSum_=0;
    uint64_t overlapScoreCount_=0,negativeOverlapCount_=0;
};
} // namespace skelly
