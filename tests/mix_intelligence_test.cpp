#include "MixIntelligence.h"
#include "ZeroLatencyEngine.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using M=skell::MixIntelligence;
static void check(bool b,const char* s){if(!b){std::fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
int main(){
  check(M::territory(1.f,1.f,1.f,1.f,0)==1.f,"center key remains linked");
  check(M::territory(1.f,1.f,1.f,0.f,0)==1.f,"conflicting left preserved");
  check(M::territory(1.f,1.f,1.f,0.f,1)<.21f,"opposite side protected");
  check(M::erb(8000.f)>M::erb(100.f),"ERB widens");
  check(M::auditoryWeight(1000.f,1000.f)==1.f,"auditory center");
  check(M::auditoryWeight(1000.f,5000.f)==0.f,"distant bands excluded");
  check(M::component(60.f,0.f,0.f)<M::component(60.f,0.f,1.f),"protect bass unless conflict");
  check(M::attack(.9f,1.f,1.f)<.9f && M::rising(0.f,1.f)==0.f,"causal attack only");
  double maxError=0.; float maxGain=1.f;
  for(int i=-100;i<=100;++i) for(int j=-100;j<=100;++j){
    const float l=static_cast<float>(i)/100.f,r=static_cast<float>(j)/100.f;
    const auto y=M::steer(l,r,.174532925f);
    const double error=std::abs(static_cast<double>(y.l*y.l+y.r*y.r-l*l-r*r));
    maxError=std::max(maxError,error);
    const float g=M::redistribute(l*l,r*r); maxGain=std::max(maxGain,g);
    check(std::isfinite(g)&&g>=1.f&&g<=1.123f,"redistribution capped");
    check((g*g-1.f)*r*r<=l*l+1e-6f,"restoration within removed power");
  }
  check(maxError<1e-6,"orthogonal steering energy");
  // Compare enabled territory to identical parent settings with a unilateral key.
  // Main stereo tones are identical; energy retained on the non-key channel
  // must increase, while the key channel remains subject to normal attenuation.
  for(double fs:{44100.,48000.,96000.,192000.}){
    skell::ZeroLatencyEngine base,smart; base.prepare(fs); smart.prepare(fs);
    skell::SpacerParams p; p.amount=.5f;p.smart=1.f;base.setParams(p);
    p.sonicTerritory=1.f;smart.setParams(p);
    double left=0.,right=0.,baseR=0.;
    for(int pos=0;pos<static_cast<int>(fs);pos+=128){
      float al[128],ar[128],bl[128],br[128],kl[128],kr[128]={};
      for(int i=0;i<128;++i){const float v=.25f*static_cast<float>(std::sin(2.*skell::kPi*1000.*(pos+i)/fs)); al[i]=ar[i]=bl[i]=br[i]=kl[i]=v;}
      float* a[]={al,ar};float* b[]={bl,br};const float* k[]={kl,kr};
      base.process(a,k,2,2,128);smart.process(b,k,2,2,128);
      if(pos>static_cast<int>(fs*.5))for(int i=0;i<128;++i){left+=bl[i]*bl[i];right+=br[i]*br[i];baseR+=ar[i]*ar[i];}
    }
    check(right>left && right>baseR,"unilateral key preserves opposite channel");
    check(smart.latencySamples()==0,"latency unchanged");
    std::printf("%.0f Hz territory right/left power %.6f, right/parent %.6f\n",fs,right/left,right/baseR);
  }
  std::printf("PASS: 40401 offline steering/redistribution pairs, energy error %.9g, max gain %.6f\n",maxError,static_cast<double>(maxGain));
}
