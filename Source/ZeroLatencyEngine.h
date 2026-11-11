#pragma once
// Zero-latency "Live" engine for Skell Spacer (no FFT in the audio path, no lookahead).
//
//   sidechain --> 60-band filter bank --> level per band --> cut depth per band (per channel)
//   track     --> chain of 60 minimum-phase bell filters (one cut per band) --> out
//
// Bells whose cut is zero are skipped, so an idle engine returns the input bit-exactly.
// The FFT here only feeds the spectrum display and is never in the audio path.
#include "SpacerDSP.h"

namespace skell {

class ZeroLatencyEngine {
public:
  static constexpr int kBandsPerOct = 6;     // 1/6-octave bands, 20 Hz .. 20 kHz
  static constexpr int kBands = 10 * kBandsPerOct;
  static constexpr int kDisp = SpacerEngine::kDisp;
  static constexpr int kMaxCh = SpacerEngine::kMaxCh;
  static constexpr int kDispN = 4096;        // display FFT only
  static constexpr int kSpan = kBandsPerOct / 2 + 1;
  static constexpr float kMaxCutDb = SpacerEngine::kMaxCutDb;
  static constexpr float kBellQ = 8.65f;     // 1/6-octave bells: 1 / (2^(1/12) - 2^(-1/12))
  static constexpr float kAnaQ = 6.0f;       // per stage; two stages per analysis band
  static constexpr float kFullDb = 6.f;      // full cut this far below the sidechain's peak band
  static constexpr float kNoneDb = 36.f;     // no cut this far below it

  ZeroLatencyEngine() { prepare(48000.0); }

  int latencySamples() const { return 0; }

  // Never call concurrently with process().
  void prepare(double sampleRate) {
    fs = std::isfinite(sampleRate) && sampleRate >= 1000.0 ? std::min(sampleRate, 384000.0) : 44100.0;
    mixStep = (float)(1.0 / (0.010 * fs));
    step = fs <= 50000.0 ? 16 : (fs <= 100000.0 ? 32 : 64);
    stepSec = (float)(step / fs);

    nb = 0;
    for (int i = 0; i < kBands; ++i) {
      fc[i] = 20.f * std::exp2(((float)i + 0.5f) / (float)kBandsPerOct);
      if (fc[i] < 0.45f * (float)fs) nb = i + 1;
    }
    const float k = 1.0f / kAnaQ;
    for (int i = 0; i < nb; ++i) {
      const float g = (float)std::tan(kPi * fc[i] / fs);
      anaA1[i] = 1.0f / (1.0f + g * (g + k));
      anaA2[i] = g * anaA1[i];
      anaA3[i] = g * anaA2[i];
      bellG[i] = g;
      const float tau = std::max(0.004f, 2.5f / fc[i]);
      anaPowCoef[i] = 1.0f - std::exp(-1.0f / ((float)fs * tau));
    }
    // Overlapping bells add in dB; scale each so a uniform request lands near the requested depth.
    const int mid = nb / 2;
    float sum = 0.f;
    for (int j = 0; j < nb; ++j) sum += bellDb(fc[mid], fc[j], -12.f);
    invNorm = 1.0f / std::max(1.0f, sum / -12.f);

    dispFft.init(kDispN);
    dispWin.resize(kDispN);
    for (int n = 0; n < kDispN; ++n) dispWin[(size_t)n] = 0.5f - 0.5f * (float)std::cos(2.0 * kPi * n / kDispN);
    dispRe.assign(kDispN, 0.f); dispIm.assign(kDispN, 0.f);
    dispTrack.assign(kDispN, 0.f); dispSc.assign(kDispN, 0.f);
    const double binHz = fs / kDispN;
    for (int i = 0; i < kDisp; ++i) {
      auto freq = [](double t) { return 20.0 * std::pow(1000.0, t); };
      const double t = (double)i / (kDisp - 1), dt = 1.0 / (kDisp - 1);
      int lo = (int)std::floor(freq(t - 0.5 * dt) / binHz), hi = (int)std::ceil(freq(t + 0.5 * dt) / binHz);
      lo = std::min(std::max(lo, 1), kDispN / 2);
      hi = std::min(std::max(hi, lo), kDispN / 2);
      dispLo[i] = lo; dispHi[i] = hi;
    }
    bandDirty = true;
    reset();
  }

  void reset() {
    clearAnalysis();
    std::memset(env, 0, sizeof env);
    std::memset(cutDb, 0, sizeof cutDb);
    for (auto& c : bell) for (auto& b : c) b = Bell();
    peak[0] = peak[1] = 0.f;
    stepCount = 0; mix = 0.f; anyActive = false; prevShared = true;
    std::fill(dispTrack.begin(), dispTrack.end(), 0.f);
    std::fill(dispSc.begin(), dispSc.end(), 0.f);
    dispPos = 0; dispCount = 0;
    for (int i = 0; i < kDisp; ++i) { dispMain[i] = -120.f; dispScOut[i] = -120.f; dispCut[i] = 0.f; }
  }

  // Safe to call from the audio thread.
  void setParams(SpacerParams p) {
    auto fin = [](float v, float lo, float hi, float def) { return std::isfinite(v) ? std::min(hi, std::max(lo, v)) : def; };
    p.amount = fin(p.amount, 0.f, 1.f, 0.f);
    p.smooth = fin(p.smooth, 0.f, 1.f, 0.f);
    p.link = fin(p.link, 0.f, 1.f, 0.f);
    p.pan = fin(p.pan, -1.f, 1.f, 0.f);
    p.attackMs = fin(p.attackMs, 1.f, 200.f, 10.f);
    p.releaseMs = fin(p.releaseMs, 5.f, 2000.f, 150.f);
    p.lowHz = fin(p.lowHz, 20.f, 20000.f, 20.f);
    p.highHz = fin(p.highHz, 20.f, 20000.f, 20000.f);
    if (p.highHz < p.lowHz * 1.25f) {
      p.highHz = p.lowHz * 1.25f;
      if (p.highHz > 20000.f) { p.highHz = 20000.f; p.lowHz = 16000.f; }
    }
    if (p.lowHz != params.lowHz || p.highHz != params.highHz) bandDirty = true;
    params = p;
  }

  // In-place on `main` (1 or 2 channels). `sc` may be null / nSc == 0.
  void process(float* const* main, const float* const* sc, int nCh, int nSc, int nSamples) {
    if (!main || nCh < 1 || nSamples <= 0) return;
    nCh = std::min(nCh, kMaxCh);
    for (int c = 0; c < nCh; ++c) if (!main[c]) return;
    if (!sc || nSc > kMaxCh) nSc = sc ? kMaxCh : 0;
    for (int c = 0; c < nSc; ++c) if (!sc[c]) { nSc = 0; break; }

    const bool wantMs = params.ms && nCh == 2;
    if (mix == 0.f && wantMs != msApplied) { msApplied = wantMs; resetBells(); }   // domain flips only while fully dry (after a 10 ms fade)
    const float target = (params.power && params.amount > 1e-4f && wantMs == msApplied) ? 1.f : 0.f;
    const bool ms = msApplied;
    // One detector (shared by both sides) unless a stereo sidechain drives a stereo track in L/R mode.
    const bool shared = !(nSc >= 2 && nCh == 2 && !ms);
    if (shared != prevShared) { clearAnalysis(); prevShared = shared; }
    if (nSc == 0 && hadSc) { clearAnalysis(); hadSc = false; }
    if (nSc > 0) hadSc = true;

    for (int i = 0; i < nSamples; ++i) {
      if (mix < target) mix = std::min(target, mix + mixStep);
      else if (mix > target) mix = std::max(target, mix - mixStep);

      const float x0 = clean(main[0][i]), x1 = nCh > 1 ? clean(main[1][i]) : 0.f;
      float d0 = 0.f, d1 = 0.f;
      if (nSc > 0) {
        if (shared) d0 = nSc >= 2 ? 0.5f * (clean(sc[0][i]) + clean(sc[1][i])) : clean(sc[0][i]);
        else { d0 = clean(sc[0][i]); d1 = clean(sc[1][i]); }
        analyse(0, d0);
        if (!shared) analyse(1, d1);
      }
      feedDisplay(nCh > 1 ? 0.5f * (x0 + x1) : x0, shared ? d0 : 0.5f * (d0 + d1));

      float y0 = x0, y1 = x1;
      if (mix > 0.f && anyActive) {
        float a = x0, b = x1;
        if (ms) { a = (x0 + x1) * 0.5f; b = (x0 - x1) * 0.5f; }
        a = runChain(0, a);
        if (nCh > 1) b = runChain(1, b);
        if (ms) { y0 = a + b; y1 = a - b; } else { y0 = a; y1 = b; }
        if (!std::isfinite(y0) || !std::isfinite(y1)) { resetBells(); y0 = x0; y1 = x1; }
        else if (mix < 1.f) { y0 = x0 + (y0 - x0) * mix; y1 = x1 + (y1 - x1) * mix; }
      }
      main[0][i] = y0;
      if (nCh > 1) main[1][i] = y1;

      if (++stepCount >= step) { stepCount = 0; updateControl(nCh, shared); }
    }
  }

  void getDisplay(float* mainDb, float* scDb, float* cutOut) const {
    for (int i = 0; i < kDisp; ++i) {
      mainDb[i] = dispMain[i].load(std::memory_order_relaxed);
      scDb[i] = dispScOut[i].load(std::memory_order_relaxed);
      cutOut[i] = dispCut[i].load(std::memory_order_relaxed);
    }
  }

private:
  struct Bell { float a1 = 0, a2 = 0, a3 = 0, m1 = 0, ic1 = 0, ic2 = 0; bool on = false; };

  static float clean(float v) { return std::isfinite(v) ? v : 0.f; }
  static float clamp01(float v) { return std::min(1.f, std::max(0.f, v)); }
  static float smoothstep(float t) { t = clamp01(t); return t * t * (3.f - 2.f * t); }

  static float bellDb(float f, float fcBell, float gainDb) {
    const float A = std::pow(10.f, gainDb / 40.f), k = 1.f / (kBellQ * A), w = f / fcBell;
    const float d = 1.f - w * w;
    return 10.f * std::log10(((d * d) + (k * A * A * w) * (k * A * A * w)) / ((d * d) + (k * w) * (k * w)));
  }

  void clearAnalysis() {
    std::memset(anaState, 0, sizeof anaState);
    std::memset(anaPow, 0, sizeof anaPow);
  }
  void resetBells() { for (auto& c : bell) for (auto& b : c) { b.ic1 = b.ic2 = 0.f; } }

  void buildBand() {
    for (int i = 0; i < nb; ++i) {
      float wl = 1.f, wh = 1.f;
      if (params.lowHz > 20.01f)    wl = smoothstep((std::log2(fc[i] / params.lowHz) + 1.f / 6.f) * 3.f);
      if (params.highHz < 19999.f)  wh = 1.f - smoothstep((std::log2(fc[i] / params.highHz) + 1.f / 6.f) * 3.f);
      bandWeight[i] = wl * wh;
    }
    bandDirty = false;
  }

  // detector d: sidechain -> two cascaded unity-peak band-passes per band -> smoothed power
  void analyse(int d, float x) {
    x += 1.0e-25f;
    float* st = anaState[d];
    float* pw = anaPow[d];
    for (int i = 0; i < nb; ++i) {
      float* c = st + 4 * i;
      const float a1 = anaA1[i], a2 = anaA2[i], a3 = anaA3[i];
      float v3 = x - c[1];
      float v1 = a1 * c[0] + a2 * v3;
      float v2 = c[1] + a2 * c[0] + a3 * v3;
      c[0] = 2.f * v1 - c[0]; c[1] = 2.f * v2 - c[1];
      const float y = v1 * (1.f / kAnaQ);
      v3 = y - c[3];
      v1 = a1 * c[2] + a2 * v3;
      v2 = c[3] + a2 * c[2] + a3 * v3;
      c[2] = 2.f * v1 - c[2]; c[3] = 2.f * v2 - c[3];
      const float z = v1 * (1.f / kAnaQ);
      pw[i] += (z * z - pw[i]) * anaPowCoef[i] + 1.0e-30f;
    }
  }

  float runChain(int c, float x) {
    for (int i = 0; i < nb; ++i) {
      Bell& b = bell[c][i];
      if (!b.on) continue;
      const float v3 = x - b.ic2;
      const float v1 = b.a1 * b.ic1 + b.a2 * v3;
      const float v2 = b.ic2 + b.a2 * b.ic1 + b.a3 * v3;
      b.ic1 = 2.f * v1 - b.ic1;
      b.ic2 = 2.f * v2 - b.ic2;
      x += b.m1 * v1;
    }
    return x;
  }

  // every `step` samples: levels -> depth -> envelope -> link/pan -> bell coefficients
  void updateControl(int nCh, bool shared) {
    if (bandDirty) buildBand();
    const float aCoef = std::exp(-stepSec / std::max(0.001f, params.attackMs * 0.001f));
    const float rCoef = std::exp(-stepSec / std::max(0.005f, params.releaseMs * 0.001f));
    const float peakDecay = std::exp(-stepSec / 2.0f);
    const float h = params.smooth * 0.5f * (float)kBandsPerOct;   // spread (in bands) of the detected power (up to 1/2 octave)
    const int nDet = shared ? 1 : 2;

    for (int d = 0; d < nDet; ++d) {
      float ps[kBands], curPeak = 0.f;
      for (int i = 0; i < nb; ++i) {
        float sum = 0.f, wsum = 0.f;
        for (int j = -kSpan; j <= kSpan; ++j) {
          const int q = i + j;
          if (q < 0 || q >= nb) continue;
          const float w = clamp01(h - (float)std::abs(j) + 1.f);
          sum += w * anaPow[d][q]; wsum += w;
        }
        ps[i] = 2.f * sum / wsum;                 // unity-peak band-pass: sine power A^2/2 -> A^2
        curPeak = std::max(curPeak, ps[i]);
      }
      peak[d] = std::max(curPeak, peak[d] * peakDecay);
      const float peakDb = 10.f * std::log10(std::max(peak[d], 1e-20f));

      for (int i = 0; i < nb; ++i) {
        const float db = 10.f * std::log10(std::max(ps[i], 1e-20f));
        const float rel = clamp01((db - peakDb + kNoneDb) / (kNoneDb - kFullDb));
        const float gate = clamp01((db + 85.f) / 10.f);
        const float t = rel * gate;
        if (!params.freeze) {
          float& e = env[d][i];
          const float cf = t > e ? aCoef : rCoef;
          e = t + (e - t) * cf;
          if (e < 1.0e-4f && t < 1.0e-4f) e = 0.f;
        }
      }
    }

    // pan: 0 = both sides, towards an edge = only that side (L/R or M/S)
    float w[2] = { params.pan > 0.f ? 1.f - params.pan : 1.f, params.pan < 0.f ? 1.f + params.pan : 1.f };
    if (nCh == 1) w[0] = 1.f;
    anyActive = false;
    for (int i = 0; i < nb; ++i) {
      const float e0 = env[0][i], e1 = shared ? e0 : env[1][i], deeper = std::max(e0, e1);
      const float eff[2] = { e0 + (deeper - e0) * params.link, e1 + (deeper - e1) * params.link };
      cutDb[i] = params.amount * kMaxCutDb * e0 * bandWeight[i];        // shown in the display
      for (int c = 0; c < nCh; ++c) {
        Bell& b = bell[c][i];
        if (std::fabs(b.ic1) < 1.0e-20f) b.ic1 = 0.f;          // flush decaying state before it goes denormal
        if (std::fabs(b.ic2) < 1.0e-20f) b.ic2 = 0.f;
        const float cut = params.amount * kMaxCutDb * eff[c] * bandWeight[i] * w[c] * invNorm;
        if (cut < 0.003f) { if (b.on) { b.on = false; b.ic1 = b.ic2 = 0.f; } continue; }
        const float A = std::exp(-cut * 0.05756463f);          // 10^(-cut/40)
        const float k = 1.f / (kBellQ * A), g = bellG[i];
        b.a1 = 1.f / (1.f + g * (g + k));
        b.a2 = g * b.a1; b.a3 = g * b.a2;
        b.m1 = k * (A * A - 1.f);
        b.on = true; anyActive = true;
      }
    }
  }

  // ---- display (runs on the audio thread, never touches the audio path) ----
  void feedDisplay(float track, float side) {
    dispTrack[(size_t)dispPos] = track;
    dispSc[(size_t)dispPos] = side;
    dispPos = (dispPos + 1) & (kDispN - 1);
    if (++dispCount >= kDispN / 2) { dispCount = 0; publishDisplay(); }
  }

  void publishDisplay() {
    for (int n = 0; n < kDispN; ++n) {
      const int idx = (dispPos + n) & (kDispN - 1);
      dispRe[(size_t)n] = dispTrack[(size_t)idx] * dispWin[(size_t)n];
      dispIm[(size_t)n] = dispSc[(size_t)idx] * dispWin[(size_t)n];
    }
    dispFft.forward(dispRe.data(), dispIm.data());
    const float norm = 4.0f / (float)kDispN;            // Hann: sine amplitude A -> |X| = A*N/4
    for (int i = 0; i < kDisp; ++i) {
      float m = 0.f, sc = 0.f;
      for (int k = dispLo[i]; k <= dispHi[i]; ++k) {
        const int j = (kDispN - k) & (kDispN - 1);
        const float xr = 0.5f * (dispRe[(size_t)k] + dispRe[(size_t)j]), xi = 0.5f * (dispIm[(size_t)k] - dispIm[(size_t)j]);
        const float sr = 0.5f * (dispIm[(size_t)k] + dispIm[(size_t)j]), si = -0.5f * (dispRe[(size_t)k] - dispRe[(size_t)j]);
        m = std::max(m, (xr * xr + xi * xi) * norm * norm);
        sc = std::max(sc, (sr * sr + si * si) * norm * norm);
      }
      dispMain[i].store(10.f * std::log10(std::max(m, 1e-12f)), std::memory_order_relaxed);
      dispScOut[i].store(10.f * std::log10(std::max(sc, 1e-12f)), std::memory_order_relaxed);
      const float t = (float)i / (float)(kDisp - 1);
      const float pos = std::min(std::max(std::log2(std::pow(1000.f, t)) * (float)kBandsPerOct - 0.5f, 0.f), (float)(nb - 1));
      const int lo = (int)pos, hi = std::min(lo + 1, nb - 1);
      dispCut[i].store(cutDb[lo] + (cutDb[hi] - cutDb[lo]) * (pos - (float)lo), std::memory_order_relaxed);
    }
  }

  double fs = 48000.0;
  SpacerParams params;
  int nb = kBands, step = 16, stepCount = 0;
  float stepSec = 0.f, invNorm = 1.f, mix = 0.f, mixStep = 0.002f, peak[2] = {};
  bool msApplied = false, bandDirty = true, anyActive = false, hadSc = false, prevShared = true;
  float fc[kBands] = {}, bandWeight[kBands] = {}, bellG[kBands] = {};
  float anaA1[kBands] = {}, anaA2[kBands] = {}, anaA3[kBands] = {}, anaPowCoef[kBands] = {};
  float anaState[2][kBands * 4] = {}, anaPow[2][kBands] = {};
  float env[2][kBands] = {}, cutDb[kBands] = {};
  Bell bell[kMaxCh][kBands];

  FFT dispFft;
  std::vector<float> dispWin, dispRe, dispIm, dispTrack, dispSc;
  int dispPos = 0, dispCount = 0, dispLo[kDisp] = {}, dispHi[kDisp] = {};
  std::atomic<float> dispMain[kDisp], dispScOut[kDisp], dispCut[kDisp];
};

} // namespace skell
