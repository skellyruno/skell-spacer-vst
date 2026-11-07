#pragma once
// Exact SkellyTune real-time DSP imported from skelltune-mini; use its
// declared host PDC, NOT pitch-source age. All allocations occur in prepare().
#include <SkellCore/Core.h>
#include "tune/RealtimeTuner.hpp"

namespace skellcore::backends {
class SkellyTuneBackend final : public PitchFormantBackend {
public:
    // Realtime fixed-dry reference mode only. Experimental PSOLA changes source
    // age; its delay is not safely characterized by this adapter.
    void prepare(double sampleRate, int channels) override {
        channels_ = std::clamp(channels, 1, 2);
        tuner_.prepare(sampleRate, channels_, false, false);
        tuner_.setParameters(parameters_);
    }
    void reset() noexcept override { tuner_.reset(); tuner_.setParameters(parameters_); }
    void setParameters(const skelly::Parameters& parameters) noexcept {
        parameters_ = parameters;
        tuner_.setParameters(parameters_);
    }
    const skelly::Parameters& parameters() const noexcept { return parameters_; }
    int latencySamples() const noexcept override { return tuner_.latencySamples(); }
    skelly::DelayContract outputDelayContract() const noexcept {
        return tuner_.outputDelayContract(false);
    }
    void processFrame(const float* input, float* output, int channels) noexcept override {
        if (!input || !output) return;
        float in[2] {}, out[2] {};
        for (int c=0; c < channels && c < channels_; ++c) in[c]=input[c];
        const float* inPtr[2]={in, in+1};
        float* outPtr[2]={out, out+1};
        tuner_.process(inPtr,outPtr,1,std::min(channels,channels_));
        for (int c=0; c < channels && c < channels_; ++c) output[c]=out[c];
    }
    void processBlock(const float* const* input, float* const* output, int frames,
                      int channels) noexcept override {
        tuner_.process(input,output,frames,std::min(channels,channels_));
    }
    skelly::PitchEstimate lastPitch() const noexcept { return tuner_.lastPitch(); }
private:
    skelly::RealtimeTuner tuner_;
    skelly::Parameters parameters_ {};
    int channels_ = 2;
};
} // namespace skellcore::backends
