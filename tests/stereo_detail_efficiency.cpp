#include "ZeroLatencyEngine.h"
#include "unlinked/ZeroLatencyEngine.h"
#include <chrono>
#include <cstdio>
#include <cmath>
#include <memory>
#include <vector>
using Clock=std::chrono::steady_clock;
int main(){
 bool ok=true;
 for(double fs:{44100.,48000.,96000.,192000.}){
  std::vector<std::unique_ptr<skell::ZeroLatencyEngine>> a;
  std::vector<std::unique_ptr<unlinked_skell::ZeroLatencyEngine>> b;
  for(int j=0;j<4;++j){
   a.push_back(std::make_unique<skell::ZeroLatencyEngine>());b.push_back(std::make_unique<unlinked_skell::ZeroLatencyEngine>());
   a.back()->prepare(fs);b.back()->prepare(fs);
   skell::SpacerParams p;unlinked_skell::SpacerParams q;
   p.smart=q.smart=1.f;p.cleanKey=q.cleanKey=.8f;p.phaseProtect=q.phaseProtect=.7f;p.stereoDetail=q.stereoDetail=1.f;
   a.back()->setParams(p);b.back()->setParams(q);
  }
  double linkedTime=0.,referenceTime=0.;float error=0.f;
  for(int block=0;block<450;++block){
   float l[128],r[128],kl[128],kr[128];
   for(int i=0;i<128;++i){const double t=(block*128+i)/fs;
    l[i]=.2f*static_cast<float>(std::sin(2.*skell::kPi*400.*t));r[i]=l[i]+.08f*static_cast<float>(std::sin(2.*skell::kPi*2000.*t));
    kl[i]=.3f*static_cast<float>(std::sin(2.*skell::kPi*400.*t));kr[i]=block<320?kl[i]:.4f*static_cast<float>(std::sin(2.*skell::kPi*2000.*t));
   }
   for(size_t j=0;j<a.size();++j){
    float x[128],y[128],u[128],v[128];std::memcpy(x,l,sizeof l);std::memcpy(y,r,sizeof r);std::memcpy(u,l,sizeof l);std::memcpy(v,r,sizeof r);
    float* X[]={x,y};float* U[]={u,v};const float* K[]={kl,kr};
    auto t0=Clock::now();if(block%2)a[j]->process(X,K,2,2,128);else b[j]->process(U,K,2,2,128);auto t1=Clock::now();
    if(block%2)b[j]->process(U,K,2,2,128);else a[j]->process(X,K,2,2,128);auto t2=Clock::now();
    if(block>100&&block<320){double first=std::chrono::duration<double,std::micro>(t1-t0).count(),second=std::chrono::duration<double,std::micro>(t2-t1).count();linkedTime+=block%2?first:second;referenceTime+=block%2?second:first;}
    for(int i=0;i<128;++i){error=std::max(error,std::max(std::abs(x[i]-u[i]),std::abs(y[i]-v[i])));if(!std::isfinite(x[i])||x[i]!=u[i]||y[i]!=v[i])ok=false;}
   }
  }
  std::printf("%.0f Hz centered key four instances: %.2f%% processing time saved; full stereo transition audio error %.9g\n",fs,100.*(referenceTime-linkedTime)/referenceTime,static_cast<double>(error));
 }
 return ok?0:1;
}
