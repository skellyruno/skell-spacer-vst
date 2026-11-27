#include "ZeroLatencyEngine.h"
#include "prior_smart/ZeroLatencyEngine.h"
#include <chrono>
#include <cstdio>
#include <vector>
#include <memory>
using Clock=std::chrono::steady_clock;
template<typename E,typename P> void config(E& e,P& p,int k,int j) {
 p.amount=.72f; p.smooth=.3f; p.smart=1.f; p.punchProtect=.65f; p.bassProtect=.7f;
 p.adaptiveRelease=.6f; p.spectralFocus=.6f; p.vocalFocus=.5f; p.perceptualFocus=.4f;
 p.harmonicProtect=.6f; p.depthBudget=.5f; p.sonicTerritory=.7f; p.humanEar=k<100?.0f:k<220?.5f:1.f;
 p.componentProtect=.6f; p.foresight=.8f; p.pan=j==1?-.5f:0.f; p.ms=k>=420&&k<500;
 p.deltaListen=k>=520; p.freeze=k>=460&&k<480; e.setParams(p);
}
int main(){
 bool ok=true;
 for(double fs:{44100.,48000.,96000.,192000.}){
  constexpr int N=128, I=4;
  std::vector<std::unique_ptr<skell::ZeroLatencyEngine>> a;
  std::vector<std::unique_ptr<prior_skell::ZeroLatencyEngine>> b;
  for(int j=0;j<I;++j){a.push_back(std::make_unique<skell::ZeroLatencyEngine>()); b.push_back(std::make_unique<prior_skell::ZeroLatencyEngine>());a.back()->prepare(fs);b.back()->prepare(fs);}
  double usA=0.,usB=0.;float err=0.f,diag=0.f;
  for(int k=0;k<600;++k){
   float l[N],r[N],sl[N],sr[N];
   for(int i=0;i<N;++i){double t=(k*N+i)/fs; l[i]=.18f*static_cast<float>(std::sin(2*skell::kPi*82*t))+.14f*static_cast<float>(std::sin(2*skell::kPi*1000*t));r[i]=.83f*l[i]+.025f*static_cast<float>(std::sin(2*skell::kPi*5500*t)); sl[i]=k%150<100?.3f*static_cast<float>(std::sin(2*skell::kPi*1000*t))+.1f*static_cast<float>(std::sin(2*skell::kPi*5500*t)):0.f;sr[i]=.25f*sl[i];}
   for(int j=0;j<I;++j){
    skell::SpacerParams pa;prior_skell::SpacerParams pb;config(*a[j],pa,k,j);config(*b[j],pb,k,j);
    float x[N],y[N],u[N],v[N];std::memcpy(x,l,sizeof l);std::memcpy(y,r,sizeof r);std::memcpy(u,l,sizeof l);std::memcpy(v,r,sizeof r);
    float* ap[]={x,y};float* bp[]={u,v};const float* key[]={sl,sr};
    // Alternate order to reduce cache/order bias. Exclude initialization.
    auto t0=Clock::now();if(k%2)a[j]->process(ap,key,2,2,N);else b[j]->process(bp,key,2,2,N);auto t1=Clock::now();if(k%2)b[j]->process(bp,key,2,2,N);else a[j]->process(ap,key,2,2,N);auto t2=Clock::now();
    if(k>100){const double d1=std::chrono::duration<double,std::micro>(t1-t0).count(),d2=std::chrono::duration<double,std::micro>(t2-t1).count();usA+=k%2?d1:d2;usB+=k%2?d2:d1;}
    for(int i=0;i<N;++i){err=std::max(err,std::max(std::abs(x[i]-u[i]),std::abs(y[i]-v[i])));if(!std::isfinite(x[i])||x[i]!=u[i]||y[i]!=v[i])ok=false;}
    if(k%16==0){auto aa=a[j]->getSmartSnapshot();auto bb=b[j]->getSmartSnapshot();for(int i=0;i<skell::ZeroLatencyEngine::kBands;++i){diag=std::max(diag,std::max(std::abs(aa.overlap[i]-bb.overlap[i]),std::abs(aa.protection[i]-bb.protection[i])));}if(diag!=0.f)ok=false;}
   }
  }
  std::printf("%.0f Hz, four Smart instances: prior %.1f us cached %.1f us change %.2f%% audio error %.9g diagnostic error %.9g\n",fs,usB,usA,100.*(usB-usA)/usB,static_cast<double>(err),static_cast<double>(diag));
 }
 return ok?0:1;
}
