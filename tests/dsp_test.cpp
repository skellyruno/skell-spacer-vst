// Standalone DSP tests (no JUCE needed):
//   g++ -O2 -std=c++17 -I Source tests/dsp_test.cpp -o dsp_test && ./dsp_test
#include "SpacerDSP.h"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <chrono>

using namespace skell;
static double FS = 48000.0;
static int failures = 0;

static int LAT(double fs) { return SpacerEngine::sizeFor(fs); }

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
    for (int i = 0; i + LAT(FS) < n; ++i) if (out.l[i + LAT(FS)] != in.l[i] || out.r[i + LAT(FS)] != in.r[i]) ++bad;
    check(bad == 0, "amount 0: bit-exact latency-sample delay (mismatches)", bad);
  }
  { // 2. wet path with no cuts is transparent & correctly aligned (freeze, empty env)
    SpacerEngine e; e.prepare(FS); p = {}; p.amount = 1; p.freeze = true; e.setParams(p);
    Sig out = run(e, track, nullptr, 512);
    double err = 0, ref = 0;
    for (int i = 20000; i + LAT(FS) < n; ++i) { double d = out.l[i + LAT(FS)] - track.l[i]; err += d * d; ref += track.l[i] * track.l[i]; }
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
  { // 9b. stereo link: L sidechain ducks both sides; partial link ducks R about half way
    Sig sc = sines(n, { {1000, 0.5} }); std::fill(sc.r.begin(), sc.r.end(), 0.f);
    double rdb[2];
    int idx = 0;
    for (float lk : { 1.0f, 0.5f }) {
      SpacerEngine e; e.prepare(FS); p = {}; p.amount = 1; p.link = lk; e.setParams(p);
      Sig out = run(e, track, &sc, 256);
      double l = db(amp(out.l, 1000, n - 24000, 24000) / 0.2);
      rdb[idx++] = db(amp(out.r, 1000, n - 24000, 24000) / 0.2);
      if (lk == 1.0f) check(l < -20 && rdb[0] < -20, "link 100%: both sides cut (L dB)", l);
    }
    check(rdb[1] < -8 && rdb[1] > rdb[0] + 4, "link 50%: R cut partway (R dB)", rdb[1]);
  }
  { // 10. no NaN on silence / extreme input
    SpacerEngine e; e.prepare(FS); p = {}; p.amount = 1; p.smooth = 1; e.setParams(p);
    Sig z; z.l.assign(48000, 0.f); z.r = z.l;
    Sig out = run(e, z, &z, 512);
    bool ok = true; for (float v : out.l) if (!std::isfinite(v)) ok = false;
    check(ok, "silence in/out is finite", ok);
  }
  { // 11. sample rates: exact delay, correct cut at each rate
    for (double rate : { 44100.0, 88200.0, 96000.0, 192000.0 }) {
      FS = rate;
      const int m = (int)(rate * 3);
      Sig tr = sines(m, trackTones), sc = sines(m, { {1000, 0.5} });
      SpacerEngine e; e.prepare(rate); p = {}; p.amount = 1; e.setParams(p);
      Sig out = run(e, tr, &sc, 300);
      const int w = (int)(rate / 2);
      char name[96]; std::snprintf(name, sizeof name, "%.0f Hz: 1 kHz cut ~24 dB, 500 Hz untouched (dB)", rate);
      double c = db(amp(out.l, 1000, m - w, w) / 0.2), c5 = db(amp(out.l, 500, m - w, w) / 0.2);
      check(c < -20 && c > -26 && std::fabs(c5) < 1.0, name, c);
      SpacerEngine e0; e0.prepare(rate); p.amount = 0; e0.setParams(p);
      Sig o0 = run(e0, tr, &sc, 300);
      int bad = 0; for (int i = 0; i + LAT(rate) < m; ++i) if (o0.l[i + LAT(rate)] != tr.l[i]) ++bad;
      std::snprintf(name, sizeof name, "%.0f Hz: bypass is a bit-exact latency-sample delay (mismatches)", rate);
      check(bad == 0, name, bad);
    }
    FS = 48000.0;
  }
  { // 12. mono track with stereo sidechain; mono sidechain on stereo track
    SpacerEngine e; e.prepare(FS); p = {}; p.amount = 1; e.setParams(p);
    Sig sc = sines(n, { {1000, 0.5} });
    Sig out = track;
    for (int pos = 0; pos < n; pos += 256) {
      float* m[1] = { out.l.data() + pos };
      const float* s[2] = { sc.l.data() + pos, sc.r.data() + pos };
      e.process(m, s, 1, 2, 256);
    }
    check(db(amp(out.l, 1000, n - 24000, 24000) / 0.2) < -20, "mono track + stereo sidechain: 1 kHz cut (dB)", db(amp(out.l, 1000, n - 24000, 24000) / 0.2));
    SpacerEngine e2; e2.prepare(FS); e2.setParams(p);
    Sig out2 = track;
    for (int pos = 0; pos < n; pos += 256) {
      float* m[2] = { out2.l.data() + pos, out2.r.data() + pos };
      const float* s[1] = { sc.l.data() + pos };
      e2.process(m, s, 2, 1, 256);
    }
    double l = db(amp(out2.l, 1000, n - 24000, 24000) / 0.2), r = db(amp(out2.r, 1000, n - 24000, 24000) / 0.2);
    check(l < -20 && r < -20, "mono sidechain drives both channels (L dB)", l);
  }
  { // 13. impulse: appears 2048 samples later at unit gain when nothing is cut
    SpacerEngine e; e.prepare(FS); p = {}; p.amount = 1; p.freeze = true; e.setParams(p);
    Sig in; in.l.assign(20000, 0.f); in.r = in.l; in.l[5000] = 1.f;
    Sig out = run(e, in, nullptr, 512);
    int peakAt = (int)(std::max_element(out.l.begin(), out.l.end(), [](float a, float b) { return std::fabs(a) < std::fabs(b); }) - out.l.begin());
    check(peakAt == 5000 + LAT(FS) && std::fabs(out.l[peakAt] - 1.0f) < 1e-3f, "impulse returns at +latency samples, unit gain (peak)", out.l[peakAt]);
  }
  { // 14. no clicks: abrupt amount / power / sidechain-connect changes stay smooth
    Sig sc = sines(n, { {1000, 0.5} });
    Sig tr = sines(n, { {1000, 0.5} });
    double base = 0; for (int i = 1; i < n; ++i) base = std::max(base, (double)std::fabs(tr.l[i] - tr.l[i - 1]));
    for (int mode = 0; mode < 3; ++mode) {
      SpacerEngine e; e.prepare(FS); p = {}; p.amount = 1; e.setParams(p);
      Sig out = tr; int blk = 0;
      for (int pos = 0; pos + 256 <= n; pos += 256, ++blk) {
        const bool flip = (blk / 19) % 2 == 1;
        SpacerParams q = p;
        if (mode == 0) q.amount = flip ? 0.f : 1.f;
        if (mode == 1) q.power = !flip;
        e.setParams(q);
        float* m[2] = { out.l.data() + pos, out.r.data() + pos };
        const float* s[2] = { sc.l.data() + pos, sc.r.data() + pos };
        e.process(m, s, 2, (mode == 2 && flip) ? 0 : 2, 256);
      }
      double mx = 0; for (int i = LAT(FS) + 1; i < n; ++i) mx = std::max(mx, (double)std::fabs(out.l[i] - out.l[i - 1]));
      const char* nm[] = { "no click on abrupt Amount 0<->100% (max step / sine step)", "no click on Power toggling (max step / sine step)", "no click when sidechain connects/disconnects (max step / sine step)" };
      check(mx < base * 1.5, nm[mode], mx / base);
    }
  }
  { // 15. loud noise with a bursting sidechain (fast-changing cuts): finite, no overshoot, no runaway gain
    std::mt19937 rng(5); std::uniform_real_distribution<float> d(-1, 1);
    const int m = (int)(FS * 20);
    Sig in; in.l.resize(m); in.r.resize(m); Sig sc = in;
    for (int i = 0; i < m; ++i) { in.l[i] = 0.9f * d(rng); in.r[i] = 0.9f * d(rng); const float burst = ((i / 12000) % 2) ? 1.f : 0.f; sc.l[i] = burst * d(rng); sc.r[i] = burst * d(rng); }
    SpacerEngine e; e.prepare(FS); p = {}; p.amount = 1; p.smooth = 0.5f; p.link = 0.5f; e.setParams(p);
    Sig out = run(e, in, &sc, 480);
    float pk = 0; bool fin = true; double eo = 0, ei = 0;
    for (int i = 0; i < m; ++i) { pk = std::max({ pk, std::fabs(out.l[i]), std::fabs(out.r[i]) }); fin = fin && std::isfinite(out.l[i]) && std::isfinite(out.r[i]); eo += out.l[i] * out.l[i]; ei += in.l[i] * in.l[i]; }
    check(fin, "20 s of loud noise + bursting sidechain stays finite", fin);
    check(pk < 0.9f * 1.1f, "bursty sidechain: output peak within 10% of 0.9 input peak", pk);
    check(eo < ei * 1.05, "never louder overall than the input (energy ratio)", eo / ei);
  }
  { // 16. CPU: stereo, every bin cutting, smooth on, 48 kHz
    const int m = (int)(FS * 10);
    Sig tr = sines(m, trackTones), sc = sines(m, { {1000, 0.5}, {3000, 0.3} });
    SpacerEngine e; e.prepare(FS); p = {}; p.amount = 1; p.smooth = 1; p.link = 1; e.setParams(p);
    auto t0 = std::chrono::steady_clock::now();
    Sig out = run(e, tr, &sc, 512);
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    check(sec / 10.0 < 0.03, "CPU: fraction of one core (10 s of stereo audio)", sec / 10.0);
  }
  { // 17. denormals: tail after loud sidechain has stopped must not slow the audio thread
    const int m = (int)(FS * 40);
    Sig tr = sines(m, trackTones), sc = sines(m, { {1000, 0.5} });
    std::fill(sc.l.begin() + (int)(FS * 2), sc.l.end(), 0.f); std::fill(sc.r.begin() + (int)(FS * 2), sc.r.end(), 0.f);
    SpacerEngine e; e.prepare(FS); p = {}; p.amount = 1; e.setParams(p);
    Sig out = run(e, tr, &sc, 512);          // first 2 s loud, then 38 s decaying toward zero
    SpacerEngine ref; ref.prepare(FS); ref.setParams(p);
    Sig sc2 = sines(m, { {1000, 0.5} });
    auto t0 = std::chrono::steady_clock::now(); run(ref, tr, &sc2, 512);
    double tLoud = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    auto t1 = std::chrono::steady_clock::now(); { SpacerEngine e2; e2.prepare(FS); e2.setParams(p); run(e2, tr, &sc, 512); }
    double tTail = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
    check(tTail < tLoud * 1.3, "decaying tail costs no more than steady cutting (time ratio)", tTail / tLoud);
  }
  std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
