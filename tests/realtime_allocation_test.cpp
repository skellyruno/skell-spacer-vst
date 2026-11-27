// V16 JUCE-free allocation/zero-latency safety regression.
// Construction and prepare() may allocate; setParams()/process() must not.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "ZeroLatencyEngine.h"
static std::atomic<bool> trackAlloc{false};
static std::atomic<unsigned long> newCalls{0};
void* operator new(std::size_t n) {
  if (trackAlloc.load(std::memory_order_relaxed)) newCalls.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n)) { return p; }
  throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
  if (trackAlloc.load(std::memory_order_relaxed)) newCalls.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n)) { return p; }
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
int main() {
  bool pass=true;
  for(double fs : {44100.,48000.,96000.,192000.}) {
    skell::ZeroLatencyEngine e; e.prepare(fs);
    skell::SpacerParams p; p.amount=.9f; p.smooth=.5f; p.power=true;
    float l[257]={},r[257]={},sl[257]={},sr[257]={};
    float* track[2]={l,r}; const float* sc[2]={sl,sr};
    long long samples=0;
    trackAlloc=true;
    for(int b=0;b<1800;++b) {
      const int n=(b%5==0?1:(b%3==0?37:257));
      for(int i=0;i<n;++i) {
        const float t=(float)(b*n+i);
        l[i]=r[i]=0.18f*std::sin(t*.1f);
        sl[i]=sr[i]=0.33f*std::sin(t*.14f);
      }
      if(b%7==0) {
        p.amount=(float)(b%100)/100.f;
        p.levelSensitivity=(float)(b%21)/20.f;
        p.smart=(float)(b%17)/16.f;
        p.punchProtect=.8f; p.bassProtect=.7f; p.adaptiveRelease=.6f;
        p.spectralFocus=.8f; p.vocalFocus=.7f; p.perceptualFocus=.6f;
        p.harmonicProtect=.9f; p.depthBudget=.5f;
        p.sonicTerritory=.7f; p.humanEar=.6f; p.componentProtect=.5f; p.foresight=.8f;
        p.refinedPerception=b%2==0;
        p.cleanKey=.8f; p.phaseProtect=.7f; p.stereoDetail=static_cast<float>(b%4)/3.f;
        p.tempoRecovery=.8f; p.hostBpm=b%4==0?0.f:90.f+static_cast<float>(b%80);
        p.protect[static_cast<size_t>(b%skell::ProtectionCurve::count)]=.85f;
        p.deltaListen=b%3==0;
        p.attackMs=1.f+(float)(b%5);
        p.pan=(float)(b%19)/9.f-1.f;
        p.scLowHz=20.f+(float)(b%12)*30.f;
        e.setParams(p);
      }
      e.process(track,sc,2,2,n);
      for(int i=0;i<n;++i) if(!std::isfinite(l[i])||!std::isfinite(r[i]))pass=false;
      samples+=n;
    }
    trackAlloc=false;
    const auto count=newCalls.exchange(0);
    const bool ok=count==0 && e.latencySamples()==0 && pass;
    std::printf("[%s] %.0f Hz / %lld samples / %lu new calls / delay %d samples\n",
                ok?"PASS":"FAIL",fs,samples,count,e.latencySamples());
    if(!ok) pass=false;
  }
  return pass?0:1;
}
