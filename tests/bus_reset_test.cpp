#include "ZeroLatencyEngine.h"
#include "prior_smart/ZeroLatencyEngine.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
static void check(bool b,const char* msg){if(!b){std::fprintf(stderr,"FAIL: %s\n",msg);std::exit(1);}}
template<class Engine,class Params> static void prime(Engine& e,Params p,double fs){
 e.prepare(fs); e.setParams(p);
 for(int pos=0;pos<static_cast<int>(fs*.6);pos+=128){
  float l[128],r[128],kl[128],kr[128];
  for(int i=0;i<128;++i){const double t=(pos+i)/fs;l[i]=r[i]=.2f*static_cast<float>(std::sin(2.*skell::kPi*1000.*t));kl[i]=0.f;kr[i]=.4f*static_cast<float>(std::sin(2.*skell::kPi*1000.*t));}
  float* M[]={l,r}; const float* K[]={kl,kr}; e.process(M,K,2,2,128);
 }
}
template<class E> static float switchBus(E& e,bool key){
 // One mono sample, then reconnect a silent right channel before another
 // control tick. The retired right-hand IIR state must not leak into audio.
 float l=0.f,r=0.f,kl=0.f,kr=0.f,monitorL=0.f,monitorR=0.f;
 float* M[]={&l,&r}; const float* K[]={&kl,&kr}; float* O[]={&monitorL,&monitorR};
 e.process(M,K,key?2:1,key?1:2,1,O);
 l=r=monitorL=monitorR=0.f;
 e.process(M,K,2,2,1,O);
 return key?std::abs(monitorL):std::abs(r);
}
int main(){
 for(double fs:{44100.,48000.,96000.,192000.}){
  skell::ZeroLatencyEngine next;prior_skell::ZeroLatencyEngine old;
  skell::SpacerParams p;prior_skell::SpacerParams q;
  p.smart=q.smart=1.f;p.amount=q.amount=.8f;p.stereoDetail=q.stereoDetail=1.f;
  p.scLowHz=q.scLowHz=500.f;p.scHighHz=q.scHighHz=2000.f;
  prime(next,p,fs);prime(old,q,fs);
  const float oldKey=switchBus(old,true),newKey=switchBus(next,true);
  check(newKey==0.f,"silent reconnected key cannot replay prior filter state");
  prime(next,p,fs);prime(old,q,fs);
  const float oldMain=switchBus(old,false),newMain=switchBus(next,false);
  check(newMain==0.f,"silent reconnected main cannot replay retired bell state");
  // Reset while running an M/S stream, then compare to an independently
  // prepared engine with identical new LR parameters and source frames.
  p.ms=true;prime(next,p,fs);next.reset();p.ms=false;next.setParams(p);
  skell::ZeroLatencyEngine fresh;fresh.prepare(fs);fresh.setParams(p);
  float error=0.f;
  for(int pos=0;pos<static_cast<int>(fs*.1);pos+=128){
   float a[128],b[128],c[128],d[128],k[128];
   for(int i=0;i<128;++i){const float v=.2f*static_cast<float>(std::sin(2.*skell::kPi*1000.*(pos+i)/fs));a[i]=b[i]=c[i]=d[i]=k[i]=v;}
   float* A[]={a,b};float* B[]={c,d};const float* K[]={k,k};next.process(A,K,2,2,128);fresh.process(B,K,2,2,128);
   for(int i=0;i<128;++i)error=std::max(error,std::max(std::abs(a[i]-c[i]),std::abs(b[i]-d[i])));
  }
  check(error==0.f,"reset matches fresh stream exactly");
  std::printf("%.0f Hz reconnect key prior %.9g new %.9g; main prior %.9g new %.9g; reset error %.9g\n",fs,static_cast<double>(oldKey),static_cast<double>(newKey),static_cast<double>(oldMain),static_cast<double>(newMain),static_cast<double>(error));
 }
}
