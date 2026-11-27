#include "ZeroLatencyEngine.h"
#include "prior_smart/ZeroLatencyEngine.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
static void check(bool ok,const char* msg){if(!ok){std::fprintf(stderr,"FAIL: %s\n",msg);std::exit(1);}}
int main(){
 using K=skell::KeyIntelligence;
 check(K::phraseHold(.8f,.001f,.01f,1.f,0.f,.99f)==0.f,"tail cancellation");
 check(K::phraseHold(0.f,.001f,.0001f,1.f,.9f,.99f)==.9f,"quiet onset rescue");
 check(K::phraseHold(.9f,.001f,.001f,1.f,0.f,.99f)>.8f,"phrase hold");
 check(K::phraseHold(0.f,1e-12f,0.f,1.f,1.f,.99f)==0.f,"noise floor");
 for(double fs:{44100.,48000.,96000.,192000.}) {
   skell::ZeroLatencyEngine next,normal;
   prior_skell::ZeroLatencyEngine old;
   next.prepare(fs);normal.prepare(fs);old.prepare(fs);
   skell::SpacerParams p;p.smart=1.f;p.amount=.65f;p.attackMs=5.f;p.releaseMs=35.f;p.smooth=.1f;
   normal.setParams(p);p.cleanKey=1.f;next.setParams(p);
   prior_skell::SpacerParams q;q.smart=p.smart;q.amount=p.amount;q.attackMs=p.attackMs;q.releaseMs=p.releaseMs;q.smooth=p.smooth;q.cleanKey=p.cleanKey;old.setParams(q);
   // Loud vowel -> key gap -> quiet vowel, same underlying frequency. An
   // arbitrary constructed phrase, not evidence of singing classification.
   double a=0.,b=0.,c=0.,dry=0.;
   for(int pos=0;pos<static_cast<int>(fs*1.35);pos+=128){
     float x[128],y[128],z[128],k[128],input[128];
     for(int i=0;i<128;++i){const double t=(pos+i)/fs; const float f=static_cast<float>(std::sin(2.*skell::kPi*1000.*t));
       x[i]=y[i]=z[i]=input[i]=.2f*f;
       k[i]=(t<.65?.4f:t<.95?0.f:.044f)*f;
     }
     float* X[]={x};float* Y[]={y};float* Z[]={z};const float* Kp[]={k};
     next.process(X,Kp,1,1,128);old.process(Y,Kp,1,1,128);normal.process(Z,Kp,1,1,128);
     for(int i=0;i<128;++i){const double t=(pos+i)/fs;
       check(std::isfinite(x[i])&&std::isfinite(y[i])&&std::isfinite(z[i]),"finite phrase");
       if(t>1.08&&t<1.20){a+=x[i]*x[i];b+=y[i]*y[i];c+=z[i]*z[i];dry+=input[i]*input[i];}
     }
   }
   std::printf("%.0f Hz quiet phrase retained power: prior %.5f%% candidate %.5f%% normal %.5f%%\n",fs,100.*b/dry,100.*a/dry,100.*c/dry);
   check(a<b*.98,"new quiet phrase receives stronger cut than prior Clean Key");
   check(a>=c*.999,"clean key never more attenuation than normal on test phrase");
 }
}
