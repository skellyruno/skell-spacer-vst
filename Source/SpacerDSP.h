#pragma once
// Shared JUCE-free pieces for the Skell Spacer engine: the parameter block and the FFT used by the spectrum display.
#include <vector>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstring>
#include "ProtectionCurve.h"

namespace skell {

constexpr double kPi = 3.14159265358979323846;

struct SpacerParams {
  float amount = 0.5f;       // 0..1  (share of 24 dB)
  float smooth = 0.3f;       // 0..1  (0..1 octave spread)
  float attackMs = 10.f;
  float releaseMs = 150.f;
  float lowHz = 20.f;
  float highHz = 20000.f;
  float scLowHz = 20.f;      // sidechain high-pass (20 Hz = off)
  float scHighHz = 20000.f;  // sidechain low-pass (20 kHz = off)
  float pan = 0.f;           // -1..1: L/R mode: left..right; M/S mode: mid..side. 0 = both
  bool ms = false;           // false = L/R, true = M/S
  bool freeze = false;
  bool power = true;
  float smart = 0.f, punchProtect = 0.f, bassProtect = 0.f, adaptiveRelease = 0.f;
  float spectralFocus = 0.f, vocalFocus = 0.f, perceptualFocus = 0.f, harmonicProtect = 0.f, depthBudget = 0.f;
  float sonicTerritory = 0.f, humanEar = 0.f, componentProtect = 0.f, foresight = 0.f;
  float cleanKey=0.f, phaseProtect=0.f, stereoDetail=0.f;
  float tempoRecovery=0.f, hostBpm=0.f;
  ProtectionCurve::Nodes protect{};
  bool deltaListen = false;
  bool refinedPerception = false; // preserve the original DSP library contract
  float levelSensitivity = 0.f; // 0..1: legacy relative spectrum -> absolute-level-sensitive depth
};

class FFT {
public:
  void init(int n) {
    N = n;
    rev.assign((size_t)n, 0);
    cosT.assign((size_t)n / 2, 0.f);
    sinT.assign((size_t)n / 2, 0.f);
    int bits = 0;
    while ((1 << bits) < n) ++bits;
    for (int i = 0; i < n; ++i) {
      int r = 0;
      for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
      rev[(size_t)i] = r;
    }
    for (int i = 0; i < n / 2; ++i) {
      double a = -2.0 * kPi * i / n;
      cosT[(size_t)i] = (float)std::cos(a);
      sinT[(size_t)i] = (float)std::sin(a);
    }
  }
  void forward(float* re, float* im) const {
    for (int i = 0; i < N; ++i) {
      int j = rev[(size_t)i];
      if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= N; len <<= 1) {
      const int half = len >> 1, step = N / len;
      for (int i = 0; i < N; i += len)
        for (int k = 0; k < half; ++k) {
          const float wr = cosT[(size_t)(k * step)], wi = sinT[(size_t)(k * step)];
          const int a = i + k, b = a + half;
          const float tr = re[b] * wr - im[b] * wi;
          const float ti = re[b] * wi + im[b] * wr;
          re[b] = re[a] - tr; im[b] = im[a] - ti;
          re[a] += tr;        im[a] += ti;
        }
    }
  }
private:
  int N = 0;
  std::vector<int> rev;
  std::vector<float> cosT, sinT;
};


} // namespace skell
