#include "ZeroLatencyEngine.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using M=skell::MixIntelligence;
static void check(bool b,const char* s){if(!b){std::fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
static int band(float hz){return static_cast<int>(std::round(8.f*std::log2(hz/20.f)-.5f));}
static float tone(int n,double fs,double hz){return static_cast<float>(std::sin(2.*skell::kPi*hz*n/fs));}
int main(){
  check(M::maskingWeight(1000.f,1200.f)>M::maskingWeight(1200.f,1000.f),"upward spread exceeds downward spread");
  check(M::maskingWeight(1000.f,5000.f)==0.f,"distant bands excluded");
  check(M::maskingNeed(.2f,.4f,1.f,0.f)>M::maskingNeed(.2f,.4f,0.f,0.f),"neighboring masker raises cut need");
  check(M::maskingNeed(.2f,.4f,0.f,1.f)<M::maskingNeed(.2f,.4f,0.f,0.f),"audible nearby key reduces need");
  check(M::componentNeed(82.f,0.f,1.f,.2f)<M::componentNeed(82.f,0.f,0.f,.2f),"prominent bass protected more than diffuse bass");
  check(M::componentNeed(4000.f,1.f,0.f,0.f)<M::componentNeed(4000.f,0.f,0.f,0.f),"local hit protection");
  check(M::componentNeed(4000.f,1.f,0.f,0.f)<M::componentNeed(4000.f,1.f,0.f,1.f),"collision limits protection");
  for(double fs:{44100.,48000.,96000.,192000.}){
    skell::ZeroLatencyEngine old,refined,disabledOld,disabledNew;
    for(auto* e:{&old,&refined,&disabledOld,&disabledNew})e->prepare(fs);
    skell::SpacerParams p; p.smart=1.f;p.amount=.65f;p.attackMs=5.f;p.releaseMs=60.f;
    disabledOld.setParams(p);p.refinedPerception=true;disabledNew.setParams(p);
    p.componentProtect=1.f;p.humanEar=0.f;refined.setParams(p);p.refinedPerception=false;old.setParams(p);
    double oldStable=0.,newStable=0.;float oldHit=0.f,newHit=0.f;int count=0;float parity=0.f;
    for(int pos=0;pos<static_cast<int>(fs*1.2);pos+=64){
      float a[64],b[64],c[64],d[64],key[64];
      for(int i=0;i<64;++i){const int n=pos+i;const double t=n/fs;
        const float hit=t>=.65&&t<.74?.7f*tone(n,fs,4200.):0.f;
        a[i]=b[i]=c[i]=d[i]=.15f*tone(n,fs,1400.)+.12f*tone(n,fs,82.)+hit;
        key[i]=.22f*tone(n,fs,1400.)+.08f*tone(n,fs,4200.);
      }
      float* aa[]={a};float* bb[]={b};float* cc[]={c};float* dd[]={d};const float* kk[]={key};
      old.process(aa,kk,1,1,64);refined.process(bb,kk,1,1,64);disabledOld.process(cc,kk,1,1,64);disabledNew.process(dd,kk,1,1,64);
      for(int i=0;i<64;++i){check(std::isfinite(a[i])&&std::isfinite(b[i]),"finite output");parity=std::max(parity,std::abs(c[i]-d[i]));}
      if(pos/fs>.66&&pos/fs<.72){auto o=old.getSmartSnapshot(),r=refined.getSmartSnapshot();oldStable+=o.protection[band(1400.f)];newStable+=r.protection[band(1400.f)];oldHit=std::max(oldHit,o.protection[band(4200.f)]);newHit=std::max(newHit,r.protection[band(4200.f)]);++count;}
    }
    check(count>0&&parity==0.f,"disabled new model exact parity");
    check(newStable<oldStable*.3,"unrelated sustained band no longer globally protected");
    check(newHit>.01f,"hit band still receives protection");
    check(refined.latencySamples()==0,"no new latency");
    std::printf("%.0f Hz unrelated-band protection %.6f -> %.6f, hit-band peak %.6f -> %.6f, disabled error %.9g\n",fs,oldStable/count,newStable/count,static_cast<double>(oldHit),static_cast<double>(newHit),static_cast<double>(parity));
  }
  std::puts("PASS: asymmetric masking and localized component protection");
}
