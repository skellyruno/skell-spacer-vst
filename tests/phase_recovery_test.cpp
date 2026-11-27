#include "ZeroLatencyEngine.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
static void check(bool b,const char* message){if(!b){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
 for(double fs:{44100.,48000.,96000.,192000.}){
  skell::ZeroLatencyEngine wet,delta;wet.prepare(fs);delta.prepare(fs);
  skell::SpacerParams p;p.smart=1.f;p.amount=.9f;p.attackMs=1.f;p.releaseMs=30.f;p.phaseProtect=1.f;p.stereoDetail=1.f;p.cleanKey=.5f;
  float cap=0.f,overshoot=0.f,err=0.f;const float decay=static_cast<float>(std::exp(-1./(.01*fs)));
  for(int pos=0;pos<static_cast<int>(fs*.65);pos+=128){
   float l[128],r[128],x[128],y[128],a[128],b[128],kl[128],kr[128];
   for(int i=0;i<128;++i){const double t=(pos+i)/fs;const float burst=std::fmod(t,.04)<.001?.55f:0.f;
    l[i]=x[i]=a[i]=.15f*static_cast<float>(std::sin(2.*skell::kPi*1000.*t))+.18f*static_cast<float>(std::sin(2.*skell::kPi*88.*t))+burst;
    r[i]=y[i]=b[i]=.13f*static_cast<float>(std::sin(2.*skell::kPi*1600.*t))+.18f*static_cast<float>(std::sin(2.*skell::kPi*88.*t))-burst;
    kl[i]=.3f*static_cast<float>(std::sin(2.*skell::kPi*1000.*t));kr[i]=.3f*static_cast<float>(std::sin(2.*skell::kPi*1600.*t));
   }
   p.deltaListen=false;wet.setParams(p);p.deltaListen=true;delta.setParams(p);
   float* W[]={l,r};float* D[]={x,y};const float* K[]={kl,kr};wet.process(W,K,2,2,128);delta.process(D,K,2,2,128);
   for(int i=0;i<128;++i){check(std::isfinite(l[i])&&std::isfinite(r[i])&&std::isfinite(x[i])&&std::isfinite(y[i]),"finite phase output");
    cap=std::max(std::max(std::abs(a[i]),std::abs(b[i])),cap*decay);
    overshoot=std::max(overshoot,std::max(std::abs(l[i]),std::abs(r[i]))-cap);
    if(pos+i>static_cast<int>(fs*.1))err=std::max(err,std::max(std::abs(l[i]+x[i]-a[i]),std::abs(r[i]+y[i]-b[i])));
   }
  }
  check(overshoot<2e-6f,"linked peak ceiling preserved");check(err<2e-6f,"Delta reconstruction preserved");check(wet.latencySamples()==0,"zero reported latency");
  std::printf("%.0f Hz phase ceiling error %.9g, Delta reconstruction error %.9g\n",fs,static_cast<double>(overshoot),static_cast<double>(err));
 }
}
