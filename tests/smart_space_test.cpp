#include "ZeroLatencyEngine.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cmath>
#include <algorithm>

static void require(bool ok, const char* msg) {
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", msg); std::exit(1); }
}
static float wave(int i, double fs, double hz) {
  return static_cast<float>(std::sin(2.0 * skell::kPi * hz * i / fs));
}
int main() {
  for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
    constexpr int block = 128;
    skell::ZeroLatencyEngine wet, delta, classic, smart;
    for (auto* e : {&wet, &delta, &classic, &smart}) e->prepare(fs);
    skell::SpacerParams p; p.amount = .6f; p.smooth = .15f;
    wet.setParams(p); classic.setParams(p);
    p.deltaListen = true; delta.setParams(p); p.deltaListen = false;
    p.smart = 1.f; p.punchProtect = .6f; p.bassProtect = .6f; p.adaptiveRelease = .8f;
    smart.setParams(p);
    float err = 0.f;
    double dryE = 0., wetE = 0., smartE = 0.;
    for (int pos = 0; pos < static_cast<int>(fs * 1.25); pos += block) {
      float x[block], w[block], d[block], a[block], b[block], key[block];
      for (int i = 0; i < block; ++i) {
        const int n = pos+i;
        x[i] = .25f*wave(n, fs, 82.0) + .20f*wave(n, fs, 1000.0);
        key[i] = .25f*wave(n, fs, 1000.0) + .10f*wave(n, fs, 6500.0);
        w[i] = d[i] = a[i] = b[i] = x[i];
      }
      const float* k[] = {key};
      float* wp[] = {w}; float* dp[] = {d}; float* ap[] = {a}; float* bp[] = {b};
      wet.process(wp,k,1,1,block); delta.process(dp,k,1,1,block);
      classic.process(ap,k,1,1,block); smart.process(bp,k,1,1,block);
      for (int i = 0; i < block; ++i) {
        require(std::isfinite(b[i]), "Smart output finite");
        require(w[i] == a[i], "Classic monitor-off parity");
        if (pos > static_cast<int>(fs*.25)) {
          err = std::max(err, std::fabs((w[i]+d[i])-x[i]));
          dryE += x[i]*x[i]; wetE += w[i]*w[i]; smartE += b[i]*b[i];
        }
      }
    }
    auto snap = smart.getSmartSnapshot();
    require(err < 2e-6f, "wet + delta reconstructs dry");
    require(snap.inputPower > 0.f && std::isfinite(snap.outputPower), "live diagnostic power");
    float overlap = 0.f;
    for (int i = 0; i < skell::ZeroLatencyEngine::kBands; ++i) {
      require(snap.overlap[i] >= 0.f && snap.overlap[i] <= 1.f, "overlap bounds");
      require(snap.protection[i] >= 0.f && snap.protection[i] <= 1.f, "protection bounds");
      overlap = std::max(overlap, snap.overlap[i]);
    }
    require(overlap > .1f, "overlap detected");
    require(smart.latencySamples() == 0 && delta.latencySamples() == 0, "no added latency");
    std::printf("%.0f Hz delta error %.9g, wet/dry %.6f, smart/dry %.6f, max overlap %.6f\n",fs,static_cast<double>(err),wetE/dryE,smartE/dryE,static_cast<double>(overlap));
  }
  std::puts("PASS: Smart Space diagnostics and Delta listening");
}
