// Actual production engine: exact parent parity for unchanged modes, malformed key recovery and fast-mode measurements.
#include "ZeroLatencyEngine.h"
#include "PriorZeroLatencyEngine.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <limits>
#include <vector>
constexpr double pi=skell::kPi;
struct Result {std::vector<float> a,b;double us=0;};
template<class E,class P>
static Result render(double fs,P p,double trackHz=1000,bool poison=false,int block=128,bool missing=false) {
  E e;e.prepare(fs);e.setParams(p);
  int n=int(fs),on=int(fs/4),off=int(fs*.75);
  Result out;out.a.resize(n);out.b.resize(n);
  std::vector<float> sl(n),sr(n);
  for(int i=0;i<n;++i){
    double t=double(i)/fs;
    out.a[i]=out.b[i]=float(.2*std::sin(2*pi*trackHz*t));
    float k=(i>=on&&i<off)?float(.4*std::sin(2*pi*1000*t)):0.f;
    sl[i]=sr[i]=k;
  }
  for(int i=on+120;i<on+128;++i){
    sl[i]=poison?1.e30f:0.f;sr[i]=poison?-1.e30f:0.f;
  }
  auto start=std::chrono::steady_clock::now();
  for(int i=0;i<n;i+=block){
    float* x[]={out.a.data()+i,out.b.data()+i};const float* sc[]={sl.data()+i,sr.data()+i};
    e.process(x,sc,2,missing?-1:2,std::min(block,n-i));
  }
  out.us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
  assert(e.latencySamples()==0);
  for(int i=0;i<n;++i)assert(std::isfinite(out.a[i])&&std::isfinite(out.b[i])&&std::abs(out.a[i])<=.20001f&&out.a[i]==out.b[i]);
  return out;
}
static double rmsCut(const Result& x,double fs,double begin,double end,double hz=1000){
  double a=0,b=0;
  for(int i=int(fs*begin);i<int(fs*end);++i){double dry=.2*std::sin(2*pi*hz*i/fs);a+=dry*dry;b+=double(x.a[i])*x.a[i];}
  return -10*std::log10(std::max(b/a,1e-15));
}
int main(){
  using E=skell::ZeroLatencyEngine;using Old=prior_skell::ZeroLatencyEngine;
  skell::SpacerParams p;p.amount=.5f;p.smooth=.3f;p.attackMs=1;p.releaseMs=100;
  prior_skell::SpacerParams q;q.amount=p.amount;q.smooth=p.smooth;q.attackMs=p.attackMs;q.releaseMs=p.releaseMs;
  for(double fs:{44100.,48000.,96000.,192000.}){
    // All old projects including the fastest old attack, and the previously
    // verified level-sensitive curve at normal attack, remain exactly identical.
    p.levelSensitivity=q.levelSensitivity=0;
    assert(render<E>(fs,p).a==render<Old>(fs,q).a);
    p.levelSensitivity=q.levelSensitivity=1;p.attackMs=q.attackMs=5;
    assert(render<E>(fs,p).a==render<Old>(fs,q).a);
    p.attackMs=q.attackMs=1;
    auto before=render<Old>(fs,q),after=render<E>(fs,p);
    auto earlyOld=rmsCut(before,fs,.25,.275),earlyNew=rmsCut(after,fs,.25,.275);
    auto steadyOld=rmsCut(before,fs,.55,.70),steadyNew=rmsCut(after,fs,.55,.70);
    printf("FAST_MODE %.0f Hz first 25ms cut %.6f -> %.6f dB, steady %.6f -> %.6f dB, cpu_us_per_second %.1f -> %.1f\n",fs,earlyOld,earlyNew,steadyOld,steadyNew,before.us,after.us);
    // Guard against worse early suppression or a large steady-state shift.
    assert(earlyNew+0.2>=earlyOld&&std::abs(steadyNew-steadyOld)<1.0);
    assert(rmsCut(render<E>(fs,p,100),fs,.55,.70,100)<1.5);
    assert(after.a==render<E>(fs,p,1000,false,377).a);
    // A corrupt sample must have exactly the same effect as an explicit zero,
    // even with key filters active, without poisoning later audio.
    p.scLowHz=80;p.scHighHz=12000;
    const auto clean=render<E>(fs,p),bad=render<E>(fs,p,1000,true);
    assert(clean.a==bad.a&&clean.b==bad.b);
    assert(rmsCut(bad,fs,.55,.70)>4.);
    p.scLowHz=20;p.scHighHz=20000;
    auto missing=render<E>(fs,p,1000,false,128,true);
    for(int i=0;i<int(missing.a.size());++i)assert(missing.a[i]==float(.2*std::sin(2*pi*1000*(double(i)/fs))));
    p.levelSensitivity=q.levelSensitivity=0;
    printf("RECOVERY %.0f Hz passed, zero delay, exact quiet-key compatibility\n",fs);
  }
}
