#ifdef NDEBUG
#undef NDEBUG
#endif
#include <SkellCore/Core.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>
#include <iostream>

// Independent copy of the previous 4-phase / 16-tap limiter logic.
// Compare its Standard output before using the optimized limiter in releases.
class LegacyLimiter {
public:
    void prepare(double rate,int channels) {
        channels_ = std::clamp(channels,1,2);
        delaySamples_=std::max(48,static_cast<int>(rate*.003));
        for(auto& b:delay_)b.assign(static_cast<std::size_t>(delaySamples_+1),0.f);
        for(int p=0;p<4;++p){
            double sum=0;
            for(int j=0;j<16;++j){
                const double offset=j-7.5+p*.25;
                const double sinc=std::abs(offset)<1.e-10?1.0:std::sin(pi*offset)/(pi*offset);
                const double window=.5-.5*std::cos(2*pi*j/15);
                kernel_[p][j]=sinc*window;sum+=kernel_[p][j];
            }
            for(auto& coeff:kernel_[p])coeff/=sum;
        }
        release_=std::exp(-1.0/(rate*.07));
        ceiling_=std::pow(10.0,-1.0/20.0);
    }
    void processFrame(const float* in,float* out,int channels){
        const int n=std::min(channels,channels_);
        double peak=0;
        for(int c=0;c<n;++c){
            const double s=std::isfinite(in[c])?in[c]:0.0;
            delay_[c][write_]=float(s);hist_[c][histWrite_]=s;
            peak=std::max(peak,std::abs(s));
            for(int phase=0;phase<4;++phase){
                double interp=0;
                for(int j=0;j<16;++j)
                    interp+=kernel_[phase][j]*hist_[c][(histWrite_-j+16)%16];
                peak=std::max(peak,std::abs(interp));
            }
        }
        histWrite_=(histWrite_+1)%16;
        const double desired=peak>ceiling_?ceiling_/peak:1.0;
        if(desired<gain_){gain_=desired;hold_=delaySamples_+16;}
        else if(hold_>0)--hold_;
        else gain_=release_*gain_+(1.0-release_)*desired;
        const std::size_t next=(write_+1)%delay_[0].size();
        for(int c=0;c<n;++c)out[c]=float(delay_[c][next]*gain_);
        write_=next;
    }
private:
    static constexpr double pi=3.1415926535897932384626433832795;
    int channels_=2,delaySamples_=144,hold_=0,histWrite_=0;
    double ceiling_=.891250938,gain_=1.0,release_=0;
    std::size_t write_=0;
    std::array<std::vector<float>,2> delay_;
    std::array<std::array<double,16>,2> hist_{};
    std::array<std::array<double,16>,4> kernel_{};
};

int main(){
    constexpr double pi=3.1415926535897932384626433832795;
    for(int rate : {44100,48000,96000,192000}){
        LegacyLimiter legacy;legacy.prepare(rate,2);
        skellcore::TruePeakLimiter current;current.prepare(rate,2);current.setCeilingDb(-1.f);
        assert(current.quality()==skellcore::TruePeakLimiter::Quality::Standard);
        double maxError=0;
        for(int i=0;i<50000;++i){
            const float in[2]{
                float(std::sin(2*pi*997*i/rate)*(i<3000?1.2:1.8)),
                float(std::sin(2*pi*2250*i/rate)*(i%17000<8500?1.6:.1))
            };
            float a[2]{},b[2]{};
            legacy.processFrame(in,a,2);current.processFrame(in,b,2);
            for(int c=0;c<2;++c)maxError=std::max(maxError,std::abs(double(a[c]-b[c])));
        }
        assert(maxError<=1.e-6);
        std::cout<<rate<<" Hz Standard parity max error "<<maxError<<"\n";
    }
}
