// Compare the actual V17 engine with the exact pre-change header and exercise the opt-in detector.
#include "ZeroLatencyEngine.h"
#include "PriorZeroLatencyEngine.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>
constexpr double pi=3.14159265358979323846;
template<class Engine,class Params>
std::vector<float> render(double fs,float level,double hz,double keyHz,Params p,int block=128,bool anti=false){
  Engine e;e.prepare(fs);e.setParams(p);
  const int n=int(2*fs);std::vector<float> a(n),b(n),s(n),t(n);
  for(int i=0;i<n;++i){a[i]=b[i]=float(.2*std::sin(2*pi*hz*i/fs));s[i]=float(level*std::sin(2*pi*keyHz*i/fs));t[i]=anti?-s[i]:s[i];}
  for(int i=0;i<n;i+=block){float* x[]={a.data()+i,b.data()+i};const float* k[]={s.data()+i,t.data()+i};e.process(x,k,2,2,std::min(block,n-i));}
  assert(e.latencySamples()==0);
  for(int i=0;i<n;++i){assert(std::isfinite(a[i])&&std::isfinite(b[i]));assert(a[i]==b[i]);}
  return a;
}
double cut(const std::vector<float>& a,double fs,double hz){
  double re=0,im=0;const int n=int(a.size());
  for(int i=n/2;i<n;++i){re+=a[i]*std::cos(2*pi*hz*i/fs);im+=a[i]*std::sin(2*pi*hz*i/fs);}
  return -20*std::log10(std::max(2*std::hypot(re,im)/(n-n/2)/.2,1e-12));
}
int main(){
  using E=skell::ZeroLatencyEngine;skell::SpacerParams p;p.amount=.5f;p.smooth=.3f;p.attackMs=5;p.releaseMs=100;
  prior_skell::SpacerParams old;old.amount=p.amount;old.smooth=p.smooth;old.attackMs=p.attackMs;old.releaseMs=p.releaseMs;
  for(double fs:{44100.,48000.,96000.,192000.}){
    p.levelSensitivity=0;
    auto a=render<E>(fs,.4f,1000,1000,p);auto b=render<prior_skell::ZeroLatencyEngine>(fs,.4f,1000,1000,old);
    assert(a==b);
    p.levelSensitivity=1;
    double last=-1;
    for(float db:{-40.f,-30.f,-24.f,-20.f,-12.f,-6.f}){
      auto x=render<E>(fs,std::pow(10.f,db/20.f),1000,1000,p);double v=cut(x,fs,1000);
      printf("%.0f Hz detector %.0f dBFS -> %.5f dB cut\n",fs,db,v);
      assert(v>=last-.05);last=v;
      if(db==-40)assert(v<2.);
    }
    assert(last>5.);
    auto strong=render<E>(fs,.4f,1000,1000,p);
    assert(cut(strong,fs,1000)>5.);
    assert(cut(render<E>(fs,.4f,100,1000,p),fs,100)<1.5);
    assert(cut(render<E>(fs,.4f,8000,1000,p),fs,8000)<1.5);
    assert(strong==render<E>(fs,.4f,1000,1000,p,377));
    assert(cut(render<E>(fs,.4f,1000,1000,p,128,true),fs,1000)>4.);
    p.amount=0;auto bypass=render<E>(fs,.4f,1000,1000,p);
    for(int i=0;i<int(bypass.size());++i)assert(bypass[i]==float(.2*std::sin(2*pi*1000*i/fs)));
    p.amount=.5f;
  }
  // Mid-stream host changes and freeze must be finite and bounded.
  E e;e.prepare(48000);p.levelSensitivity=0;e.setParams(p);
  float a[128],b[128],k[128];float* m[]={a,b};const float* sc[]={k,k};
  double prev=0,jump=0;
  for(int block=0;block<750;++block){
    if(block==250||block==500){p.levelSensitivity=block==250?1.f:0.f;e.setParams(p);}
    if(block==350||block==450){p.freeze=block==350;e.setParams(p);}
    for(int i=0;i<128;++i){int n=block*128+i;a[i]=b[i]=float(.2*std::sin(2*pi*1000*n/48000));k[i]=float(.03*std::sin(2*pi*1000*n/48000));}
    e.process(m,sc,2,2,128);
    for(int i=0;i<128;++i){assert(std::isfinite(a[i])&&std::abs(a[i])<=.20001f&&a[i]==b[i]);jump=std::max(jump,std::abs(a[i]-prev));prev=a[i];}
  }
  assert(jump<.06);
  printf("AUTOMATION max adjacent sample change %.9g\n",jump);
}
