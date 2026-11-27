// Deterministic stimulus, real DSP, measured callback duration. This is an
// environmental benchmark: never gate CI on timing thresholds from shared hosts.
#include "ZeroLatencyEngine.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
static double percentile(const std::vector<double>& sorted, double q) {
  if (sorted.empty()) return 0;
  const auto idx = static_cast<size_t>(q * static_cast<double>(sorted.size() - 1));
  return sorted[idx];
}
int main() {
  bool allGood=true;
  std::puts("sample_rate_hz,block_frames,ui_mode,p50_us,p99_us,max_us,deadline_us,deadline_misses_percent,ui_reads,output_finite");
  for (double fs : {44100.,48000.,96000.,192000.}) {
    for (int n : {64,128,256,512}) {
      for (int uiMode=0;uiMode<2;++uiMode) {
        skell::ZeroLatencyEngine e; e.prepare(fs);
        skell::SpacerParams p;
        p.amount=.8f; p.attackMs=5.f; p.releaseMs=140.f; p.smooth=.3f;
        e.setParams(p);
        std::atomic<bool> running{true};
        std::atomic<int> reads{0};
        std::thread ui;
        if (uiMode) ui=std::thread([&] {
          float a[skell::ZeroLatencyEngine::kDisp],b[skell::ZeroLatencyEngine::kDisp];
          float c[skell::ZeroLatencyEngine::kDisp];
          do {
            e.getDisplay(a,b,c);
            ++reads;
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
          }while(running.load(std::memory_order_acquire));
        });
        std::vector<float> l(n),r(n),s0(n),s1(n);
        float* in[2]={l.data(),r.data()};
        const float* key[2]={s0.data(),s1.data()};
        std::vector<double> times;
        const int blocks=std::max(250,static_cast<int>(2.*fs/n));
        times.reserve(static_cast<size_t>(blocks));
        bool finite=true;int over=0;
        const double deadline=1.e6*static_cast<double>(n)/fs;
        for(int k=0;k<blocks;++k) {
          for(int i=0;i<n;++i) {
            const double t=(static_cast<double>(k)*n+i)/fs;
            const float x=.22f*static_cast<float>(std::sin(2.*skell::kPi*110.*t))
                         +.06f*static_cast<float>(std::sin(2.*skell::kPi*880.*t));
            const float beat=(std::fmod(t,.55)<.28 ? .4f:0.f)
                             *static_cast<float>(std::sin(2.*skell::kPi*350.*t));
            l[i]=x;r[i]=.97f*x;s0[i]=beat;s1[i]=beat;
          }
          const auto start=Clock::now();
          e.process(in,key,2,2,n);
          const auto stop=Clock::now();
          const double us=std::chrono::duration<double,std::micro>(stop-start).count();
          if(k>30) {times.push_back(us);if(us>deadline)++over;}
          for(int i=0;i<n;++i) if(!std::isfinite(l[i])||!std::isfinite(r[i]))finite=false;
        }
        running.store(false,std::memory_order_release);
        if(ui.joinable())ui.join();
        std::sort(times.begin(),times.end());
        const double miss=100.*over/static_cast<double>(times.size());
        std::printf("%.0f,%d,%s,%.2f,%.2f,%.2f,%.2f,%.4f,%d,%s\n",
                    fs,n,uiMode?"open":"closed",percentile(times,.5),percentile(times,.99),
                    times.back(),deadline,miss,reads.load(),finite?"yes":"no");
        if(!finite||e.latencySamples()!=0)allGood=false;
      }
    }
  }
  return allGood?0:1;
}
