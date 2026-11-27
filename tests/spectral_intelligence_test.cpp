#include "SpectralIntelligence.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
using S=skell::SpectralIntelligence;
static void check(bool b,const char* s){if(!b){std::fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
int main(){
  check(S::narrow(1.f,1.f)==1.f,"on-band preserved");
  check(S::narrow(.01f,1.f)<.01f,"spill reduced");
  check(S::vocal(1000.f,1.f)==1.f,"presence preserved");
  check(S::vocal(10000.f,1.f)<.4f,"high dominant key moderated");
  check(S::perceptual(1000.f)>S::perceptual(40.f),"presence weighted");
  check(S::harmonic(8.f,1.f,0.f)<.31f,"unmasked harmonic protected");
  check(S::harmonic(8.f,1.f,1.f)==1.f,"actual conflict remains eligible");
  check(S::budget(.01f,1.f)<.26f,"dominant key needs less cut");
  check(S::budget(1.f,.01f)==1.f,"strong main remains eligible");
  int count=0;
  for(int a=0;a<=20;++a) for(int b=0;b<=20;++b) for(int c=0;c<=20;++c){
    const float x=static_cast<float>(a)/20.f,y=static_cast<float>(b)/20.f,z=static_cast<float>(c)/20.f;
    const float g=S::blend(z,S::narrow(x,y))*S::blend(z,S::harmonic(x,y,z))*S::blend(z,S::budget(x,y))*S::vocal(20.f+x*20000.f,y)*S::perceptual(20.f+y*20000.f);
    check(std::isfinite(g)&&g>=0.f&&g<=1.f,"bounded combined factor"); ++count;
  }
  check(S::blend(0.f,.1f)==1.f,"disabled path unity");
  std::printf("PASS: %d bounded spectral heuristic combinations\n",count);
}
