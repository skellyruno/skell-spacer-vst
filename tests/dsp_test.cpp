// Standalone DSP tests (no JUCE needed):
//   g++ -O2 -std=c++17 -I Source tests/dsp_test.cpp -o dsp_test && ./dsp_test
#include "SpacerDSP.h"
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace skell;
static const double FS = 48000.0;
static int failures = 0;

static void check(bool ok, const char* name, double v) {
  std::printf("[%s] %-52s %g\n", ok ? "PASS" : "FAIL", name, v);
  if (!ok) ++failures;
}

struct Sig { std::vector<float> l, r; };

static Sig sines(int n, std::vector<std::pair<double, double>> tones) {
  Sig s; s.l.assign(n, 0.f); s.r.assign(n, 0.f);
  for (auto [f, a] : tones)
    for (int i = 0; i < n; ++i) s.l[i] += (float)(a * std::sin(2 * kPi * f * i / FS));
  s.r = s.l;
  return s;
}

// Runs the engine over `in` in blocks; sc==nullptr => no sidechain bus.
static Sig run(SpacerEngine& e, const Sig& in, const Sig* sc, int block) {
  Sig out = in;
  const int n = (int)in.l.size();
  for (int p = 0; p < n; p += block) {
    int b = std::min(block, n - p);
    float* m[2] = { out.l.data() + p, out.r.data() + p };
    const float* s[2] = { nullptr, nullptr };
    if (sc) { s[0] = sc->l.data() + p; s[1] = sc->r.data() + p; }
    e.process(m, s, 2, sc ? 2 : 0, b);
  }
  return out;
}

// amplitude of frequency f in x[start, start+len)
static double amp(const std::vector<float>& x, double f, int start, int len) {
  double re = 0, im = 0;
  for (int i = 0; i < len; ++i) {
    double ph = 2 * kPi * f * (start + i) / FS;
    re += x[start + i] * std::cos(ph); im += x[start + i] * std::sin(ph);
  }
  return 2.0 * std::sqrt(re * re + im * im) / len;
}
static double db(double a) { return 20 * std::log10(std::max(a, 1e-12)); }

int main() {
  const int n = (int)(FS * 4);
  const std::vector<std::pair<double, double>> trackTones = { {500, 0.2}, {1000, 0.2}, {2000, 0.2} };
  Sig track = sines(n, trackTones);
  SpacerParams p;

  { // 1. amount 0 => bit-exact delayed copy
    std::mt19937 rng(1); std::uniform_real_distribution<float> d(-1, 1);
    Sig in; in.l.resize(n); in.r.resize(n);
    for (int i = 0; i < n; ++i) { in.l[i] = d(rng); in.r[i] = d(rng); }
    SpacerEngine e; e.prepare(FS); p = {}; p.amount = 0; e.setParams(p);
    Sig sc = sines(n, { {1000, 0.5} });
    Sig out = run(e, in, &sc, 480);
    int bad = 0;
    for (int i = 0; i + 2048 < n; ++i) if (out.l[i + 2048] != in.l[i] || out.r[i + 2048] != in.r[i]) ++bad;
    check(bad == 0, "amount 0: bit-exact 2048-sample delay (mismatches)", bad);
  }
  { // 2. wet path with no cuts is transparent & correctly aligned (freeze, empty env)
    SpacerEngine e; e.prepare(FS); p = {}; p.amount = 1; p.freeze = true; e.setParams(p);
    Sig out = run(e, track, nullptr, 512);
    double err = 0, ref = 0;
    for (int i = 20000; i + 2048 < n; ++i) { double d = out.l[i + 2048] - track.l[i]; err += d * d; ref += track.l[i] * track.l[i]; }
    double rdb = 10 * std::log10(err / ref + 1e-30);
    check(rdb < -90, "wet path transparent / latency 2048 (error dB)", rdb);
  }
  { // 3. tone in sidechain ducks only that tone
    SpacerEngine e; e.prepare(FS); p = {}; p.amount = 1; e.setParams(p);
    Sig sc = sines(n, { {1000, 0.5} });
    Sig out = run(e, track, &sc, 256);
    double c1k = db(amp(out.l, 1000, n - 24000, 24000) / 0.2);
    double c500 = db(amp(out.l, 500, n - 24000, 24000) / 0.2);
    double c2k = db(amp(out.l, 2000, n - 24000, 24000) / 0.2);
    check(c1k < -20 && c1k > -26, "1 kHz cut ~24 dB at amount 100%", c1k);
    check(std::fabs(c500) < 1.0, "500 Hz untouched (dB)", c500);
    check(std::fabs(c2k) < 1.0, "2 kHz untouched (dB)", c2k);
    // 4. level independence
    SpacerEngine e2; e2.prepare(FS); e2.setParams(p);
    Sig sc2 = sines(n, { {1000, 0.02} });
    Sig out2 = run(e2, track, &sc2, 256);
    double q = db(amp(out2.l, 1000, n - 24000, 24000) / 0.2);
    check(std::fabs(q - c1k) < 1.0, "quiet vs loud sidechain: same cut (dB diff)", q - c1k);
    // 5. amount scales depth
    SpacerEngine e3; e3.prepare(FS); SpacerParams h = p; h.amount = 0.5f; e3.setParams(h);
    Sig out3 = run(e3, track, &sc, 256);
    double half = db(amp(out3.l, 1000, n - 24000, 24000) / 0.2);
    check(half < -10.5 && half > -13.5, "amount 50% => ~12 dB cut", half);
  }
  { // 6. block-size independence (bit-exact)
    Sig sc = sines(n / 2, { {1000, 0.5}, {3000, 0.3} });
    Sig tr = sines(n / 2, trackTones);
    SpacerEngine a; a.prepare(FS); p = {}; p.amount = 0.8f; a.setParams(p);
    Sig ref = run(a, tr, &sc, 512);
    int bad = 0;
    for (int b : { 1, 64, 777, 4096 }) {
      SpacerEngine e; e.prepare(FS); e.setParams(p);
      Sig o = run(e, tr, &sc, b);
      for (size_t i = 0; i < o.l.size(); ++i) if (o.l[i] != ref.l[i]) { ++bad; break; }
    }
    check(bad == 0, "output identical for block sizes 1/64/777/4096", bad);
  }
  { // 7. band limits where cuts happen
    SpacerEngine e; e.prepare(FS); p = {}; p.amount = 1; p.lowHz = 4000; e.setParams(p);
    Sig sc = sines(n, { {1000, 0.5} });
    Sig out = run(e, track, &sc, 256);
    double c = db(amp(out.l, 1000, n - 24000, 24000) / 0.2);
    check(std::fabs(c) < 1.0, "low cut 4 kHz: 1 kHz not cut (dB)", c);
    SpacerEngine e2; e2.prepare(FS); p = {}; p.amount = 1; p.highHz = 600; e2.setParams(p);
    Sig out2 = run(e2, track, &sc, 256);
    double c2 = db(amp(out2.l, 1000, n - 24000, 24000) / 0.2);
    check(std::fabs(c2) < 1.0, "high cut 600 Hz: 1 kHz not cut (dB)", c2);
  }
  { // 8. freeze holds cut after sidechain stops; no freeze releases
    const int half = n / 2;
    Sig sc = sines(n, { {1000, 0.5} });
    for (int i = half; i < n; ++i) sc.l[i] = sc.r[i] = 0.f;
    for (int mode = 0; mode < 2; ++mode) {
      SpacerEngine e; e.prepare(FS); p = {}; p.amount = 1; e.setParams(p);
      Sig out = track;
      for (int pos = 0; pos < n; pos += 256) {
        if (pos == half + 4096) { p.freeze = (mode == 0); e.setParams(p); }
        float* m[2] = { out.l.data() + pos, out.r.data() + pos };
        const float* s[2] = { sc.l.data() + pos, sc.r.data() + pos };
        if (pos == half) { /* freeze engaged just before release below */ }
        e.process(m, s, 2, 2, 256);
      }
      double c = db(amp(out.l, 1000, n - 12000, 12000) / 0.2);
      if (mode == 0) check(c < -15, "freeze: cut held after sidechain stops (dB)", c);
      else check(c > -3, "no freeze: cut released (dB)", c);
    }
  }
  { // 9. stereo: left sidechain ducks left only
    SpacerEngine e; e.prepare(FS); p = {}; p.amount = 1; e.setParams(p);
    Sig sc = sines(n, { {1000, 0.5} }); std::fill(sc.r.begin(), sc.r.end(), 0.f);
    Sig out = run(e, track, &sc, 256);
    double l = db(amp(out.l, 1000, n - 24000, 24000) / 0.2);
    double r = db(amp(out.r, 1000, n - 24000, 24000) / 0.2);
    check(l < -20 && std::fabs(r) < 1.0, "L sidechain cuts L only (L dB / R dB)", l);
  }
  { // 10. no NaN on silence / extreme input
    SpacerEngine e; e.prepare(FS); p = {}; p.amount = 1; p.smooth = 1; e.setParams(p);
    Sig z; z.l.assign(48000, 0.f); z.r = z.l;
    Sig out = run(e, z, &z, 512);
    bool ok = true; for (float v : out.l) if (!std::isfinite(v)) ok = false;
    check(ok, "silence in/out is finite", ok);
  }
  std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
