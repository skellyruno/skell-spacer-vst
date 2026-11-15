// Skell Spacer zero-latency (Live) engine contract; no JUCE, DAW or audio hardware.
#include "../Source/ZeroLatencyEngine.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>

using skell::SpacerParams;
using skell::ZeroLatencyEngine;
constexpr double pi = 3.14159265358979323846;

static int failures = 0;
static void check(bool ok, const char* what, double v = 0) {
  std::printf("[%s] %s (%.3f)\n", ok ? "PASS" : "FAIL", what, v);
  if (!ok) ++failures;
}

struct Sig { std::vector<float> l, r; };
static Sig sine(int n, double fs, double f, float a, float lg = 1.f, float rg = 1.f) {
  Sig s; s.l.resize((size_t)n); s.r.resize((size_t)n);
  for (int i = 0; i < n; ++i) { float v = a * (float)std::sin(2 * pi * f * i / fs); s.l[(size_t)i] = v * lg; s.r[(size_t)i] = v * rg; }
  return s;
}
static void run(ZeroLatencyEngine& e, Sig& in, const Sig* sc, int block, int nCh = 2) {
  const int n = (int)in.l.size();
  for (int p = 0; p < n; p += block) {
    const int m = std::min(block, n - p);
    float* x[2] = { in.l.data() + p, in.r.data() + p };
    const float* s[2] = { sc ? sc->l.data() + p : nullptr, sc ? sc->r.data() + p : nullptr };
    e.process(x, s, nCh, sc ? 2 : 0, m);
  }
}
static double amp(const std::vector<float>& v, double fs, double f) {
  double re = 0, im = 0; const int n = (int)v.size();
  for (int i = n / 2; i < n; ++i) { re += v[(size_t)i] * std::cos(2 * pi * f * i / fs); im += v[(size_t)i] * std::sin(2 * pi * f * i / fs); }
  return 2.0 * std::hypot(re, im) / (n - n / 2);
}
static double db(double a, double b) { return 20.0 * std::log10(std::max(a, 1e-9) / std::max(b, 1e-9)); }
static SpacerParams full() { SpacerParams p; p.amount = 1.f; p.smooth = 0.3f; p.attackMs = 5.f; p.releaseMs = 100.f; return p; }

// cut (dB, positive = quieter) applied to a track tone by a sidechain tone
static double cutOf(double fs, double trackHz, double scHz, SpacerParams p, bool chR = false) {
  ZeroLatencyEngine e; e.prepare(fs); e.setParams(p);
  const int n = (int)fs; auto in = sine(n, fs, trackHz, 0.2f); auto sc = sine(n, fs, scHz, 0.4f);
  run(e, in, &sc, 480);
  return -db(amp(chR ? in.r : in.l, fs, trackHz), 0.2);
}

int main() {
  for (double fs : { 44100., 48000., 96000., 192000. }) {
    ZeroLatencyEngine e; e.prepare(fs);
    check(e.latencySamples() == 0, "reports zero latency", fs);
    float imp[1] = { 1.f }; float* x[1] = { imp };
    e.process(x, nullptr, 1, 0, 1);
    check(imp[0] == 1.f, "dry impulse is immediate", imp[0]);
  }

  const double fs = 48000;
  { // idle = bit-exact
    ZeroLatencyEngine e; e.prepare(fs); auto p = full(); e.setParams(p);
    auto in = sine(48000, fs, 700, 0.3f), ref = in;
    run(e, in, nullptr, 256);
    check(in.l == ref.l && in.r == ref.r, "no sidechain: output bit-identical to input");
    p.amount = 0.f; e.setParams(p); auto sc = sine(48000, fs, 700, 0.5f); run(e, in, &sc, 256);
    check(in.l == ref.l, "amount 0: bit-identical");
    p.amount = 1.f; p.power = false; e.setParams(p); run(e, in, &sc, 256);
    check(in.l == ref.l, "power off: bit-identical");
  }
  { // block size independence
    auto p = full(); ZeroLatencyEngine a, b; a.prepare(fs); b.prepare(fs); a.setParams(p); b.setParams(p);
    auto x1 = sine(24000, fs, 1000, 0.2f), x2 = x1, sc = sine(24000, fs, 1000, 0.4f);
    run(a, x1, &sc, 1); run(b, x2, &sc, 377);
    check(x1.l == x2.l && x1.r == x2.r, "block size 1 and 377 give identical output");
  }

  auto p = full();
  const double same = cutOf(fs, 1000, 1000, p);
  check(same > 8.0 && same < 26.0, "same-frequency sidechain ducks the track", same);
  const double lowOff = cutOf(fs, 100, 1000, p), hiOff = cutOf(fs, 8000, 1000, p);
  check(lowOff < 1.5, "1 kHz sidechain barely touches 100 Hz", lowOff);
  check(hiOff < 1.5, "1 kHz sidechain barely touches 8 kHz", hiOff);
  check(cutOf(fs, 1000, 1000, [&] { auto q = p; q.amount = 0.5f; return q; }()) < same * 0.75, "amount scales the depth");
  for (double f : { 44100., 96000., 192000. }) {
    const double c = cutOf(f, 1000, 1000, p);
    check(c > 8.0 && c < 26.0, "ducking works at other sample rates", c);
    check(cutOf(f, 100, 1000, p) < 1.5, "selectivity holds at other sample rates", f);
  }
  { auto q = p; q.lowHz = 2000.f; check(cutOf(fs, 1000, 1000, q) < 1.0, "low cut protects bands below it", cutOf(fs, 1000, 1000, q)); }
  { auto q = p; q.highHz = 500.f; check(cutOf(fs, 1000, 1000, q) < 1.0, "high cut protects bands above it", cutOf(fs, 1000, 1000, q)); }

  { // attack / release timing
    ZeroLatencyEngine e; e.prepare(fs); auto q = p; q.attackMs = 5.f; q.releaseMs = 400.f; q.smooth = 0.f; e.setParams(q);
    const int n = 48000; auto in = sine(n, fs, 1000, 0.2f); auto sc = sine(n, fs, 1000, 0.f);
    for (int i = 24000; i < n; ++i) sc.l[(size_t)i] = sc.r[(size_t)i] = 0.4f * (float)std::sin(2 * pi * 1000 * i / fs);
    run(e, in, &sc, 480);
    auto win = [&](int a, int b) { std::vector<float> v(in.l.begin() + a, in.l.begin() + b); return -db(amp(v, fs, 1000), 0.2); };
    check(win(23000, 24000) < 1.0, "quiet before the sidechain starts", win(23000, 24000));
    check(win(27000, 30000) > 5.0, "ducks within ~60 ms of the sidechain", win(27000, 30000));
  }

  { // the sidechain is summed to mono: a one-sided sidechain ducks both sides equally
    auto in = sine(48000, fs, 1000, 0.2f);
    for (int side = 0; side < 2; ++side) {
      auto sc = sine(48000, fs, 1000, 0.4f, side ? 0.f : 1.f, side ? 1.f : 0.f);
      ZeroLatencyEngine e; e.prepare(fs); e.setParams(p); auto a = in; run(e, a, &sc, 480);
      const double cl = -db(amp(a.l, fs, 1000), 0.2), cr = -db(amp(a.r, fs, 1000), 0.2);
      check(cl > 8.0 && std::fabs(cl - cr) < 0.3, side ? "R-only sidechain ducks both sides equally" : "L-only sidechain ducks both sides equally", cl - cr);
    }
  }
  { // pan in L/R mode
    auto in = sine(48000, fs, 1000, 0.2f), sc = sine(48000, fs, 1000, 0.4f);
    for (float pan : { -1.f, 0.f, 1.f }) {
      ZeroLatencyEngine e; e.prepare(fs); auto q = p; q.pan = pan; e.setParams(q); auto a = in; run(e, a, &sc, 480);
      const double cl = -db(amp(a.l, fs, 1000), 0.2), cr = -db(amp(a.r, fs, 1000), 0.2);
      if (pan < 0) check(cl > 8.0 && cr < 0.3, "pan left: only left is ducked", cl);
      if (pan > 0) check(cr > 8.0 && cl < 0.3, "pan right: only right is ducked", cr);
      if (pan == 0) check(cl > 8.0 && std::fabs(cl - cr) < 0.3, "pan centre: both sides ducked", cl);
    }
  }
  { // M/S mode: pan left = mid only, pan right = side only
    auto sc = sine(48000, fs, 1000, 0.4f);
    auto midTrack = sine(48000, fs, 1000, 0.2f, 1.f, 1.f), sideTrack = sine(48000, fs, 1000, 0.2f, 1.f, -1.f);
    for (float pan : { -1.f, 1.f }) {
      auto q = p; q.ms = true; q.pan = pan;
      ZeroLatencyEngine e1; e1.prepare(fs); e1.setParams(q); auto m = midTrack; run(e1, m, &sc, 480);
      ZeroLatencyEngine e2; e2.prepare(fs); e2.setParams(q); auto s = sideTrack; run(e2, s, &sc, 480);
      const double cm = -db(amp(m.l, fs, 1000), 0.2), cs = -db(amp(s.l, fs, 1000), 0.2);
      if (pan < 0) check(cm > 8.0 && cs < 0.3, "M/S pan mid: mid ducked, side untouched", cm);
      else check(cs > 8.0 && cm < 0.3, "M/S pan side: side ducked, mid untouched", cs);
    }
    auto q = p; q.ms = true; ZeroLatencyEngine e; e.prepare(fs); e.setParams(q);
    auto lOnly = sine(48000, fs, 1000, 0.2f, 1.f, 0.f); run(e, lOnly, &sc, 480);
    const double ml = -db(amp(lOnly.l, fs, 1000), 0.2), mr = amp(lOnly.r, fs, 1000);
    check(ml > 3.0 && mr < 1e-3, "M/S centre: left-only material stays left-only", ml);
  }

  { // switching L/R <-> M/S while ducking: no click (centred material is cut the same either way)
    auto in = sine(96000, fs, 1000, 0.2f), sc = sine(96000, fs, 1000, 0.4f);
    ZeroLatencyEngine a, b; a.prepare(fs); b.prepare(fs); auto q = p; a.setParams(q); b.setParams(q);
    auto ref = in, tog = in; run(a, ref, &sc, 256);
    for (int blk = 0; blk < 96000 / 256; ++blk) {
      if (blk % 40 == 0) { q.ms = !q.ms; b.setParams(q); }
      float* x[2] = { tog.l.data() + blk * 256, tog.r.data() + blk * 256 };
      const float* s2[2] = { sc.l.data() + blk * 256, sc.r.data() + blk * 256 };
      b.process(x, s2, 2, 2, 256);
    }
    double worst = 0, jump = 0;
    for (size_t i = 1; i < tog.l.size(); ++i) { jump = std::max(jump, (double)std::fabs(tog.l[i] - tog.l[i - 1])); }
    for (size_t i = 24000; i < tog.l.size(); ++i) worst = std::max(worst, (double)std::fabs(tog.l[i]) - 0.2 * 1.02);   // never louder than the dry input
    check(jump < 0.2 * 2 * pi * 1000 / fs * 1.5 + 0.02 && worst <= 0.0, "M/S toggling is click-free (max step / overshoot)", jump);
  }
  { // the displayed cut is the cut that is actually applied
    for (float pan : { 0.f, -1.f }) {
      ZeroLatencyEngine e; e.prepare(fs); auto q = p; q.pan = pan; e.setParams(q);
      auto in = sine(48000, fs, 1000, 0.2f), sc = sine(48000, fs, 1000, 0.4f); run(e, in, &sc, 480);
      float m[ZeroLatencyEngine::kDisp], d[ZeroLatencyEngine::kDisp], c[ZeroLatencyEngine::kDisp]; e.getDisplay(m, d, c);
      const int idx = (int)std::lround((ZeroLatencyEngine::kDisp - 1) * std::log(1000.0 / 20.0) / std::log(1000.0));
      const double real = -db(amp(in.l, fs, 1000), 0.2);
      check(std::fabs(c[idx] - real) < 1.5, "display cut at 1 kHz matches the applied cut (dB)", c[idx] - real);
    }
  }
  { // the output never rises above the input's peak (full-scale noise, every mode)
    const size_t n = 48000 * 10; std::mt19937 rg(1); std::uniform_real_distribution<float> d(-1, 1);
    Sig base, sc; base.l.resize(n); base.r.resize(n); sc.l.resize(n); sc.r.resize(n);
    for (size_t i = 0; i < n; ++i) { base.l[i] = 0.9f * d(rg); base.r[i] = 0.9f * d(rg); sc.l[i] = sc.r[i] = ((i / 12000) % 2) ? d(rg) : 0.f; }
    float inPk = 0.f; for (size_t i = 0; i < n; ++i) inPk = std::max({ inPk, std::fabs(base.l[i]), std::fabs(base.r[i]) });
    float worst = 0.f;
    for (int ms = 0; ms < 2; ++ms) for (float pan : { 0.f, 0.5f, -1.f }) {
      ZeroLatencyEngine e; e.prepare(fs); auto q = p; q.ms = ms != 0; q.pan = pan; q.smooth = 0.3f; e.setParams(q);
      auto o = base; run(e, o, &sc, 256);
      for (size_t i = 0; i < n; ++i) worst = std::max({ worst, std::fabs(o.l[i]), std::fabs(o.r[i]) });
    }
    check(worst <= inPk * 1.00001f, "output peak never exceeds the input peak (ratio)", worst / inPk);
  }
  { // depth and sharpness: full depth on the key's own frequency, almost none half an octave away
    const double own = cutOf(fs, 1000, 1000, p), quarter = cutOf(fs, 1000, 1260, p), half = cutOf(fs, 1000, 1414, p), oct = cutOf(fs, 1000, 2000, p);
    check(own > 22.0 && own < 26.0, "tone is cut close to the full 24 dB (dB)", own);
    check(quarter < own - 2.0 && half < 16.0 && oct < 3.0, "cut falls off away from the key (1/4 oct, 1/2 oct, 1 oct dB)", half);
  }
  { // the whole audible range is covered: tones all the way up, and a broadband key on a broadband track
    for (double f : { 8000., 12000., 15000., 18000. }) check(cutOf(fs, f, f, p) > 12.0, "same-frequency tone is ducked up high (dB)", cutOf(fs, f, f, p));
    ZeroLatencyEngine e; e.prepare(fs); e.setParams(p);
    const size_t n = 96000; std::mt19937 rg(1); std::uniform_real_distribution<float> d(-1, 1);
    Sig a, k; a.l.resize(n); a.r.resize(n); k.l.resize(n); k.r.resize(n);
    for (size_t i = 0; i < n; ++i) { a.l[i] = a.r[i] = 0.2f * d(rg); k.l[i] = k.r[i] = 0.5f * d(rg); }
    const Sig ref = a; run(e, a, &k, 256);
    double pOut = 0, pIn = 0;                                  // power from 40 Hz to 20 kHz, weighted per Hz
    for (int j = 0; j < 120; ++j) {
      const double f = 40.0 * std::pow(500.0, (j + 0.5) / 120.0);
      double ro = 0, io = 0, rr = 0, ir = 0;
      for (size_t i = 60000; i < 90000; ++i) {
        const double c = std::cos(2 * pi * f * (double)i / fs), sn = std::sin(2 * pi * f * (double)i / fs);
        ro += a.l[i] * c; io += a.l[i] * sn; rr += ref.l[i] * c; ir += ref.l[i] * sn;
      }
      pOut += f * (ro * ro + io * io); pIn += f * (rr * rr + ir * ir);
    }
    check(-10 * std::log10(pOut / pIn) > 18.0, "white-noise key cuts white-noise track across 40 Hz-20 kHz (dB)", -10 * std::log10(pOut / pIn));
  }
  { // robustness: NaN / inf / huge input and sidechain, odd params
    ZeroLatencyEngine e; e.prepare(fs); e.setParams(full());
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    auto in = sine(48000, fs, 300, 0.3f), sc = sine(48000, fs, 300, 0.5f);
    for (int i = 100; i < 200; ++i) { in.l[(size_t)i] = nan; sc.l[(size_t)i] = inf; in.r[(size_t)i] = -inf; sc.r[(size_t)i] = 1e30f; }
    run(e, in, &sc, 300);
    bool ok = true; for (float v : in.l) ok = ok && std::isfinite(v); for (float v : in.r) ok = ok && std::isfinite(v);
    check(ok, "non-finite input never reaches the output");
    SpacerParams bad; bad.amount = nan; bad.pan = inf; bad.lowHz = nan; bad.attackMs = -5.f; e.setParams(bad);
    auto in2 = sine(4800, fs, 300, 0.3f); run(e, in2, &sc, 300);
    ok = true; for (float v : in2.l) ok = ok && std::isfinite(v);
    check(ok, "garbage parameters are sanitised");
    // after the abuse, it still works
    ZeroLatencyEngine e3; e3.prepare(fs); e3.setParams(full());
    check(cutOf(fs, 1000, 1000, full()) > 8.0, "engine unaffected by earlier abuse");
  }
  { // no denormals after silence, and a mono track with a stereo sidechain
    ZeroLatencyEngine e; e.prepare(fs); e.setParams(full());
    auto in = sine(48000, fs, 1000, 0.2f), sc = sine(48000, fs, 1000, 0.4f); run(e, in, &sc, 480);
    Sig z; z.l.assign(96000, 0.f); z.r = z.l; Sig zs = z; run(e, z, &zs, 480);
    bool tiny = false; for (float v : z.l) if (v != 0.f && std::fabs(v) < 1e-30f) tiny = true;
    check(!tiny, "no denormal output after the sidechain stops");
    ZeroLatencyEngine m; m.prepare(fs); m.setParams(full()); auto mono = sine(48000, fs, 1000, 0.2f);
    float* x[1] = { mono.l.data() }; const float* s[2] = { sc.l.data(), sc.r.data() };
    for (int p0 = 0; p0 < 48000; p0 += 480) { float* xp[1] = { x[0] + p0 }; const float* sp[2] = { s[0] + p0, s[1] + p0 }; m.process(xp, sp, 1, 2, 480); }
    check(-db(amp(mono.l, fs, 1000), 0.2) > 8.0, "mono track with stereo sidechain ducks");
  }
  { // CPU: 10 s of stereo audio at 48 kHz, worst case (full amount, separate detectors)
    ZeroLatencyEngine e; e.prepare(fs); auto q = full(); q.smooth = 1.f; e.setParams(q);
    Sig in; in.l.resize(480000); in.r.resize(480000); Sig sc = in;
    uint32_t r = 1; for (size_t i = 0; i < in.l.size(); ++i) { r = r * 1664525u + 1013904223u; in.l[i] = in.r[i] = (float)((r >> 8) & 0xffff) / 65536.f - .5f; r = r * 1664525u + 1013904223u; sc.l[i] = sc.r[i] = ((float)((r >> 8) & 0xffff) / 65536.f - .5f) * (i % 2 ? 1.f : .3f); }
    const auto t0 = std::chrono::steady_clock::now(); run(e, in, &sc, 512); const auto t1 = std::chrono::steady_clock::now();
    const double load = std::chrono::duration<double>(t1 - t0).count() / 10.0 * 100.0;
    check(load < 20.0, "CPU load for stereo at 48 kHz (% of one core, worst case)", load);
  }

  { // sidechain filters
    const int n = (int)fs;
    auto heard = [&](SpacerParams q, double hz) {            // dB of the sidechain tone as the detector hears it
      ZeroLatencyEngine e; e.prepare(fs); e.setParams(q);
      auto in = sine(n, fs, 440, 0.1f); auto sc = sine(n, fs, hz, 0.4f);
      Sig out = sc; float* o[2] = { out.l.data(), out.r.data() };
      for (int pos = 0; pos < n; pos += 480) {
        const int m = std::min(480, n - pos);
        float* x[2] = { in.l.data() + pos, in.r.data() + pos };
        const float* s[2] = { sc.l.data() + pos, sc.r.data() + pos };
        float* so[2] = { o[0] + pos, o[1] + pos };
        e.process(x, s, 2, 2, m, so);
      }
      return db(amp(out.l, fs, hz), 0.4);
    };
    SpacerParams off = full();
    check(std::fabs(heard(off, 1000)) < 0.001, "sidechain filters off: monitor is the input", heard(off, 1000));
    SpacerParams hp = full(); hp.scLowHz = 1000.f;
    check(heard(hp, 100) < -35.0, "sidechain HP at 1 kHz removes 100 Hz (24 dB/oct)", heard(hp, 100));
    check(std::fabs(heard(hp, 1000) + 3.0) < 0.6, "sidechain HP is -3 dB at its corner", heard(hp, 1000));
    check(std::fabs(heard(hp, 8000)) < 0.3, "sidechain HP passes 8 kHz", heard(hp, 8000));
    SpacerParams lp = full(); lp.scHighHz = 1000.f;
    check(heard(lp, 8000) < -35.0, "sidechain LP at 1 kHz removes 8 kHz (24 dB/oct)", heard(lp, 8000));
    check(std::fabs(heard(lp, 100)) < 0.3, "sidechain LP passes 100 Hz", heard(lp, 100));
    SpacerParams bad = full(); bad.scLowHz = 15000.f; bad.scHighHz = 100.f;
    check(std::isfinite(heard(bad, 16500)) && heard(bad, 16500) > -8.0, "crossed sidechain filter limits keep a pass-band open", heard(bad, 16500));
    // a loud rumble would hide a quiet 1 kHz key: filtering it out lets the key duck at full depth
    auto mix = [&](SpacerParams q) {
      ZeroLatencyEngine e; e.prepare(fs); e.setParams(q);
      auto in = sine(n, fs, 1000, 0.2f); auto lo = sine(n, fs, 80, 0.8f), hi = sine(n, fs, 1000, 0.04f);
      for (size_t i = 0; i < lo.l.size(); ++i) { lo.l[i] += hi.l[i]; lo.r[i] += hi.r[i]; }
      run(e, in, &lo, 480);
      return -db(amp(in.l, fs, 1000), 0.2);
    };
    SpacerParams q1 = full(), q2 = full(); q2.scLowHz = 300.f;
    check(mix(q2) > mix(q1) + 3.0 && mix(q2) > 18.0, "sidechain HP stops rumble from masking the key", mix(q2));
  }
  std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
