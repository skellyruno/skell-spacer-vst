// Skell Spacer Live / Studio DSP contract; no JUCE, DAW or audio hardware.
#include <SkellCore/LiveEngine.h>
#include "../Source/SpacerDSP.h"
#include <cassert>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>

constexpr double pi = 3.14159265358979323846;
using skellcore::live::LiveDuckerParams;
using skellcore::live::LiveMultibandDucker;

struct Signal { std::vector<float> a,b; };
Signal tone(int count, int fs, float freq) {
    Signal x;
    x.a.resize(static_cast<std::size_t>(count));
    x.b.resize(static_cast<std::size_t>(count));
    for(int i=0;i<count;++i) {
        x.a[static_cast<std::size_t>(i)]=0.20f*std::sin(2.0*pi*freq*i/fs);
        x.b[static_cast<std::size_t>(i)]=x.a[static_cast<std::size_t>(i)];
    }
    return x;
}
double amp(const std::vector<float>& in, double fs, double freq) {
    double re=0,im=0;
    const auto n=static_cast<int>(in.size());
    for (int i=n/2;i<n;++i) {
        const double t=2*pi*freq*i/fs;
        re+=in[static_cast<std::size_t>(i)]*std::cos(t);
        im+=in[static_cast<std::size_t>(i)]*std::sin(t);
    }
    return 2.0*std::hypot(re,im)/(n-n/2);
}
void liveRender(LiveMultibandDucker& e, Signal& in,
                const Signal* sc, int block) {
    for (int p=0;p<static_cast<int>(in.a.size());p+=block) {
        const int n=std::min(block,static_cast<int>(in.a.size())-p);
        float* x[2]{in.a.data()+p,in.b.data()+p};
        const float* s[2]{sc?sc->a.data()+p:nullptr,sc?sc->b.data()+p:nullptr};
        e.process(x,s,2,sc?2:0,n);
    }
}
int main() {
    for(double fs : {44100.,48000.,96000.,192000.}) {
        skell::SpacerEngine studio;
        studio.prepare(fs);
        LiveMultibandDucker live;
        live.prepare(fs);
        assert(live.latencySamples()==0);
        assert(studio.latencySamples()==skell::SpacerEngine::sizeFor(fs));
        // A studio bypass remains delayed by FFT size; a live dry impulse is
        // identical and immediate, regardless of I/O host block sizes.
        float impulse[1]{1.f};float* x[1]{impulse};
        live.process(x,nullptr,1,0,1);
        assert(impulse[0]==1.f);
    }

    const int fs=48000,n=48000;
    const auto input=tone(n,fs,1000),side=tone(n,fs,1000);
    LiveDuckerParams params;params.amount=1.f;params.attackMs=5;params.releaseMs=100;
    LiveMultibandDucker a;a.prepare(fs);a.setParameters(params);
    auto out=input;liveRender(a,out,&side,256);
    const double attenuation=20*std::log10(amp(out.a,fs,1000)/amp(input.a,fs,1000));
    std::cout<<"Live 1kHz attenuation: "<<attenuation<<" dB\n";
    assert(std::isfinite(attenuation)&&attenuation<-5.0&&attenuation>-36.0);
    LiveMultibandDucker b;b.prepare(fs);b.setParameters(params);
    auto second=input;liveRender(b,second,&side,1);
    assert(second.a==out.a && second.b==out.b);

    LiveMultibandDucker bypass;bypass.prepare(fs);
    params.amount=0.f;bypass.setParameters(params);
    auto exact=input;liveRender(bypass,exact,&side,777);
    assert(exact.a==input.a && exact.b==input.b);

    float mainDb[LiveMultibandDucker::bands]{},scDb[LiveMultibandDucker::bands]{},cut[LiveMultibandDucker::bands]{};
    a.copyDisplayBands(0,mainDb,scDb,cut);
    for(int k=0;k<LiveMultibandDucker::bands;++k)
        assert(std::isfinite(mainDb[k])&&std::isfinite(scDb[k])&&std::isfinite(cut[k]));

    std::cout<<"Skell Spacer: Studio unchanged, Live intrinsic delay 0 samples, bypass and sidechain tests passed\n";
}
