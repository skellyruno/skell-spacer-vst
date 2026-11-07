#ifdef NDEBUG
#undef NDEBUG
#endif
#include <SkellCore/Core.h>
#include <SkellCore/backends/SkellyTuneBackend.h>
#include <SkellCore/backends/deesser/PhraseAwareDetector.h>
#include <SkellCore/backends/master/MasteringEngine.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
int main() {
    using namespace skellcore;
    // Exact de-esser phrase tracker: learn one sibilance event per phrase.
    deesser::PhraseAwareDetector deesser;
    deesser.prepare(48000);
    deesser::PhraseAwareDetector::Decision last;
    for (int i = 0; i < 24000; ++i) {
        float voice = .12f, sib = (i > 4000 && i < 13000) ? .08f : .0001f;
        last = deesser.process(voice, sib, -24, 4);
    }
    assert(last.learnedEvents >= 1 && last.ready);
    // Production tuner backend is run block-wise: no change to its DSP.
    backends::SkellyTuneBackend tuned;
    PitchFormantEngine pitch;
    pitch.setBackend(&tuned);
    pitch.prepare(48000, 2);
    assert(pitch.hasBackend() && pitch.latencySamples() > 0);
    skelly::Parameters parameters;
    parameters.speedMs = 0; parameters.pitchClasses = 0x0fff;
    tuned.setParameters(parameters);
    constexpr int block=256;
    std::vector<float> a(block), b(block), outA(block), outB(block);
    for (int n = 0; n < 32; ++n) {
        for (int i = 0; i < block; ++i) {
            float s = .2f * std::sin(2*3.141592653589793*180.0*(i+n*block)/48000.0);
            a[i]=s; b[i]=s*.85f;
        }
        const float* input[2]={a.data(),b.data()};
        float* output[2]={outA.data(),outB.data()};
        pitch.processBlock(input,output,block,2);
        for (int i=0;i<block;++i) {
            assert(std::isfinite(outA[i]) && std::isfinite(outB[i]));
            assert(std::abs(outA[i])<8 && std::abs(outB[i])<8);
        }
    }
    // Mastering code is *sampled at 4x* externally; never claim true peak
    // certification from this inner engine in isolation.
    master::MasteringEngine master;
    master.prepare(192000,2,192);
    master::MasterParameters mp {};
    master.setParameters(mp);
    for (int i=0;i<8000;++i) {
        auto y=master.processFrame(.2f, -.15f);
        assert(std::isfinite(y.left) && std::isfinite(y.right));
    }
    std::cout<<"Production backend adapter smoke tests passed\n";
}
