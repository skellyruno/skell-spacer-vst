#include "ZeroLatencyEngine.h"
#include "BaselineZeroLatencyEngine.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <random>
#include <vector>
#include <memory>
using Clock = std::chrono::steady_clock;
int main() {
  constexpr int N=128, instances=4;
  bool ok=true;
  for(double rate:{44100.,48000.,96000.,192000.}) {
    std::vector<std::unique_ptr<skell::ZeroLatencyEngine>> a;
    std::vector<std::unique_ptr<baseline_skell::ZeroLatencyEngine>> b;
    for(int j=0;j<instances;++j) {
      a.emplace_back(std::make_unique<skell::ZeroLatencyEngine>());
      b.emplace_back(std::make_unique<baseline_skell::ZeroLatencyEngine>());
      a.back()->prepare(rate); b.back()->prepare(rate);
    }
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> noise(-.02f,.02f);
    double oldUs=0,newUs=0; float error=0, displayError=0;
    for(int k=0;k<600;++k) {
      float l[N],r[N],sl[N],sr[N];
      for(int i=0;i<N;++i) {
        double t=(k*N+i)/rate;
        l[i]=.16f*float(std::sin(2*skell::kPi*110*t))+.09f*float(std::sin(2*skell::kPi*1100*t))+noise(rng);
        r[i]=.8f*l[i]+noise(rng);
        sl[i]=sr[i]=k%160<100?.35f*float(std::sin(2*skell::kPi*330*t)):0.f;
      }
      for(int j=0;j<instances;++j) {
        skell::SpacerParams p; baseline_skell::SpacerParams q;
        p.amount=q.amount=j==0?0.f:.7f;
        p.smooth=q.smooth=k<200?0.f:k<400?.3f:1.f;
        p.attackMs=q.attackMs=k<300?5.f:20.f;
        p.releaseMs=q.releaseMs=k<300?100.f:300.f;
        p.pan=q.pan=j==1?-.6f:0.f;
        p.ms=q.ms=k>=400;
        p.freeze=q.freeze=k>=460&&k<510;
        p.power=q.power=k<550;
        a[j]->setParams(p); b[j]->setParams(q);
        float x[N],y[N],u[N],v[N];
        std::memcpy(x,l,sizeof l); std::memcpy(y,r,sizeof r);
        std::memcpy(u,l,sizeof l); std::memcpy(v,r,sizeof r);
        float* out[2]={x,y};float* ref[2]={u,v}; const float* key[2]={sl,sr};
        int nc=k>=500&&k<520?1:2, ns=k<530?2:0;
        auto t0=Clock::now();b[j]->process(ref,key,nc,ns,N);auto t1=Clock::now();
        a[j]->process(out,key,nc,ns,N);auto t2=Clock::now();
        oldUs+=std::chrono::duration<double,std::micro>(t1-t0).count();
        newUs+=std::chrono::duration<double,std::micro>(t2-t1).count();
        for(int i=0;i<N;++i) {
          if(!std::isfinite(x[i])||!std::isfinite(y[i])||x[i]!=u[i]||y[i]!=v[i])ok=false;
          error=std::max(error,std::max(std::abs(x[i]-u[i]),std::abs(y[i]-v[i])));
        }
        if(k%40==0) {
          float m[256],s[256],c[256],bm[256],bs[256],bc[256];
          a[j]->getDisplay(m,s,c);b[j]->getDisplay(bm,bs,bc);
          for(int i=0;i<256;++i) {
            if(!std::isfinite(c[i])||std::abs(c[i]-bc[i])>1.e-5f||m[i]!=bm[i]||s[i]!=bs[i])ok=false;
            displayError=std::max(displayError,std::abs(c[i]-bc[i]));
          }
        }
        if(a[j]->latencySamples()!=0)ok=false;
      }
    }
    std::printf("%.0f Hz, four instances, baseline %.1f us, optimized %.1f us, audio max error %.9g, display max error %.9g\n",rate,oldUs,newUs,error,displayError);
  }
  return ok?0:1;
}
