// Concurrent, lock-free audio/GUI display snapshot regression.
#include "ZeroLatencyEngine.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
int main() {
  bool ok = true;
  for (double rate : {44100.0,48000.0,96000.0,192000.0}) {
    skell::ZeroLatencyEngine e;
    e.prepare(rate);
    skell::SpacerParams p; p.amount=1.f; p.attackMs=4.f; p.releaseMs=100.f;
    e.setParams(p);
    std::atomic<bool> running{true};
    std::atomic<int> frames{0}, bad{0};
    std::thread ui([&] {
      float a[skell::ZeroLatencyEngine::kDisp],b[skell::ZeroLatencyEngine::kDisp];
      float c[skell::ZeroLatencyEngine::kDisp];
      while(running.load(std::memory_order_acquire)){
        e.getDisplay(a,b,c);
        for(int i=0;i<skell::ZeroLatencyEngine::kDisp;++i){
          if(!std::isfinite(a[i])||!std::isfinite(b[i])||!std::isfinite(c[i])||c[i]<-1e-5f||c[i]>60.f){
            bad.fetch_add(1,std::memory_order_relaxed);break;
          }
        }
        frames.fetch_add(1,std::memory_order_relaxed);
        std::this_thread::yield();
      }
    });
    constexpr int N=64;float l[N],r[N],sl[N],sr[N];
    float* main[2]={l,r};const float* sidechain[2]={sl,sr};
    const int blocks=static_cast<int>(rate/N)*2;
    for(int k=0;k<blocks;++k){
      for(int i=0;i<N;++i){
        const double t=(k*N+i)/rate;
        l[i]=r[i]=.2f*static_cast<float>(std::sin(2*skell::kPi*1000*t));
        sl[i]=sr[i]=.4f*static_cast<float>(std::sin(2*skell::kPi*1000*t));
      }
      e.process(main,sidechain,2,2,N);
      for(int i=0;i<N;++i) if(!std::isfinite(l[i])||!std::isfinite(r[i])) ++bad;
    }
    running.store(false,std::memory_order_release);
    ui.join();
    float a[skell::ZeroLatencyEngine::kDisp],b[skell::ZeroLatencyEngine::kDisp];
    float c[skell::ZeroLatencyEngine::kDisp]; e.getDisplay(a,b,c);
    float peak=0.f;for(float x:c)peak=std::max(peak,x);
    const bool passed=frames>0&&bad==0&&peak>5.f&&e.latencySamples()==0;
    ok=ok&&passed;
    std::printf("[%s] %.0f Hz, %d UI reads, max cut %.1f dB, errors %d, latency %d samples\n",
                passed?"PASS":"FAIL",rate,frames.load(),peak,bad.load(),e.latencySamples());
  }
  return ok?0:1;
}
