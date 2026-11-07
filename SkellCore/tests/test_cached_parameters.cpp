#ifdef NDEBUG
#undef NDEBUG
#endif
#include <SkellCore/Core.h>
#include <cassert>
#include <cmath>
#include <iostream>
int main(){
    constexpr double pi=3.14159265358979323846;
    skellcore::FDNReverb a,b;
    a.prepare(48000);b.prepare(48000);
    a.setParameters(2.5f,.35f,.5f);
    b.setParameters(2.5f,.35f,.5f);
    for(int i=0;i<48000;++i){
        // Simulate a DAW re-sending unchanged automation each audio block.
        if(i%128==0)a.setParameters(2.5f,.35f,.5f);
        const float left=i==0?1.f:float(.2*std::sin(2*pi*280*i/48000.0));
        const auto x=a.processFrame(left,0.f);
        const auto y=b.processFrame(left,0.f);
        assert(std::abs(x[0]-y[0])<=1e-6f);
        assert(std::abs(x[1]-y[1])<=1e-6f);
    }
    skellcore::TruePeakLimiter c,d;
    c.prepare(48000,2);d.prepare(48000,2);
    c.setCeilingDb(-1.f);d.setCeilingDb(-1.f);
    for(int i=0;i<48000;++i){
        if(i%128==0)c.setCeilingDb(-1.f);
        const float input[2]{float(1.6*std::sin(2*pi*1100*i/48000.0)),.2f};
        float x[2]{},y[2]{};
        c.processFrame(input,x,2);d.processFrame(input,y,2);
        assert(std::abs(x[0]-y[0])<=1e-6f);
        assert(std::abs(x[1]-y[1])<=1e-6f);
    }
    std::cout<<"Cached realtime parameter updates preserve audio output\n";
}
