#pragma once
// JUCE-free spectral ducking engine for Skell Spacer.
// Track is cut, per FFT bin, by how loud the sidechain is in that bin.
#include <vector>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <cstring>

namespace skell {

constexpr double kPi = 3.14159265358979323846;

struct SpacerParams {
  float amount = 0.5f;       // 0..1  (share of 24 dB)
  float smooth = 0.3f;       // 0..1  (0..1 octave spread)
  float link = 0.0f;         // 0..1  stereo link: blend each side's cut toward the deeper of the two
  float attackMs = 10.f;
  float releaseMs = 150.f;
  float lowHz = 20.f;
  float highHz = 20000.f;
  bool freeze = false;
  bool power = true;
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

class SpacerEngine {
public:
  static constexpr int kDisp = 256;
  static constexpr int kMaxCh = 2;
  static constexpr float kMaxCutDb = 24.f;

  SpacerEngine() { prepare(48000.0); }

  // FFT size grows with the sample rate so bin width (and the ~43 ms latency) stay constant.
  static int sizeFor(double sampleRate) { return sampleRate <= 50000.0 ? 2048 : (sampleRate <= 100000.0 ? 4096 : 8192); }
  int latencySamples() const { return N; }

  void prepare(double sampleRate) {
    fs = sampleRate > 1000.0 ? sampleRate : 44100.0;
    if (sizeFor(fs) != N || win.empty()) allocate(sizeFor(fs));
    mixStep = (float)(1.0 / (0.010 * fs));
    buildDisplayMap();
    bandDirty = true;
    reset();
  }

  void allocate(int n) {
    N = n; HOP = n / 4; K = n / 2 + 1;
    fft.init(N);
    win.resize((size_t)N);
    for (int i = 0; i < N; ++i) win[(size_t)i] = std::sqrt(0.5f - 0.5f * (float)std::cos(2.0 * kPi * i / N));
    for (auto& c : ch) {
      c.ring.assign((size_t)N, 0.f); c.scRing.assign((size_t)N, 0.f);
      c.outReady.assign((size_t)HOP, 0.f); c.acc.assign((size_t)N, 0.f);
      c.env.assign((size_t)K, 0.f); c.eff.assign((size_t)K, 0.f);
      c.xr.assign((size_t)K, 0.f); c.xi.assign((size_t)K, 0.f); c.ps.assign((size_t)K, 0.f);
    }
    re.assign((size_t)N, 0.f); im.assign((size_t)N, 0.f);
    P.assign((size_t)K, 0.f); cum.assign((size_t)K + 1, 0.f); bw.assign((size_t)K, 1.f);
  }

  void reset() {
    for (auto& c : ch) {
      std::fill(c.ring.begin(), c.ring.end(), 0.f);
      std::fill(c.scRing.begin(), c.scRing.end(), 0.f);
      std::fill(c.outReady.begin(), c.outReady.end(), 0.f);
      std::fill(c.acc.begin(), c.acc.end(), 0.f);
      std::fill(c.env.begin(), c.env.end(), 0.f);
      c.peak = 0.f;
    }
    wpos = 0; hopCount = 0; mix = 0.f;
    for (int i = 0; i < kDisp; ++i) { dispMain[i] = -120.f; dispSc[i] = -120.f; dispCut[i] = 0.f; }
  }

  void setParams(SpacerParams p) {
    p.amount = clamp01(p.amount);
    p.smooth = clamp01(p.smooth);
    p.link = clamp01(p.link);
    p.lowHz = std::min(std::max(p.lowHz, 20.f), 20000.f);
    p.highHz = std::min(std::max(p.highHz, 20.f), 20000.f);
    if (p.highHz < p.lowHz * 1.25f) {
      p.highHz = p.lowHz * 1.25f;
      if (p.highHz > 20000.f) { p.highHz = 20000.f; p.lowHz = 16000.f; }
    }
    if (p.lowHz != params.lowHz || p.highHz != params.highHz) bandDirty = true;
    params = p;
  }

  // In-place on `main`. `sc` may contain nullptrs / nSc == 0.
  void process(float* const* main, const float* const* sc, int nCh, int nSc, int nSamples) {
    nCh = std::min(nCh, kMaxCh);
    hasSc = nSc > 0;
    const bool wet = params.power && params.amount > 1e-4f && (hasSc || params.freeze);
    const float target = wet ? 1.f : 0.f;
    for (int i = 0; i < nSamples; ++i) {
      if (mix < target) mix = std::min(target, mix + mixStep);
      else if (mix > target) mix = std::max(target, mix - mixStep);

      for (int c = 0; c < nCh; ++c) {
        auto& s = ch[c];
        const float x = main[c][i];
        float scv = 0.f;
        if (nSc > 0) {
          if (nCh == 1 && nSc >= 2) scv = 0.5f * (sc[0][i] + sc[1][i]);
          else scv = sc[c < nSc ? c : 0][i];
        }
        const float dry = s.ring[(size_t)wpos];
        const float w = s.outReady[(size_t)hopCount];
        s.ring[(size_t)wpos] = x;
        s.scRing[(size_t)wpos] = scv;
        main[c][i] = mix <= 0.f ? dry : (mix >= 1.f ? w : dry + (w - dry) * mix);
      }
      wpos = (wpos + 1) & (N - 1);
      if (++hopCount == HOP) { hopCount = 0; runFrame(nCh); }
    }
  }

  void getDisplay(float* mainDb, float* scDb, float* cutDb) const {
    for (int i = 0; i < kDisp; ++i) {
      mainDb[i] = dispMain[i].load(std::memory_order_relaxed);
      scDb[i] = dispSc[i].load(std::memory_order_relaxed);
      cutDb[i] = dispCut[i].load(std::memory_order_relaxed);
    }
  }

private:
  struct Chan {
    std::vector<float> ring, scRing, outReady, acc, env, eff, xr, xi, ps;
    float peak = 0.f;
  };

  static float clamp01(float v) { return std::min(1.f, std::max(0.f, v)); }
  static float smoothstep(float t) { t = clamp01(t); return t * t * (3.f - 2.f * t); }

  void buildBand() {
    const float binHz = (float)(fs / N);
    for (int k = 0; k < K; ++k) {
      const float f = (float) k * binHz;
      float wl = 1.f, wh = 1.f;
      if (params.lowHz > 20.01f)
        wl = f <= 0.f ? 0.f : smoothstep((std::log2(f / params.lowHz) + 1.f / 6.f) * 3.f);
      if (params.highHz < 19999.f)
        wh = f <= 0.f ? 1.f : 1.f - smoothstep((std::log2(f / params.highHz) + 1.f / 6.f) * 3.f);
      bw[(size_t)k] = wl * wh;
    }
    bandDirty = false;
  }

  void buildDisplayMap() {
    const double binHz = fs / N;
    for (int i = 0; i < kDisp; ++i) {
      auto freq = [](double t) { return 20.0 * std::pow(1000.0, t); };
      const double t = (double)i / (kDisp - 1), dt = 1.0 / (kDisp - 1);
      const double f0 = freq(t - 0.5 * dt), f1 = freq(t + 0.5 * dt);
      int lo = (int)std::floor(f0 / binHz), hi = (int)std::ceil(f1 / binHz);
      lo = std::min(std::max(lo, 1), K - 1);
      hi = std::min(std::max(hi, lo), K - 1);
      dispLo[i] = lo; dispHi[i] = hi;
    }
  }

  void runFrame(int nCh) {
    if (bandDirty) buildBand();
    const float hopSec = (float)(HOP / fs);
    const float aCoef = std::exp(-hopSec / std::max(0.001f, params.attackMs * 0.001f));
    const float rCoef = std::exp(-hopSec / std::max(0.005f, params.releaseMs * 0.001f));
    const float peakDecay = std::exp(-hopSec / 2.0f);
    const float norm = 1.f / (0.3183f * (float) N);       // sine amplitude A -> |X|*norm = A
    const float depth = params.amount * kMaxCutDb;
    const float octHalf = std::exp2(params.smooth * 0.5f);

    // pass 1: analyse each channel and follow its envelope
    for (int c = 0; c < nCh; ++c) {
      auto& s = ch[c];
      for (int n = 0; n < N; ++n) {
        const int idx = (wpos + n) & (N - 1);
        re[(size_t)n] = s.ring[(size_t)idx] * win[(size_t)n];
        im[(size_t)n] = s.scRing[(size_t)idx] * win[(size_t)n];
      }
      fft.forward(re.data(), im.data());

      // split packed real FFT: Z = X + iS
      for (int k = 0; k < K; ++k) {
        const int j = (N - k) & (N - 1);
        s.xr[(size_t)k] = 0.5f * (re[(size_t)k] + re[(size_t)j]);
        s.xi[(size_t)k] = 0.5f * (im[(size_t)k] - im[(size_t)j]);
        const float sr = 0.5f * (im[(size_t)k] + im[(size_t)j]);
        const float si = -0.5f * (re[(size_t)k] - re[(size_t)j]);
        P[(size_t)k] = (sr * sr + si * si) * norm * norm;
      }

      // spread sidechain over frequency (log-width average)
      cum[0] = 0.f;
      for (int k = 0; k < K; ++k) cum[(size_t)k + 1] = cum[(size_t)k] + P[(size_t)k];
      if (params.smooth < 0.01f) {
        s.ps = P;
      } else {
        for (int k = 0; k < K; ++k) {
          const int lo = (int)((float)k / octHalf);
          const int hi = std::min(K - 1, (int)((float)k * octHalf + 0.5f));
          s.ps[(size_t)k] = (cum[(size_t)hi + 1] - cum[(size_t)lo]) / (float)(hi - lo + 1);
        }
      }

      float curPeak = 0.f;
      for (int k = 1; k < K; ++k) curPeak = std::max(curPeak, s.ps[(size_t)k]);
      s.peak = std::max(curPeak, s.peak * peakDecay);
      const float peakDb = 10.f * std::log10(std::max(s.peak, 1e-20f));

      for (int k = 0; k < K; ++k) {
        const float db = 10.f * std::log10(std::max(s.ps[(size_t)k], 1e-20f));
        // no cut 50 dB under own recent peak, full cut at 10 dB under; ignore very quiet bins
        const float rel = clamp01((db - peakDb + 50.f) / 40.f);
        const float gate = clamp01((db + 85.f) / 10.f);
        const float t = (k == 0) ? 0.f : rel * gate;
        if (!params.freeze) {
          float& e = s.env[(size_t)k];
          const float cf = t > e ? aCoef : rCoef;
          e = t + (e - t) * cf;
        }
      }
    }

    // stereo link: pull each side's cut toward the deeper of the two (applied cut only)
    for (int c = 0; c < nCh; ++c) ch[c].eff = ch[c].env;
    if (nCh == 2 && params.link > 0.f) {
      for (int k = 0; k < K; ++k) {
        const float a = ch[0].env[(size_t)k], b = ch[1].env[(size_t)k], m = std::max(a, b);
        ch[0].eff[(size_t)k] = a + (m - a) * params.link;
        ch[1].eff[(size_t)k] = b + (m - b) * params.link;
      }
    }

    // pass 2: apply gains, build Hermitian spectrum, inverse FFT, overlap-add
    for (int c = 0; c < nCh; ++c) {
      auto& s = ch[c];
      for (int k = 0; k < K; ++k) {
        const float cutDb = depth * s.eff[(size_t)k] * bw[(size_t)k];
        const float g = std::exp(-cutDb * 0.11512925f);
        re[(size_t)k] = s.xr[(size_t)k] * g;
        im[(size_t)k] = -s.xi[(size_t)k] * g;  // conj for inverse-by-forward
        if (k > 0 && k < N / 2) {
          re[(size_t)(N - k)] = re[(size_t)k];
          im[(size_t)(N - k)] = -im[(size_t)k];
        }
      }
      im[0] = 0.f; im[N / 2] = 0.f;
      fft.forward(re.data(), im.data());
      const float scale = 0.5f / (float)N;   // sqrt-Hann^2 at 75% overlap sums to 2
      for (int n = 0; n < N; ++n) s.acc[(size_t)n] += re[(size_t)n] * win[(size_t)n] * scale;

      if (c == 0) publishDisplay(norm, depth);

      std::copy(s.acc.begin(), s.acc.begin() + HOP, s.outReady.begin());
      std::memmove(s.acc.data(), s.acc.data() + HOP, sizeof(float) * (N - HOP));
      std::fill(s.acc.begin() + (N - HOP), s.acc.end(), 0.f);
    }
  }

  void publishDisplay(float norm, float depth) {
    const auto& s = ch[0];
    for (int i = 0; i < kDisp; ++i) {
      float m = 0.f, sc = 0.f, cut = 0.f;
      for (int k = dispLo[i]; k <= dispHi[i]; ++k) {
        const float mag = std::sqrt(s.xr[(size_t)k] * s.xr[(size_t)k] + s.xi[(size_t)k] * s.xi[(size_t)k]) * norm;
        m = std::max(m, mag * mag);
        sc = std::max(sc, s.ps[(size_t)k]);
        cut = std::max(cut, depth * s.eff[(size_t)k] * bw[(size_t)k]);
      }
      dispMain[i].store(10.f * std::log10(std::max(m, 1e-12f)), std::memory_order_relaxed);
      dispSc[i].store(10.f * std::log10(std::max(sc, 1e-12f)), std::memory_order_relaxed);
      dispCut[i].store(cut, std::memory_order_relaxed);
    }
  }

  double fs = 48000.0;
  int N = 0, HOP = 0, K = 0;
  SpacerParams params;
  FFT fft;
  Chan ch[kMaxCh];
  std::vector<float> win, re, im, P, cum, bw;
  int wpos = 0, hopCount = 0;
  float mix = 0.f, mixStep = 0.002f;
  bool bandDirty = true, hasSc = false;
  int dispLo[kDisp] = {}, dispHi[kDisp] = {};
  std::atomic<float> dispMain[kDisp], dispSc[kDisp], dispCut[kDisp];
};

} // namespace skell
