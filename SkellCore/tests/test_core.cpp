#ifdef NDEBUG
#undef NDEBUG
#endif
#include "SkellCore/Core.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main() {
    skellcore::TempoClock clock;
    clock.prepare(48000); clock.update(120, 0, true);
    clock.advance(48000); assert(std::abs(clock.beatPosition() - 2.0) < 1.e-8);
    clock.update(0, NAN, true); assert(clock.bpm() == 120);
    skellcore::PhraseTracker phrase; phrase.prepare(48000);
    int phraseStarts = 0;
    for (int i=0; i<48000; ++i) phraseStarts += phrase.processSample(.25f);
    for (int i=0; i<48000; ++i) phrase.processSample(0);
    for (int i=0; i<48000; ++i) phraseStarts += phrase.processSample(.25f);
    assert(phraseStarts == 2);
    skellcore::OnsetDetector onset; onset.prepare(48000);
    int onsets = 0;
    for (int i=0; i<4800; ++i) onsets += onset.processSample(i<2000?0.f:.8f);
    assert(onsets >= 1);
    skellcore::FDNReverb verb; verb.prepare(48000); verb.setParameters(2.0f,.2f,1.f);
    double tailEnergy = 0;
    for(int i=0;i<24000;++i) {
        auto s=verb.processFrame(i==0?1.f:0.f,0.f);
        assert(std::isfinite(s[0]) && std::isfinite(s[1]));
        tailEnergy += std::abs(s[0])+std::abs(s[1]);
    }
    assert(tailEnergy > .05);
    skellcore::TruePeakLimiter limiter; limiter.prepare(48000,2); limiter.setCeilingDb(-1.f);
    assert(limiter.latencySamples() > 0);
    for(int i=0;i<30000;++i) {
        float input[2] = {i<15000 ? 1.8f : (i%2 ? -1.7f : 1.7f), -.6f};
        float output[2] {};
        limiter.processFrame(input,output,2);
        assert(std::isfinite(output[0]) && std::isfinite(output[1]));
        assert(std::abs(output[0]) <= .892f && std::abs(output[1]) <= .892f);
    }
    skellcore::PitchFormantEngine pitch;
    float in[2]={.25f,-.4f}, out[2]={};
    pitch.processFrame(in,out,2);
    assert(!pitch.hasBackend() && pitch.latencySamples()==0);
    assert(out[0]==in[0] && out[1]==in[1]);
    std::cout<<"SkellCore portable DSP tests passed\n";
}
