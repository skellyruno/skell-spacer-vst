#include "ZeroLatencyEngine.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using K=skell::KeyIntelligence;
static void check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static float tone(double fs,int n,double f){return static_cast<float>(std::sin(2.*skell::kPi*f*n/fs));}
int main(){
  check(K::confidence(1.f,1.f,0.f)==1.f,"strong key retained");
  check(K::confidence(.001f,1.f,0.f)<.11f,"quiet tail reduced");
  check(K::confidence(.001f,1.f,1.f)>.7f,"onset rescue");
  float maxOvershoot=0.f;
  for(int d=-100;d<=100;++d)for(int w=-200;w<=200;++w){
    const float dry=static_cast<float>(d)*.01f,wet=static_cast<float>(w)*.01f,cap=1.f;
    const float x=K::safeWet(dry,wet,cap);
    check(x>=0.f&&x<=1.f&&std::isfinite(x),"phase safe blend bounds");
    maxOvershoot=std::max(maxOvershoot,std::abs(dry+(wet-dry)*x)-cap);
  }
  check(maxOvershoot<3e-7f,"peak projection meets ceiling");
  std::printf("Phase projection: 80601 bounded pairs; max floating point overshoot %.9g\n",static_cast<double>(maxOvershoot));
  for(double fs:{44100.,48000.,96000.,192000.}) {
    skell::ZeroLatencyEngine base,clean,linked,detail;
    for(auto* e:{&base,&clean,&linked,&detail})e->prepare(fs);
    skell::SpacerParams p;p.smart=1.f;p.amount=.5f;p.attackMs=5.f;p.releaseMs=40.f;p.smooth=.1f;
    base.setParams(p);linked.setParams(p);p.cleanKey=1.f;clean.setParams(p);p.cleanKey=0.f;p.stereoDetail=1.f;detail.setParams(p);
    double strongA=0.,strongB=0.,tailA=0.,tailB=0.,dryA=0.;
    double linkedTarget=0.,detailTarget=0.,linkedOther=0.,detailOther=0.;
    for(int pos=0;pos<static_cast<int>(fs*1.4);pos+=128) {
      float a[128],b[128],l[128],r[128],x[128],y[128],key[128],kl[128],kr[128];
      for(int i=0;i<128;++i){const int n=pos+i;const float f=tone(fs,n,1000.),g=tone(fs,n,3000.);
        a[i]=b[i]=.2f*f;key[i]=(n<static_cast<int>(fs*.65)?.40f:.018f)*f;
        l[i]=r[i]=x[i]=y[i]=.16f*(f+g);kl[i]=.35f*f;kr[i]=.35f*g;
      }
      float* A[]={a};float* B[]={b};const float* K0[]={key};base.process(A,K0,1,1,128);clean.process(B,K0,1,1,128);
      float* L[]={l,r};float* D[]={x,y};const float* K1[]={kl,kr};linked.process(L,K1,2,2,128);detail.process(D,K1,2,2,128);
      for(int i=0;i<128;++i){const int n=pos+i;
        check(std::isfinite(a[i])&&std::isfinite(b[i])&&std::isfinite(x[i])&&std::isfinite(y[i]),"finite output");
        if(n>static_cast<int>(fs*.4)&&n<static_cast<int>(fs*.6)){strongA+=a[i]*a[i];strongB+=b[i]*b[i];}
        if(n>static_cast<int>(fs*.95)&&n<static_cast<int>(fs*1.25)){tailA+=a[i]*a[i];tailB+=b[i]*b[i];dryA+=.04*std::pow(tone(fs,n,1000.),2);}
        if(n>static_cast<int>(fs*.9)){const float f=tone(fs,n,1000.),g=tone(fs,n,3000.);linkedTarget+=l[i]*f+r[i]*g;detailTarget+=x[i]*f+y[i]*g;linkedOther+=l[i]*g+r[i]*f;detailOther+=x[i]*g+y[i]*f;}
      }
    }
    check(std::abs(strongB/strongA-1.)<.02,"strong key level retained");
    check(tailB>tailA*1.1,"tail key suppression reduces unwanted ducking");
    check(detailOther>linkedOther*1.1,"opposite-frequency spatial retention");
    check(detailTarget<detailOther,"actual spatial conflict remains attenuated");
    std::printf("%.0f Hz strong wet energy ratio %.6f; tail energy normal %.4f%% clean %.4f%%; stereo target ratio %.5f other ratio %.5f\n",fs,strongB/strongA,100.*tailA/dryA,100.*tailB/dryA,detailTarget/linkedTarget,detailOther/linkedOther);
    check(detail.latencySamples()==0,"zero latency");
  }
}
