#include "ZeroLatencyEngine.h"
#include "unlinked_main/ZeroLatencyEngine.h"
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>
#include <cmath>
using Clock=std::chrono::steady_clock;
int main(){
 bool ok=true;
 for(double fs:{44100.,48000.,96000.,192000.}) {
  std::vector<std::unique_ptr<skell::ZeroLatencyEngine>> a;
  std::vector<std::unique_ptr<unlinked_main_skell::ZeroLatencyEngine>> b;
  for(int j=0;j<4;++j){a.push_back(std::make_unique<skell::ZeroLatencyEngine>());b.push_back(std::make_unique<unlinked_main_skell::ZeroLatencyEngine>());a.back()->prepare(fs);b.back()->prepare(fs);}
  double timeA=0.,timeB=0.;float err=0.f,diag=0.f;
  for(int block=0;block<520;++block){
   const int nCh=(block>=340&&block<380)?1:2;
   const int nKey=(block>=400&&block<410)?0:2;
   float l[128],r[128],kl[128],kr[128];
   for(int i=0;i<128;++i){const double t=(block*128+i)/fs;
    l[i]=.2f*static_cast<float>(std::sin(2.*skell::kPi*400.*t));
    r[i]=block<300?l[i]:l[i]+.08f*static_cast<float>(std::sin(2.*skell::kPi*2000.*t));
    kl[i]=.3f*static_cast<float>(std::sin(2.*skell::kPi*400.*t));kr[i]=block<320?kl[i]:.4f*static_cast<float>(std::sin(2.*skell::kPi*2000.*t));
   }
   for(size_t j=0;j<a.size();++j){
    skell::SpacerParams p;unlinked_main_skell::SpacerParams q;
    p.smart=q.smart=block>=430&&block<490?0.f:1.f;
    p.cleanKey=q.cleanKey=.8f;p.phaseProtect=q.phaseProtect=.7f;p.stereoDetail=q.stereoDetail=1.f;
    p.spectralFocus=q.spectralFocus=.5f;p.humanEar=q.humanEar=.5f;p.foresight=q.foresight=.7f;p.levelSensitivity=q.levelSensitivity=.7f;
    p.deltaListen=q.deltaListen=block>=490;p.ms=q.ms=block>=420&&block<430;
    a[j]->setParams(p);b[j]->setParams(q);
    float x[128],y[128],u[128],v[128];std::memcpy(x,l,sizeof l);std::memcpy(y,r,sizeof r);std::memcpy(u,l,sizeof l);std::memcpy(v,r,sizeof r);
    float* X[]={x,y};float* U[]={u,v};const float* K[]={kl,kr};
    auto t0=Clock::now();if(block%2)a[j]->process(X,K,nCh,nKey,128);else b[j]->process(U,K,nCh,nKey,128);auto t1=Clock::now();
    if(block%2)b[j]->process(U,K,nCh,nKey,128);else a[j]->process(X,K,nCh,nKey,128);auto t2=Clock::now();
    if(block>80&&block<300){const double first=std::chrono::duration<double,std::micro>(t1-t0).count(),second=std::chrono::duration<double,std::micro>(t2-t1).count();timeA+=block%2?first:second;timeB+=block%2?second:first;}
    for(int i=0;i<128;++i){err=std::max(err,std::abs(x[i]-u[i]));if(nCh==2)err=std::max(err,std::abs(y[i]-v[i]));if(!std::isfinite(x[i])||x[i]!=u[i]||(nCh==2&&y[i]!=v[i]))ok=false;}
    if(block%16==0){auto aa=a[j]->getSmartSnapshot();auto bb=b[j]->getSmartSnapshot();for(int i=0;i<skell::ZeroLatencyEngine::kBands;++i){diag=std::max(diag,std::max(std::abs(aa.overlap[i]-bb.overlap[i]),std::abs(aa.protection[i]-bb.protection[i])));}if(diag!=0.f)ok=false;}
   }
  }
  std::printf("%.0f Hz four centered-main Smart instances: %.2f%% saved; across divergence, mono, lost key, M/S, Classic, Delta: audio error %.9g diagnostic error %.9g\n",fs,100.*(timeB-timeA)/timeB,static_cast<double>(err),static_cast<double>(diag));
 }
 return ok?0:1;
}
