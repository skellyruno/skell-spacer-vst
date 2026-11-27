#pragma once
// Zero-latency "Live" engine for Skell Spacer (no FFT in the audio path, no lookahead).
//
//   sidechain --> 84-band filter bank --> level per band --> cut depth per band (per channel)
//   track     --> chain of up to 84 minimum-phase bell filters (one cut per band) --> out
//
// Bells whose cut is zero are skipped, so an idle engine returns the input bit-exactly.
// Spectrum FFT and response visualizations are UI-thread-only; audio publishes atomic snapshots.
#include <cstdint>
#include "SpacerDSP.h"
#include "LevelSensitivity.h"
#include "DetectorResponse.h"

namespace skell {

class ZeroLatencyEngine {
public:
  static constexpr int kBandsPerOct = 8;     // 1/8-octave bands, 20 Hz .. 20 kHz
  static constexpr int kBands = 10 * kBandsPerOct + 4;   // 20 Hz up to ~30 kHz (bands above 0.45 fs are dropped); the extra bands keep the top of the audible range covered
  static constexpr int kDisp = 256;          // points in the display curves (20 Hz .. 20 kHz, log spaced)
  static constexpr int kMaxCh = 2;
  static constexpr int kDispN = 4096;        // display FFT only
  static constexpr int kSpan = kBandsPerOct / 2 + 1;
  static constexpr int kReach = 6;           // bells further than this many bands away are ignored when solving for the cuts
  static constexpr float kMaxCutDb = 24.f;
  static constexpr float kBellQ = 11.54f;    // 1/8-octave bells: 1 / (2^(1/16) - 2^(-1/16)); the Q actually used is bellQ[i] (below)
  static constexpr float kAnaQ = 8.0f;       // per stage; two stages per analysis band (anaK[i] = 1 / its Q)
  static constexpr float kFullDb = 6.f;      // full cut this far below the sidechain's peak band
  static constexpr float kNoneDb = 30.f;     // no cut this far below it

  ZeroLatencyEngine() { prepare(48000.0); }

  int latencySamples() const { return 0; }

  // Diagnostic snapshots: measured power before/after processing (not monitor),
  // derivative-energy retention during detected onsets, and model-based overlap
  // and target protection. Not a perceptual quality score.
  struct SmartSnapshot {
    float overlap[kBands] = {}, protection[kBands] = {};
    float inputPower = 0.f, outputPower = 0.f, onsetInput = 0.f, onsetOutput = 0.f;
  };
  SmartSnapshot getSmartSnapshot() const {
    SmartSnapshot r;
    for (int i = 0; i < kBands; ++i) {
      r.overlap[i] = diagOverlap[i].load(std::memory_order_relaxed);
      r.protection[i] = diagProtection[i].load(std::memory_order_relaxed);
    }
    r.inputPower = diagInput.load(std::memory_order_relaxed);
    r.outputPower = diagOutput.load(std::memory_order_relaxed);
    r.onsetInput = diagOnsetIn.load(std::memory_order_relaxed);
    r.onsetOutput = diagOnsetOut.load(std::memory_order_relaxed);
    return r;
  }

  // Never call concurrently with process().
  void prepare(double sampleRate) {
    fs = std::isfinite(sampleRate) && sampleRate >= 1000.0 ? std::min(sampleRate, 384000.0) : 44100.0;
    mixStep = (float)(1.0 / (0.010 * fs));
    guardDecay = (float)std::exp(-1.0 / (0.010 * fs));
    guardRelease = (float)(1.0 - std::exp(-1.0 / (0.005 * fs)));
    // Detect strong stereo anti-phase energy, then smoothly adapt the mono key.
    // Normal correlated L/R sidechains stay numerically unchanged.
    phaseEnergyCoef = (float)(1.0 - std::exp(-1.0 / (0.020 * fs)));
    phaseBlendCoef = (float)(1.0 - std::exp(-1.0 / (0.050 * fs)));
    step = fs <= 50000.0 ? 16 : (fs <= 100000.0 ? 32 : 64);
    stepSec = (float)(step / fs);
    smartStep = std::min(1.f, stepSec / 0.050f);
    onsetFastCoef = (float)(1.0 - std::exp(-1.0 / (0.003 * fs)));
    onsetSlowCoef = (float)(1.0 - std::exp(-1.0 / (0.060 * fs)));
    meterCoef = (float)(1.0 - std::exp(-1.0 / (0.250 * fs)));
    sensitivity.prepare(stepSec);
    updateTimeConstants();
    updateSpread();

    nb = 0;
    for (int i = 0; i < kBands; ++i) {
      fc[i] = 20.f * std::exp2(((float)i + 0.5f) / (float)kBandsPerOct);
      if (fc[i] < 0.45f * (float)fs) nb = i + 1;
    }
    for (int i = 0; i < nb; ++i) {
      const float g = (float)std::tan(kPi * fc[i] / fs);
      // The bilinear transform squeezes the top of the spectrum: a filter of fixed Q gets narrower (in Hz) towards
      // Nyquist. Keep every band 1/8 octave wide in Hz by lowering Q as the centre approaches Nyquist.
      const float widthHz = fc[i] * (std::exp2(0.5f / (float)kBandsPerOct) - std::exp2(-0.5f / (float)kBandsPerOct));
      bellQ[i] = std::min(kBellQ, g / ((float)kPi / (float)fs * (1.0f + g * g) * widthHz));
      const float k = kBellQ / (kAnaQ * bellQ[i]);       // = 1 / analysis Q
      anaK[i] = k;
      anaA1[i] = 1.0f / (1.0f + g * (g + k));
      anaA2[i] = g * anaA1[i];
      anaA3[i] = g * anaA2[i];
      bellG[i] = g;
      const float tau = std::max(0.004f, 2.5f / fc[i]);
      anaPowCoef[i] = 1.0f - std::exp(-1.0f / ((float)fs * tau));
      fastPowCoef[i] = DetectorResponse::fastCoefficient(fs, fc[i]);
      density[i] = 1000.0f / fc[i];                 // 1 / bandwidth: every band is 1/8 octave wide in Hz
    }
    // Neighbouring bells overlap and their cuts add in dB. The cuts are solved for (updateControl) so that the summed
    // response hits the requested depth at every band centre AND halfway between bands: kernC[i][k] / kernM[i][k] say how
    // much of a unit cut on band i+k lands on the centre of band i / on the point between bands i and i+1 (real,
    // bilinear-warped bell shapes). That removes the ripple a plain "scale everything down" would leave.
    for (int i = 0; i < nb; ++i) {
      const float fMid = std::sqrt(fc[i] * fc[std::min(i + 1, nb - 1)]);
      for (int k = -kReach; k <= kReach; ++k) {
        const int j = i + k;
        const bool ok = j >= 0 && j < nb;
        kernC[i][k + kReach] = ok ? bellDb(warp(fc[i]) / bellG[j], -8.f, bellQ[j]) / -8.f : 0.f;
        kernM[i][k + kReach] = ok ? bellDb(warp(fMid) / bellG[j], -8.f, bellQ[j]) / -8.f : 0.f;
      }
    }
    // safe gradient step: 1 / (largest row sum x largest column sum) bounds the largest eigenvalue of M^T M
    float maxRow = 1.f, maxCol = 1.f; float col[kBands] = {};
    for (int i = 0; i < nb; ++i) {
      float rc = 0.f, rm = 0.f;
      for (int k = -kReach; k <= kReach; ++k) {
        const int j = i + k; if (j < 0 || j >= nb) continue;
        rc += std::fabs(kernC[i][k + kReach]); col[j] += std::fabs(kernC[i][k + kReach]);
        if (i < nb - 1) { rm += std::fabs(kernM[i][k + kReach]); col[j] += std::fabs(kernM[i][k + kReach]); }
      }
      maxRow = std::max(maxRow, std::max(rc, rm));
    }
    for (int j = 0; j < nb; ++j) maxCol = std::max(maxCol, col[j]);
    lsStep = 0.9f / (maxRow * maxCol);

    // Display FFT resources initialize lazily on the GUI thread, never in audio.
    displayRate.store(fs, std::memory_order_release);
    bandDirty = true;
    updateScFilter();
    reset();
  }

  void reset() {
    clearAnalysis();
    sensitivity.reset();
    clearSmart();
    smartBlend = deltaMix = 0.f;
    diagInput.store(0.f); diagOutput.store(0.f);
    diagOnsetIn.store(0.f); diagOnsetOut.store(0.f);
    for (int i = 0; i < kBands; ++i) {
      diagOverlap[i].store(0.f); diagProtection[i].store(0.f);
    }
    detectorBlend = 0.f;
    std::copy(anaPowCoef, anaPowCoef + nb, activePowCoef);
    std::memset(env, 0, sizeof env);
    std::memset(applied, 0, sizeof applied);
    std::memset(solved, 0, sizeof solved);
    for (auto& c : bell) for (auto& b : c) b = Bell();
    peak = 0.f;
    for (auto& count : activeCount) count = 0;
    stepCount = 0; mix = 0.f; anyActive = false; guardPk = 0.f; guardGain = 1.f;
    phaseMidEnergy = phaseSideEnergy = phaseSideBlend = 0.f;
    for (int i = 0; i < kDispN; ++i) {
      dispTrack[i].store(0.f, std::memory_order_relaxed);
      dispSc[i].store(0.f, std::memory_order_relaxed);
    }
    for (int c = 0; c < kMaxCh; ++c)
      for (int i = 0; i < kBands; ++i)
        dispApplied[c][i].store(0.f, std::memory_order_relaxed);
    dispPos = 0; dispCount = 0;
    displayPublishedPos.store(0, std::memory_order_release);
  }

  // Safe to call from the audio thread.
  void setParams(SpacerParams p) {
    auto fin = [](float v, float lo, float hi, float def) { return std::isfinite(v) ? std::min(hi, std::max(lo, v)) : def; };
    p.amount = fin(p.amount, 0.f, 1.f, 0.f);
    p.smooth = fin(p.smooth, 0.f, 1.f, 0.f);
    p.levelSensitivity = fin(p.levelSensitivity, 0.f, 1.f, 0.f);
    p.smart = fin(p.smart, 0.f, 1.f, 0.f);
    p.punchProtect = fin(p.punchProtect, 0.f, 1.f, 0.f);
    p.bassProtect = fin(p.bassProtect, 0.f, 1.f, 0.f);
    p.adaptiveRelease = fin(p.adaptiveRelease, 0.f, 1.f, 0.f);
    p.pan = fin(p.pan, -1.f, 1.f, 0.f);
    p.attackMs = fin(p.attackMs, 1.f, 200.f, 10.f);
    p.releaseMs = fin(p.releaseMs, 5.f, 2000.f, 150.f);
    p.lowHz = fin(p.lowHz, 20.f, 20000.f, 20.f);
    p.highHz = fin(p.highHz, 20.f, 20000.f, 20000.f);
    if (p.highHz < p.lowHz * 1.25f) {
      p.highHz = p.lowHz * 1.25f;
      if (p.highHz > 20000.f) { p.highHz = 20000.f; p.lowHz = 16000.f; }
    }
    p.scLowHz = fin(p.scLowHz, 20.f, 20000.f, 20.f);
    p.scHighHz = fin(p.scHighHz, 20.f, 20000.f, 20000.f);
    if (p.scHighHz < p.scLowHz * 1.25f) {          // keep a pass-band open: the filters never collapse into silence
      p.scHighHz = p.scLowHz * 1.25f;
      if (p.scHighHz > 20000.f) { p.scHighHz = 20000.f; p.scLowHz = 16000.f; }
    }
    if (p.lowHz != params.lowHz || p.highHz != params.highHz) bandDirty = true;
    const bool scChanged = p.scLowHz != params.scLowHz || p.scHighHz != params.scHighHz;
    const bool timeChanged = p.attackMs != params.attackMs || p.releaseMs != params.releaseMs;
    const bool spreadChanged = p.smooth != params.smooth;
    params = p;
    if (timeChanged) updateTimeConstants();
    if (spreadChanged) updateSpread();
    if (scChanged) updateScFilter();
  }

  // In-place on `main` (1 or 2 channels). `sc` may be null / nSc == 0.
  // `scOut` (optional, kMaxCh channels of nSamples) receives the sidechain exactly as the detector hears it (after the
  // sidechain filters), for monitoring.
  void process(float* const* main, const float* const* sc, int nCh, int nSc, int nSamples, float* const* scOut = nullptr) {
    if (!main || nCh < 1 || nSamples <= 0) return;
    nCh = std::min(nCh, kMaxCh);
    for (int c = 0; c < nCh; ++c) if (!main[c]) return;
    nSc = sc ? std::clamp(nSc, 0, kMaxCh) : 0;
    for (int c = 0; c < nSc; ++c) if (!sc[c]) { nSc = 0; break; }

    const bool wantMs = params.ms && nCh == 2;
    if (mix == 0.f && wantMs != msApplied) { msApplied = wantMs; resetBells(); }   // domain flips only while fully dry (after a 10 ms fade)
    const float target = (params.power && params.amount > 1e-4f && wantMs == msApplied) ? 1.f : 0.f;
    const bool ms = msApplied;
    if (nSc == 0 && hadSc) {
      clearAnalysis(); hadSc = false;
      phaseMidEnergy = phaseSideEnergy = phaseSideBlend = 0.f;
    }
    if (nSc > 0) hadSc = true;

    for (int i = 0; i < nSamples; ++i) {
      if (mix < target) mix = std::min(target, mix + mixStep);
      else if (mix > target) mix = std::max(target, mix - mixStep);

      const float x0 = clean(main[0][i]), x1 = nCh > 1 ? clean(main[1][i]) : 0.f;
      float d = 0.f;      // the sidechain, filtered, then summed to mono
      if (nSc > 0) {
        float s0 = scFilter(0, DetectorResponse::safeKey(sc[0][i])), s1 = 0.f;
        if (nSc >= 2) s1 = scFilter(1, DetectorResponse::safeKey(sc[1][i]));
        d = s0;
        if (nSc >= 2) {
          // The usual mono key is the mid channel. When stereo key channels
          // nearly cancel, blend toward the side component over 50 ms.
          // This avoids sample-by-sample polarity switches and adds no delay.
          const float mid = 0.5f * (s0 + s1), side = 0.5f * (s0 - s1);
          phaseMidEnergy += phaseEnergyCoef * (mid * mid - phaseMidEnergy);
          phaseSideEnergy += phaseEnergyCoef * (side * side - phaseSideEnergy);
          if (phaseMidEnergy < 1e-20f) phaseMidEnergy = 0.f;
          if (phaseSideEnergy < 1e-20f) phaseSideEnergy = 0.f;
          const float desired = phaseSideEnergy > 2.f * phaseMidEnergy && phaseSideEnergy > 1e-12f ? 1.f : 0.f;
          phaseSideBlend += phaseBlendCoef * (desired - phaseSideBlend);
          if (std::fabs(phaseSideBlend) < 1e-12f) phaseSideBlend = 0.f;
          d = mid + (side - mid) * phaseSideBlend;
        } else {
          phaseMidEnergy = phaseSideEnergy = phaseSideBlend = 0.f;
        }
        if (scOut) {
          // Export the actual phase-safe, post-filter detector key to Listen.
          if (scOut[0]) scOut[0][i] = d;
          if (scOut[1]) scOut[1][i] = d;
        }
        analyse(d);
      }
      const bool smartActive = params.smart > 0.f || smartBlend > 0.f;
      if (smartActive) {
        analyseMain(0, DetectorResponse::safeKey(x0));
        if (nCh > 1) analyseMain(1, DetectorResponse::safeKey(x1));
        const float u0 = DetectorResponse::safeKey(x0), u1 = DetectorResponse::safeKey(x1);
        const float pwr = std::max(u0*u0, u1*u1);
        onsetFast += (pwr - onsetFast) * onsetFastCoef;
        onsetSlow += (pwr - onsetSlow) * onsetSlowCoef;
        onsetFast = onsetFast < 1e-20f ? 0.f : onsetFast;
        onsetSlow = onsetSlow < 1e-20f ? 0.f : onsetSlow;
      }
      feedDisplay(nCh > 1 ? 0.5f * (x0 + x1) : x0, d);

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
      // Overshoot guard: ducking only removes energy, but filtering shifts phase and can push a peak a little
      // above the input's. Hold the output to the input's recent (10 ms) peak: instant attack, 5 ms release,
      // so it costs no delay and does nothing at all (bit-exact) unless an overshoot really happens.
      guardPk = std::max(std::max(std::fabs(x0), std::fabs(x1)), guardPk * guardDecay);
      if (guardPk < 1.0e-20f) guardPk = 0.f;        // below -400 dBFS: silence (and keeps the follower out of denormals)
      const float outPk = std::max(std::fabs(y0), std::fabs(y1));
      if (guardGain < 1.f) { guardGain += (1.f - guardGain) * guardRelease; if (guardGain > 0.99999f) guardGain = 1.f; }
      if (outPk * guardGain > guardPk) guardGain = guardPk / outPk;
      if (guardGain < 1.f) { y0 *= guardGain; y1 *= guardGain; }
      if (smartActive) updateMeters(x0, x1, y0, y1);
      // Delta is the exact dry minus the normal processed signal, after the
      // overshoot guard. No gain boost or extra buffering. Fade monitor changes.
      if (deltaMix != 0.f || params.deltaListen) {
        deltaMix = params.deltaListen ? std::min(1.f, deltaMix + mixStep)
                                      : std::max(0.f, deltaMix - mixStep);
        const float diff0 = x0 - y0, diff1 = x1 - y1;
        if (deltaMix == 1.f) { y0 = diff0; y1 = diff1; }
        else { y0 += (diff0 - y0) * deltaMix; y1 += (diff1 - y1) * deltaMix; }
      }
      main[0][i] = y0;
      if (nCh > 1) main[1][i] = y1;

      if (++stepCount >= step) { stepCount = 0; updateControl(nCh); }
    }
  }

  // Invoked by the 30 Hz UI timer, not by the realtime audio callback.
  // Each caller owns its FFT scratch space, so multiple editors never lock.
  void getDisplay(float* mainDb, float* scDb, float* cutOut) const {
    if (!mainDb || !scDb || !cutOut) return;
    static thread_local DisplayWorkspace work;
    const double rate = displayRate.load(std::memory_order_acquire);
    if (work.fs != rate) work.prepare(rate); // UI-thread-only memory allocation
    const int pos = displayPublishedPos.load(std::memory_order_acquire);
    for (int n = 0; n < kDispN; ++n) {
      const int idx = (pos + n) & (kDispN - 1);
      work.re[(size_t)n] = dispTrack[(size_t)idx].load(std::memory_order_relaxed) * work.win[(size_t)n];
      work.im[(size_t)n] = dispSc[(size_t)idx].load(std::memory_order_relaxed) * work.win[(size_t)n];
    }
    work.fft.forward(work.re.data(), work.im.data());
    const float norm = 4.0f / (float)kDispN;
    for (int i = 0; i < kDisp; ++i) {
      float m = 0.f, sc = 0.f;
      for (int k = work.lo[i]; k <= work.hi[i]; ++k) {
        const int j = (kDispN - k) & (kDispN - 1);
        const float xr = 0.5f * (work.re[(size_t)k] + work.re[(size_t)j]);
        const float xi = 0.5f * (work.im[(size_t)k] - work.im[(size_t)j]);
        const float sr = 0.5f * (work.im[(size_t)k] + work.im[(size_t)j]);
        const float si = -0.5f * (work.re[(size_t)k] - work.re[(size_t)j]);
        m = std::max(m, (xr*xr + xi*xi) * norm*norm);
        sc = std::max(sc, (sr*sr + si*si) * norm*norm);
      }
      mainDb[i] = 10.f * std::log10(std::max(m, 1e-12f));
      scDb[i] = 10.f * std::log10(std::max(sc, 1e-12f));
    }
    float cut[kMaxCh][kBands];
    for (int c = 0; c < kMaxCh; ++c)
      for (int j = 0; j < kBands; ++j)
        cut[c][j] = dispApplied[c][j].load(std::memory_order_relaxed);
    for (int i = 0; i < kDisp; ++i) {
      float deepest = 0.f;
      for (int c = 0; c < kMaxCh; ++c) {
        float sum = 0.f;
        for (int j = work.firstBand[i]; j < work.endBand[i]; ++j)
          if (cut[c][j] > 0.f)
            sum += bellDb(work.warpedRatio[i][j], -cut[c][j], work.q[j]);
        deepest = std::max(deepest, -sum);
      }
      cutOut[i] = deepest;
    }
  }

private:
  // 2nd-order TPT state-variable filter; two in series with Butterworth Qs give a 24 dB/oct sidechain filter
  struct Svf {
    float a1 = 0, a2 = 0, a3 = 0, k = 1, ic1 = 0, ic2 = 0;
    void set(float g, float q) { k = 1.f / q; a1 = 1.f / (1.f + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    float step(float x, float& lp) {
      const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
      ic1 = 2.f * v1 - ic1; ic2 = 2.f * v2 - ic2;
      lp = v2; return x - k * v1 - v2;      // returns the high-pass output
    }
    void flush() { if (std::fabs(ic1) < 1.0e-20f) ic1 = 0.f; if (std::fabs(ic2) < 1.0e-20f) ic2 = 0.f; }
  };
  struct Bell { float a1 = 0, a2 = 0, a3 = 0, m1 = 0, gx = 1, ic1 = 0, ic2 = 0; bool on = false; };

  static float clean(float v) { return std::isfinite(v) ? v : 0.f; }
  // log2 / exp2 to ~1e-4 relative accuracy (0.001 dB), for the per-step level and coefficient maths
  static float fastLog2(float x) {
    uint32_t u; std::memcpy(&u, &x, 4);
    const float e = (float)((int)(u >> 23) - 127);
    u = (u & 0x007fffffu) | 0x3f800000u;
    float m; std::memcpy(&m, &u, 4);
    const float t = (m - 1.f) / (m + 1.f), t2 = t * t;
    return e + 2.88539008f * t * (1.f + t2 * (0.333333333f + t2 * 0.2f));
  }
  static float fastExp2(float x) {                 // x in about [-100, 100]
    const float fl = std::floor(x), f = x - fl;
    const float p = 1.f + f * (0.693147181f + f * (0.240226507f + f * (0.0555041087f + f * 0.00961812911f)));
    uint32_t u = (uint32_t)((int)fl + 127) << 23; float sc; std::memcpy(&sc, &u, 4);
    return p * sc;
  }
  static float clamp01(float v) { return std::min(1.f, std::max(0.f, v)); }
  static float smoothstep(float t) { t = clamp01(t); return t * t * (3.f - 2.f * t); }

  // frequency -> the bilinear-transform's warped axis; a bell centred on band j sees ratio warp(f) / bellG[j]
  float warp(float f) const { return (float)std::tan(kPi * (double)std::min(f, 0.499f * (float)fs) / fs); }

  // magnitude (dB) of a bell with gain gainDb at warped-frequency ratio w
  static float bellDb(float w, float gainDb, float q) {
    const float A = std::pow(10.f, gainDb / 40.f), k = 1.f / (q * A);
    const float d = 1.f - w * w;
    return 10.f * std::log10(((d * d) + (k * A * A * w) * (k * A * A * w)) / ((d * d) + (k * w) * (k * w)));
  }

  float scFilter(int c, float x) {
    float lp = 0.f;
    if (scHpOn) { x = scHp[c][0].step(x, lp); x = scHp[c][1].step(x, lp); }
    if (scLpOn) { scLp[c][0].step(x, lp); const float l1 = lp; scLp[c][1].step(l1, lp); x = lp; }
    return x;
  }
  void updateScFilter() {
    scHpOn = params.scLowHz > 20.5f;
    scLpOn = params.scHighHz < 19500.f;
    const float nyq = 0.45f * (float)fs;
    const float gh = (float)std::tan(kPi * std::min(params.scLowHz, nyq) / fs);
    const float gl = (float)std::tan(kPi * std::min(params.scHighHz, nyq) / fs);
    for (int c = 0; c < kMaxCh; ++c) {      // 4th-order Butterworth: Q = 0.5412, 1.3066
      scHp[c][0].set(gh, 0.54119610f); scHp[c][1].set(gh, 1.30656296f);
      scLp[c][0].set(gl, 0.54119610f); scLp[c][1].set(gl, 1.30656296f);
      if (!scHpOn) scHp[c][0].ic1 = scHp[c][0].ic2 = scHp[c][1].ic1 = scHp[c][1].ic2 = 0.f;
      if (!scLpOn) scLp[c][0].ic1 = scLp[c][0].ic2 = scLp[c][1].ic1 = scLp[c][1].ic2 = 0.f;
    }
  }

  void clearAnalysis() {
    for (auto& c : scHp) for (auto& f : c) f.ic1 = f.ic2 = 0.f;
    for (auto& c : scLp) for (auto& f : c) f.ic1 = f.ic2 = 0.f;
    std::memset(anaS, 0, sizeof anaS);
    std::memset(anaPow, 0, sizeof anaPow);
  }
  void clearSmart() {
    std::memset(mainAnaS, 0, sizeof mainAnaS);
    std::memset(mainAnaPow, 0, sizeof mainAnaPow);
    onsetFast = onsetSlow = meterIn = meterOut = meterOnsetIn = meterOnsetOut = 0.f;
    prevIn[0] = prevIn[1] = prevOut[0] = prevOut[1] = 0.f;
  }
  void analyseMain(int ch, float x) {
    // Match the two-stage key bank; maintain separate stereo histories so
    // anti-phase instrumentals cannot disappear from the overlap detector.
    x += 1e-25f;
    float* __restrict s0 = mainAnaS[ch][0];
    float* __restrict s1 = mainAnaS[ch][1];
    float* __restrict s2 = mainAnaS[ch][2];
    float* __restrict s3 = mainAnaS[ch][3];
    float* __restrict pw = mainAnaPow[ch];
    for (int i = 0; i < nb; ++i) {
      const float a1 = anaA1[i], a2 = anaA2[i], a3 = anaA3[i];
      float v3 = x - s1[i], v1 = a1 * s0[i] + a2 * v3;
      float v2 = s1[i] + a2 * s0[i] + a3 * v3;
      s0[i] = 2.f * v1 - s0[i]; s1[i] = 2.f * v2 - s1[i];
      v3 = v1 * anaK[i] - s3[i];
      v1 = a1 * s2[i] + a2 * v3;
      v2 = s3[i] + a2 * s2[i] + a3 * v3;
      s2[i] = 2.f * v1 - s2[i]; s3[i] = 2.f * v2 - s3[i];
      const float z = v1 * anaK[i];
      pw[i] += (z*z - pw[i]) * anaPowCoef[i] + 1e-30f;
    }
  }
  float onsetStrength() const {
    return smoothstep((onsetFast - onsetSlow * 1.5f) / std::max(onsetFast, 1e-10f));
  }
  void updateMeters(float x0, float x1, float y0, float y1) {
    const float x[2] = {DetectorResponse::safeKey(x0), DetectorResponse::safeKey(x1)};
    const float y[2] = {DetectorResponse::safeKey(y0), DetectorResponse::safeKey(y1)};
    float pi = 0.f, po = 0.f, di = 0.f, dout = 0.f;
    for (int c = 0; c < 2; ++c) {
      pi += x[c]*x[c]; po += y[c]*y[c];
      const float dx = x[c] - prevIn[c], dy = y[c] - prevOut[c];
      di += dx*dx; dout += dy*dy;
      prevIn[c] = x[c]; prevOut[c] = y[c];
    }
    const float onset = onsetStrength();
    meterIn += (pi - meterIn) * meterCoef;
    meterOut += (po - meterOut) * meterCoef;
    meterOnsetIn += (di * onset - meterOnsetIn) * meterCoef;
    meterOnsetOut += (dout * onset - meterOnsetOut) * meterCoef;
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
  void analyse(float x) {
    x += 1.0e-25f;
    // state laid out per field (not per band) so the loop over bands vectorises
    float* __restrict s0 = anaS[0]; float* __restrict s1 = anaS[1]; float* __restrict s2 = anaS[2]; float* __restrict s3 = anaS[3];
    float* __restrict pw = anaPow;
    for (int i = 0; i < nb; ++i) {
      const float a1 = anaA1[i], a2 = anaA2[i], a3 = anaA3[i];
      float v3 = x - s1[i];
      float v1 = a1 * s0[i] + a2 * v3;
      float v2 = s1[i] + a2 * s0[i] + a3 * v3;
      s0[i] = 2.f * v1 - s0[i]; s1[i] = 2.f * v2 - s1[i];
      const float y = v1 * anaK[i];
      v3 = y - s3[i];
      v1 = a1 * s2[i] + a2 * v3;
      v2 = s3[i] + a2 * s2[i] + a3 * v3;
      s2[i] = 2.f * v1 - s2[i]; s3[i] = 2.f * v2 - s3[i];
      const float z = v1 * anaK[i];
      pw[i] += (z * z - pw[i]) * activePowCoef[i] + 1.0e-30f;
    }
  }

  float runChain(int c, float x) {
    for (int j = 0; j < activeCount[c]; ++j) {
      Bell& b = bell[c][activeIndex[c][j]];
      // x + m1*v1 with v1 = a2*x + (a1*ic1 - a2*ic2) folded into one multiply-add on x, so the serial path
      // through the cascade is a single fused op per bell; the state update runs off that path
      const float v3 = x - b.ic2;
      const float v1 = b.a2 * x + (b.a1 * b.ic1 - b.a2 * b.ic2);
      const float v2 = b.ic2 + b.a2 * b.ic1 + b.a3 * v3;
      const float nx = b.gx * x + b.m1 * (b.a1 * b.ic1 - b.a2 * b.ic2);
      b.ic1 = 2.f * v1 - b.ic1;
      b.ic2 = 2.f * v2 - b.ic2;
      x = nx;
    }
    return x;
  }

  void updateTimeConstants() {
    attackCoef = std::exp(-stepSec / std::max(0.001f, params.attackMs * 0.001f));
    releaseCoef = std::exp(-stepSec / std::max(0.005f, params.releaseMs * 0.001f));
    peakDecayCoef = std::exp(-stepSec / 2.0f);
  }
  void updateSpread() {
    const float h = params.smooth * 0.5f * (float)kBandsPerOct;
    spreadRadius = 0;
    for (int j = -kSpan; j <= kSpan; ++j) {
      const float w = clamp01(h - (float)std::abs(j) + 1.f);
      spreadWeight[j + kSpan] = w;
      if (w > 0.f) spreadRadius = std::max(spreadRadius, std::abs(j));
    }
  }

  // every `step` samples: levels -> depth -> envelope -> pan -> bell coefficients
  void updateControl(int nCh) {
    if (bandDirty) buildBand();
    for (auto& c : scHp) for (auto& f : c) f.flush();
    for (auto& c : scLp) for (auto& f : c) f.flush();
    const float aCoef = attackCoef, rCoef = releaseCoef, peakDecay = peakDecayCoef;
    const float oldSmart = smartBlend;
    smartBlend = params.smart > smartBlend ? std::min(params.smart, smartBlend + smartStep)
                                           : std::max(params.smart, smartBlend - smartStep);
    if (oldSmart > 0.f && smartBlend == 0.f) {
      clearSmart();
      for (int i = 0; i < kBands; ++i) {
        diagOverlap[i].store(0.f, std::memory_order_relaxed);
        diagProtection[i].store(0.f, std::memory_order_relaxed);
      }
    }
    float mainPeak = 0.f;
    if (smartBlend > 0.f) {
      for (int i = 0; i < nb; ++i) {
        const float m = mainAnaPow[0][i] + (nCh > 1 ? mainAnaPow[1][i] : mainAnaPow[0][i]);
        mainPeak = std::max(mainPeak, m * density[i]);
      }
    }
    const float onset = smartBlend > 0.f ? onsetStrength() : 0.f;
    {
      float ps[kBands], curPeak = 0.f;
      for (int i = 0; i < nb; ++i) {
        float sum = 0.f, wsum = 0.f;
        // Skip zero weights, retaining nonzero summation order.
        for (int j = -spreadRadius; j <= spreadRadius; ++j) {
          const int q = i + j;
          if (q < 0 || q >= nb) continue;
          const float w = spreadWeight[j + kSpan];
          sum += w * anaPow[q]; wsum += w;
        }
        ps[i] = 2.f * sum / wsum;                 // unity-peak band-pass: sine power A^2/2 -> A^2
        curPeak = std::max(curPeak, ps[i] * density[i]);
      }
      peak = std::max(curPeak, peak * peakDecay);
      const float peakDb = 3.01029996f * fastLog2(std::max(peak, 1e-20f));
      const float blend = sensitivity.advance(params.levelSensitivity);
      // Faster power analysis is opt-in via Level Sensitivity and the existing
      // short attack range. Coefficients change only at the control cadence;
      // the per-sample bank keeps the same arithmetic and layout.
      const float nextBlend = DetectorResponse::fastBlend(blend, params.attackMs);
      if (nextBlend != detectorBlend) {
        detectorBlend = nextBlend;
        for (int i = 0; i < nb; ++i)
          activePowCoef[i] = DetectorResponse::coefficient(anaPowCoef[i], fastPowCoef[i], detectorBlend);
      }

      for (int i = 0; i < nb; ++i) {
        // Compare bands by power per Hz (the bands are constant-Q, so wide ones collect more of a broadband
        // signal): a flat key then asks for the same cut everywhere, like a per-bin FFT would.
        const float dbDens = 3.01029996f * fastLog2(std::max(ps[i] * density[i], 1e-20f));
        const float db = 3.01029996f * fastLog2(std::max(ps[i], 1e-20f));
        const float rel = clamp01((dbDens - peakDb + kNoneDb) / (kNoneDb - kFullDb));
        const float gate = clamp01((db + 85.f) / 10.f);
        const float legacy = rel * gate;
        // Keep the disabled path exactly the previous expression. Apply the new
        // independent knee before the existing attack/release and overlap solver.
        float t = blend > 0.f ? legacy * LevelSensitivity::scale(ps[i], blend) : legacy;
        float ac = aCoef, rc = rCoef;
        if (smartBlend > 0.f) {
          const float m = mainAnaPow[0][i] + (nCh > 1 ? mainAnaPow[1][i] : mainAnaPow[0][i]);
          // Relative main occupancy plus absolute floor: absent material should
          // not trigger unnecessary cuts; a broadband bed remains eligible.
          const float occupancy = smoothstep((m * density[i]) / std::max(0.025f * mainPeak, 1e-10f));
          const float conflict = occupancy * legacy;
          const float low = 1.f - smoothstep((fc[i] - 55.f) / 125.f);
          const float punch = params.punchProtect * onset * 0.80f;
          const float bass = params.bassProtect * low * (1.f - 0.75f * conflict);
          const float keep = (1.f - punch) * (1.f - bass);
          const float factor = occupancy * keep;
          t *= 1.f + smartBlend * (factor - 1.f);
          // More rapid recovery when the key fades or an instrumental onset
          // needs protection; bass recovers less aggressively than upper bands.
          const float timing = smartBlend * params.adaptiveRelease;
          const float recovery = timing * (1.f - legacy) * (0.35f + 0.65f * (1.f - low));
          rc += (rc*rc*rc - rc) * std::max(recovery, smartBlend * punch);
          ac += (1.f - ac) * timing * low * 0.35f;
          diagOverlap[i].store(conflict, std::memory_order_relaxed);
          diagProtection[i].store(smartBlend * (1.f - keep), std::memory_order_relaxed);
        }
        if (!params.freeze) {
          float& e = env[i];
          const float cf = t > e ? ac : rc;
          e = t + (e - t) * cf;
          if (e < 1.0e-4f && t < 1.0e-4f) e = 0.f;
        }
      }
    }

    // pan: 0 = both sides, towards an edge = only that side (L/R or M/S)
    float w[2] = { params.pan > 0.f ? 1.f - params.pan : 1.f, params.pan < 0.f ? 1.f + params.pan : 1.f };
    if (nCh == 1) w[0] = 1.f;
    anyActive = false;
    for (int c = 0; c < nCh; ++c) activeCount[c] = 0;
    for (int c = nCh; c < kMaxCh; ++c) std::fill(applied[c], applied[c] + kBands, 0.f);
    // solve for the per-band cuts whose overlapped sum gives the requested depth: a few damped gradient steps on the
    // least-squares error at band centres and mid-points, never boosting
    // padded by kReach zeros each side so the kernel loops have fixed trip counts and no bounds tests
    float req[kBands + 1] = {}, pad[kBands + 2 * kReach] = {};
    float* const cut0 = pad + kReach;
    bool needsSolve = false;
    for (int i = 0; i < nb; ++i) {
      req[i] = params.amount * kMaxCutDb * env[i] * bandWeight[i];
      // A nonzero warm start must decay through the original solver.
      needsSolve |= req[i] != 0.f || solved[i] != 0.f;
    }
    req[nb] = req[nb - 1];                          // the last band has no mid-point above it: mid-point target = its own
    std::copy(solved, solved + nb, cut0);           // warm start: the previous control step's solution
    for (int it = 0; needsSolve && it < 2; ++it) {
      float accPad[kBands + 2 * kReach] = {};
      float* const acc = accPad + kReach;
      for (int i = 0; i < nb; ++i) {
        const float* kc = kernC[i]; const float* km = kernM[i]; const float* cp = cut0 + i - kReach;
        float totC = 0.f, totM = 0.f;
        for (int k = 0; k < 2 * kReach + 1; ++k) { totC += kc[k] * cp[k]; totM += km[k] * cp[k]; }
        const bool top = i < nb - 1;
        const float eC = req[i] - totC, eM = top ? 0.5f * (req[i] + req[i + 1]) - totM : 0.f;
        float* ap = acc + i - kReach;
        for (int k = 0; k < 2 * kReach + 1; ++k) ap[k] += kc[k] * eC + km[k] * eM;
      }
      for (int j = 0; j < nb; ++j) cut0[j] = std::min(2.f * kMaxCutDb, std::max(0.f, cut0[j] + lsStep * acc[j]));
    }
    std::copy(cut0, cut0 + nb, solved);
    for (int i = 0; i < nb; ++i) {
      for (int c = 0; c < nCh; ++c) {
        Bell& b = bell[c][i];
        if (std::fabs(b.ic1) < 1.0e-20f) b.ic1 = 0.f;          // flush decaying state before it goes denormal
        if (std::fabs(b.ic2) < 1.0e-20f) b.ic2 = 0.f;
        const float cut = cut0[i] * w[c];
        applied[c][i] = cut < 0.003f ? 0.f : cut;
        if (cut < 0.003f) { if (b.on) { b.on = false; b.ic1 = b.ic2 = 0.f; } continue; }
        if (c == 1 && w[0] == w[1]) {
          // Copy identical coefficients, never the independent filter state.
          const Bell& left = bell[0][i];
          b.a1 = left.a1; b.a2 = left.a2; b.a3 = left.a3;
          b.m1 = left.m1; b.gx = left.gx;
        } else {
          const float A = fastExp2(-cut * 0.08304820f);          // 10^(-cut/40)
          const float k = 1.f / (bellQ[i] * A), g = bellG[i];
          b.a1 = 1.f / (1.f + g * (g + k));
          b.a2 = g * b.a1; b.a3 = g * b.a2;
          b.m1 = k * (A * A - 1.f);
          b.gx = 1.f + b.m1 * b.a2;
        }
        b.on = true; anyActive = true;
        activeIndex[c][activeCount[c]++] = i;
      }
    }
  }

  // Audio-thread work is bounded: two lock-free sample stores and one position
  // publication per sample; the cut gain snapshot is copied every 2048 samples.
  void feedDisplay(float track, float side) {
    dispTrack[(size_t)dispPos].store(track, std::memory_order_relaxed);
    dispSc[(size_t)dispPos].store(side, std::memory_order_relaxed);
    dispPos = (dispPos + 1) & (kDispN - 1);
    if (++dispCount >= kDispN / 2) {
      dispCount = 0;
      diagInput.store(meterIn, std::memory_order_relaxed);
      diagOutput.store(meterOut, std::memory_order_relaxed);
      diagOnsetIn.store(meterOnsetIn, std::memory_order_relaxed);
      diagOnsetOut.store(meterOnsetOut, std::memory_order_relaxed);
      for (int c = 0; c < kMaxCh; ++c)
        for (int j = 0; j < kBands; ++j)
          dispApplied[c][j].store(applied[c][j], std::memory_order_relaxed);
    }
    displayPublishedPos.store(dispPos, std::memory_order_release);
  }

  // Per-thread scratch, allocated/refreshed by GUI caller; never the DSP path.
  struct DisplayWorkspace {
    double fs = 0.0;
    FFT fft;
    std::vector<float> win, re, im;
    float fc[kBands] = {}, g[kBands] = {}, q[kBands] = {};
    int lo[kDisp] = {}, hi[kDisp] = {}, nb = 0;
    int firstBand[kDisp] = {}, endBand[kDisp] = {};
    float warpedRatio[kDisp][kBands] = {};
    void prepare(double rate) {
      fft.init(kDispN);
      win.resize(kDispN); re.assign(kDispN, 0.f); im.assign(kDispN, 0.f);
      for (int n = 0; n < kDispN; ++n)
        win[(size_t)n] = 0.5f - 0.5f * (float)std::cos(2.0 * kPi * n / kDispN);
      nb = 0;
      for (int i = 0; i < kBands; ++i) {
        fc[i] = 20.f * std::exp2(((float)i + 0.5f) / (float)kBandsPerOct);
        if (fc[i] >= 0.45f * (float)rate) continue;
        nb = i + 1;
        g[i] = (float)std::tan(kPi * fc[i] / rate);
        const float width = fc[i] * (std::exp2(0.5f / (float)kBandsPerOct)
                                            - std::exp2(-0.5f / (float)kBandsPerOct));
        q[i] = std::min(kBellQ, g[i] / ((float)kPi / (float)rate * (1.f + g[i]*g[i]) * width));
      }
      const double binHz = rate / kDispN;
      for (int i = 0; i < kDisp; ++i) {
        const double t = (double)i / (kDisp - 1), dt = 1.0 / (kDisp - 1);
        const auto freq = [](double x) { return 20.0 * std::pow(1000.0, x); };
        int bottom = (int)std::floor(freq(t - .5 * dt) / binHz);
        int top = (int)std::ceil(freq(t + .5 * dt) / binHz);
        lo[i] = std::min(std::max(bottom, 1), kDispN / 2);
        hi[i] = std::min(std::max(top, lo[i]), kDispN / 2);
        // Same display response geometry, calculated only on sample-rate change.
        const float f = 20.f * std::pow(1000.f, (float)i / (float)(kDisp - 1));
        const float wf = (float)std::tan(kPi * (double)std::min(f, 0.499f * (float)rate) / rate);
        firstBand[i] = nb; endBand[i] = nb;
        for (int j = 0; j < nb; ++j) {
          if (std::fabs(std::log2(f / fc[j])) >= 1.5f) continue;
          firstBand[i] = std::min(firstBand[i], j);
          endBand[i] = j + 1;
          warpedRatio[i][j] = wf / g[j];
        }
      }
      fs = rate;
    }
  };

  double fs = 48000.0;
  SpacerParams params;
  LevelSensitivity sensitivity;
  float smartBlend = 0.f, smartStep = 0.f, deltaMix = 0.f;
  float onsetFastCoef = 0.f, onsetSlowCoef = 0.f, meterCoef = 0.f;
  float onsetFast = 0.f, onsetSlow = 0.f;
  float mainAnaS[kMaxCh][4][kBands] = {}, mainAnaPow[kMaxCh][kBands] = {};
  float meterIn = 0.f, meterOut = 0.f, meterOnsetIn = 0.f, meterOnsetOut = 0.f;
  float prevIn[2] = {}, prevOut[2] = {};
  std::atomic<float> diagOverlap[kBands], diagProtection[kBands];
  std::atomic<float> diagInput{0.f}, diagOutput{0.f}, diagOnsetIn{0.f}, diagOnsetOut{0.f};
  int nb = kBands, step = 16, stepCount = 0;
  float stepSec = 0.f, mix = 0.f, mixStep = 0.002f, peak = 0.f;
  float guardPk = 0.f, guardGain = 1.f, guardDecay = 0.99f, guardRelease = 0.01f;
  float phaseMidEnergy = 0.f, phaseSideEnergy = 0.f, phaseSideBlend = 0.f;
  float phaseEnergyCoef = 0.001f, phaseBlendCoef = 0.001f;
  bool msApplied = false, bandDirty = true, anyActive = false, hadSc = false;
  float fc[kBands] = {}, bandWeight[kBands] = {}, bellG[kBands] = {};
  float kernC[kBands][2 * kReach + 1] = {}, kernM[kBands][2 * kReach + 1] = {}, solved[kBands] = {}, lsStep = 0.f;
  float density[kBands] = {}, bellQ[kBands] = {}, anaK[kBands] = {}, anaA1[kBands] = {}, anaA2[kBands] = {}, anaA3[kBands] = {}, anaPowCoef[kBands] = {};
  float fastPowCoef[kBands] = {}, activePowCoef[kBands] = {}, detectorBlend = 0.f;
  float anaS[4][kBands] = {}, anaPow[kBands] = {};
  float env[kBands] = {}, applied[kMaxCh][kBands] = {};   // applied: the cut (dB) each bell really makes
  Bell bell[kMaxCh][kBands];
  int activeIndex[kMaxCh][kBands] = {}, activeCount[kMaxCh] = {};
  float attackCoef = 0.f, releaseCoef = 0.f, peakDecayCoef = 0.f;
  float spreadWeight[2 * kSpan + 1] = {};
  int spreadRadius = 0;
  Svf scHp[kMaxCh][2], scLp[kMaxCh][2];
  bool scHpOn = false, scLpOn = false;

  std::atomic<float> dispTrack[kDispN], dispSc[kDispN];
  std::atomic<float> dispApplied[kMaxCh][kBands];
  std::atomic<int> displayPublishedPos { 0 };
  std::atomic<double> displayRate { 48000.0 };
  int dispPos = 0, dispCount = 0;
};

} // namespace skell
