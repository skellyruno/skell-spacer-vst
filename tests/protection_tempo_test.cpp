#include "ZeroLatencyEngine.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using skell::ProtectionCurve;
static void check(bool v,const char* s){if(!v){std::fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
int main(){
  ProtectionCurve::Nodes n{};n[2]=1.f;n[3]=.5f;
  check(ProtectionCurve::at(n,1.f)==1.f&&ProtectionCurve::at(n,1.25f)==.75f,"curve interpolation");
  check(ProtectionCurve::syncedMs(150.f,120.f)==125.f,"120 BPM sixteenth");
  check(ProtectionCurve::syncedMs(150.f,0.f)==150.f,"no tempo exact fallback");
  for(double fs:{44100.,48000.,96000.,192000.}){
    skell::ZeroLatencyEngine base,protectedE,tempo,noTempo,classicA,classicB,normal;
    for(auto* e:{&base,&protectedE,&tempo,&noTempo,&classicA,&classicB,&normal})e->prepare(fs);
    skell::SpacerParams p;p.smart=1.f;p.amount=.65f;p.releaseMs=250.f;p.attackMs=5.f;
    base.setParams(p);normal.setParams(p);
    p.protect.fill(1.f);protectedE.setParams(p);
    p.protect.fill(0.f);p.tempoRecovery=1.f;p.hostBpm=240.f;tempo.setParams(p);
    p.hostBpm=0.f;noTempo.setParams(p);
    p.smart=0.f;p.protect.fill(1.f);classicA.setParams(p);
    p.protect.fill(0.f);p.tempoRecovery=0.f;classicB.setParams(p);
    double a=0.,b=0.,dry=0.;float diff=0.f,classicDiff=0.f;
    for(int pos=0;pos<static_cast<int>(fs*1.2);pos+=128){
      float x[128],y[128],z[128],t[128],q[128],r[128],s[128],k[128];
      for(int i=0;i<128;++i){
        const float v=.2f*static_cast<float>(std::sin(2.*skell::kPi*1000.*(pos+i)/fs));
        x[i]=y[i]=z[i]=t[i]=q[i]=r[i]=s[i]=v;k[i]=v;
      }
      float* A[]={x},*B[]={y},*C[]={z},*D[]={t},*E[]={q},*F[]={r},*G[]={s};
      const float* K[]={k};
      base.process(A,K,1,1,128);protectedE.process(B,K,1,1,128);tempo.process(C,K,1,1,128);
      noTempo.process(D,K,1,1,128);classicA.process(E,K,1,1,128);classicB.process(F,K,1,1,128);normal.process(G,K,1,1,128);
      for(int i=0;i<128;++i){
        check(std::isfinite(y[i])&&std::isfinite(z[i]),"finite output");
        diff=std::max(diff,std::abs(t[i]-s[i]));classicDiff=std::max(classicDiff,std::abs(q[i]-r[i]));
        if(pos>static_cast<int>(fs*.8)){a+=x[i]*x[i];b+=y[i]*y[i];dry+=k[i]*k[i];}
      }
    }
    check(diff==0.f&&classicDiff==0.f,"fallback and Classic bit parity");
    check(b>dry*.98&&a<dry*.8,"full brush retains protected tone");
    check(protectedE.latencySamples()==0,"zero latency");
    std::printf("%.0f Hz full protection %.4f%% retained vs %.4f%% normal, fallback %.9g, Classic %.9g\n",fs,100.*b/dry,100.*a/dry,static_cast<double>(diff),static_cast<double>(classicDiff));
  }
}
