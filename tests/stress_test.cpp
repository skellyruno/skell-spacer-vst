// Extreme-conditions test for both engines (no JUCE): odd sample rates and block sizes, parameter fuzzing,
// hostile signals, channel layouts, re-prepare/reset mid-stream, long runs and CPU.
#include "../Source/ZeroLatencyEngine.h"
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <random>

using namespace skell;
static int failures = 0;
static void check(bool ok, const std::string& what, double v = 0) {
  std::printf("[%s] %s (%.3f)\n", ok ? "PASS" : "FAIL", what.c_str(), v);
  if (!ok) ++failures;
}
constexpr double pi = 3.14159265358979323846;

// Uniform interface over both engines.
struct Eng {
  virtual ~Eng() = default;
  virtual void prepare(double fs) = 0;
  virtual void reset() = 0;
  virtual void setParams(const SpacerParams&) = 0;
  virtual void process(float* const*, const float* const*, int, int, int) = 0;
  virtual int latency() const = 0;
  virtual float maxCut() const = 0;     // deepest cut on the display curve (dB), NaN-propagating
};
template <class E> struct Wrap : Eng {
  E e;
  void prepare(double fs) override { e.prepare(fs); }
  void reset() override { e.reset(); }
  void setParams(const SpacerParams& p) override { e.setParams(p); }
  void process(float* const* m, const float* const* s, int c, int n, int f) override { e.process(m, s, c, n, f); }
  int latency() const override { return e.latencySamples(); }
  float maxCut() const override {
    float m[E::kDisp], d[E::kDisp], c[E::kDisp]; e.getDisplay(m, d, c);
    float worst = 0.f; for (float v : c) { if (!std::isfinite(v)) return std::numeric_limits<float>::quiet_NaN(); worst = std::max(worst, v); }
    return worst;
  }
};
using Maker = std::function<std::unique_ptr<Eng>()>;

struct Buf { std::vector<float> c[2]; };
static Buf noise(size_t n, float amp, uint32_t seed) {
  std::mt19937 r(seed); std::uniform_real_distribution<float> d(-1, 1); Buf b;
  for (auto& c : b.c) { c.resize(n); for (auto& v : c) v = amp * d(r); }
  return b;
}
static bool finite(const Buf& b, int ch = 2) { for (int c = 0; c < ch; ++c) for (float v : b.c[c]) if (!std::isfinite(v)) return false; return true; }
static float peak(const Buf& b, int ch = 2) { float p = 0; for (int c = 0; c < ch; ++c) for (float v : b.c[c]) p = std::max(p, std::fabs(v)); return p; }
static bool hasDenormal(const Buf& b) { for (auto& c : b.c) for (float v : c) if (v != 0.f && std::fabs(v) < 1.17549435e-38f) return true; return false; }

static void run(Eng& e, Buf& in, const Buf* sc, int nCh, int nSc, const std::function<int()>& block) {
  const size_t n = in.c[0].size();
  for (size_t p = 0; p < n;) {
    const int m = (int)std::min<size_t>((size_t)block(), n - p);
    float* x[2] = { in.c[0].data() + p, in.c[1].data() + p };
    const float* s[2] = { sc ? sc->c[0].data() + p : nullptr, sc ? sc->c[1].data() + p : nullptr };
    e.process(x, s, nCh, nSc, m);
    p += (size_t)m;
  }
}
static void runFixed(Eng& e, Buf& in, const Buf* sc, int nCh, int nSc, int block) { run(e, in, sc, nCh, nSc, [=] { return block; }); }

static SpacerParams randomParams(std::mt19937& r, bool wild) {
  std::uniform_real_distribution<float> u(0, 1);
  SpacerParams p;
  p.amount = u(r); p.smooth = u(r); p.pan = u(r) * 2 - 1;
  p.attackMs = 1 + 199 * u(r); p.releaseMs = 5 + 1995 * u(r);
  p.lowHz = 20 * std::pow(1000.f, u(r)); p.highHz = 20 * std::pow(1000.f, u(r));
  p.scLowHz = 20 * std::pow(1000.f, u(r)); p.scHighHz = 20 * std::pow(1000.f, u(r));
  p.freeze = u(r) < 0.2f; p.power = u(r) < 0.8f; p.ms = u(r) < 0.5f;
  if (wild && u(r) < 0.3f) {            // out-of-range / hostile values a host should never send, but might
    const float junk[] = { -5.f, 5.f, 1e9f, -1e9f, 0.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
    float* fields[] = { &p.amount, &p.smooth, &p.pan, &p.attackMs, &p.releaseMs, &p.lowHz, &p.highHz, &p.scLowHz, &p.scHighHz };
    *fields[r() % 9] = junk[r() % 8];
  }
  return p;
}

static void suite(const std::string& name, const Maker& make) {
  std::printf("\n=== %s ===\n", name.c_str());
  const double rates[] = { 8000, 11025, 22050, 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000 };

  // 1. every sample rate: finite, bounded, ducks, latency as advertised
  for (double fs : rates) {
    auto e = make(); e->prepare(fs);
    SpacerParams p; p.amount = 1; p.attackMs = 5; e->setParams(p);
    const size_t n = (size_t)(fs * 1.5);
    Buf in, sc; for (int c = 0; c < 2; ++c) { in.c[c].resize(n); sc.c[c].resize(n); }
    const double f = std::min(1000.0, fs * 0.2);
    for (size_t i = 0; i < n; ++i) { in.c[0][i] = in.c[1][i] = 0.2f * (float)std::sin(2 * pi * f * (double)i / fs); sc.c[0][i] = sc.c[1][i] = 0.4f * (float)std::sin(2 * pi * f * (double)i / fs); }
    Buf ref = in; runFixed(*e, in, &sc, 2, 2, 480);
    double re = 0, im = 0; const size_t h = n * 3 / 4; for (size_t i = h; i < n; ++i) { re += in.c[0][i] * std::cos(2 * pi * f * (double)i / fs); im += in.c[0][i] * std::sin(2 * pi * f * (double)i / fs); }
    const double cut = -20 * std::log10(std::max(1e-9, 2 * std::hypot(re, im) / (double)(n - h) / 0.2));
    check(finite(in) && peak(in) <= 0.25f, "fs " + std::to_string((int)fs) + ": finite and bounded", peak(in));
    if (fs >= 22050) check(cut > 6.0, "fs " + std::to_string((int)fs) + ": ducks a matching tone (dB)", cut);
    check(e->latency() == 0, "fs " + std::to_string((int)fs) + ": zero latency", e->latency());
  }

  // 2. block-size independence: 1, 7, 333, 4096 and random block sizes must match
  {
    const size_t n = 96000; auto sc = noise(n, 0.4f, 3); auto base = noise(n, 0.3f, 4);
    SpacerParams p; p.amount = 0.9f; p.smooth = 0.4f; p.pan = -0.3f; p.attackMs = 8; p.releaseMs = 90;
    Buf ref = base; { auto e = make(); e->prepare(48000); e->setParams(p); runFixed(*e, ref, &sc, 2, 2, 1); }
    for (int b : { 7, 333, 4096 }) {
      Buf o = base; auto e = make(); e->prepare(48000); e->setParams(p); runFixed(*e, o, &sc, 2, 2, b);
      double worst = 0; for (int c = 0; c < 2; ++c) for (size_t i = 0; i < n; ++i) worst = std::max(worst, (double)std::fabs(o.c[c][i] - ref.c[c][i]));
      check(worst < 1e-5, "block " + std::to_string(b) + " matches block 1 (max diff)", worst);
    }
    Buf o = base; auto e = make(); e->prepare(48000); e->setParams(p); std::mt19937 r(9);
    run(*e, o, &sc, 2, 2, [&] { return 1 + (int)(r() % 5000); });
    double worst = 0; for (int c = 0; c < 2; ++c) for (size_t i = 0; i < n; ++i) worst = std::max(worst, (double)std::fabs(o.c[c][i] - ref.c[c][i]));
    check(worst < 1e-5, "random block sizes 1..5000 match block 1 (max diff)", worst);
  }

  // 3. parameter fuzz: 90 s of audio, new random (sometimes hostile) parameters every few blocks
  for (int pass = 0; pass < 3; ++pass) {
    const double fs = pass == 0 ? 48000 : (pass == 1 ? 44100 : 96000);
    auto e = make(); e->prepare(fs); std::mt19937 r(100 + (uint32_t)pass);
    const size_t n = (size_t)(fs * 30); auto in = noise(n, 0.9f, 20 + (uint32_t)pass), sc = noise(n, 0.9f, 30 + (uint32_t)pass);
    for (size_t i = 0; i < n; ++i) if ((i / 24000) % 3 == 0) { sc.c[0][i] = sc.c[1][i] = 0.f; }   // gaps in the key
    float inPeak = peak(in); bool solverOk = true; float worstCut = 0.f;
    const size_t blk = 256; size_t calls = 0;
    for (size_t p0 = 0; p0 + blk <= n; p0 += blk, ++calls) {
      if (calls % 3 == 0) e->setParams(randomParams(r, true));
      float* x[2] = { in.c[0].data() + p0, in.c[1].data() + p0 };
      const float* s[2] = { sc.c[0].data() + p0, sc.c[1].data() + p0 };
      e->process(x, s, 2, 2, (int)blk);
      if (calls % 50 == 0) { const float mc = e->maxCut(); if (!(mc <= 60.f)) { solverOk = false; worstCut = mc; } }
    }
    check(solverOk, "fuzz " + std::to_string(pass) + ": the solved cuts stay finite and below 60 dB", worstCut);
    check(finite(in), "fuzz " + std::to_string(pass) + ": output always finite");
    check(peak(in) <= inPeak * 1.0001f, "fuzz " + std::to_string(pass) + ": output peak never exceeds the input peak", peak(in) / inPeak);
    check(!hasDenormal(in), "fuzz " + std::to_string(pass) + ": no denormal output");
  }

  // 4. hostile signals
  {
    struct Case { const char* name; std::function<float(size_t)> track, key; };
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    std::vector<Case> cases = {
      { "full-scale square vs full-scale noise", [](size_t i) { return (i / 50) % 2 ? 1.f : -1.f; }, [](size_t i) { return ((i * 2654435761u) >> 16 & 1) ? 1.f : -1.f; } },
      { "DC offset track and key", [](size_t) { return 0.9f; }, [](size_t) { return -0.9f; } },
      { "impulse train", [](size_t i) { return i % 4800 == 0 ? 1.f : 0.f; }, [](size_t i) { return i % 1000 == 0 ? 1.f : 0.f; } },
      { "subnormal inputs", [](size_t) { return 1e-40f; }, [](size_t) { return -1e-40f; } },
      { "huge values (1e30)", [](size_t i) { return i % 3 ? 1e30f : -1e30f; }, [](size_t) { return 1e30f; } },
      { "NaN and inf bursts", [=](size_t i) { return i % 997 < 5 ? nan : 0.3f; }, [=](size_t i) { return i % 1999 < 7 ? inf : -inf * 0.f + 0.4f; } },
      { "silence", [](size_t) { return 0.f; }, [](size_t) { return 0.f; } },
    };
    for (auto& cs : cases) {
      auto e = make(); e->prepare(48000); SpacerParams p; p.amount = 1; p.smooth = 1; e->setParams(p);
      const size_t n = 96000; Buf in, sc; for (int c = 0; c < 2; ++c) { in.c[c].resize(n); sc.c[c].resize(n); for (size_t i = 0; i < n; ++i) { in.c[c][i] = cs.track(i); sc.c[c][i] = cs.key(i); } }
      runFixed(*e, in, &sc, 2, 2, 512);
      const bool subIn = std::string(cs.name) == "subnormal inputs";    // a denormal input may pass straight through; only flag ones we create
      check(finite(in) && (subIn || !hasDenormal(in)) && (!subIn || peak(in) < 1e-30f), std::string("hostile: ") + cs.name + ": finite, no denormals");
    }
    // silence in -> silence out, even after a loud passage (exact zeros once tails die)
    auto e = make(); e->prepare(48000); SpacerParams p; p.amount = 1; e->setParams(p);
    auto loud = noise(48000, 0.8f, 1), key = noise(48000, 0.8f, 2); runFixed(*e, loud, &key, 2, 2, 512);
    Buf z; Buf zs; for (int c = 0; c < 2; ++c) { z.c[c].assign(480000, 0.f); zs.c[c].assign(480000, 0.f); }
    runFixed(*e, z, &zs, 2, 2, 512);
    Buf tail; for (int c = 0; c < 2; ++c) tail.c[c].assign(z.c[c].end() - 96000, z.c[c].end());   // last 2 s
    check(finite(z) && !hasDenormal(z), "silence after loud audio: no denormal tails");
    check(peak(tail) < 1e-6f, "silence after loud audio decays to exact silence", peak(tail));
  }

  // 5. channel layouts
  {
    const size_t n = 48000; auto key = noise(n, 0.5f, 5);
    for (int nCh : { 1, 2 }) for (int nSc : { 0, 1, 2 }) for (bool ms : { false, true }) {
      auto e = make(); e->prepare(48000); SpacerParams p; p.amount = 1; p.ms = ms; p.pan = 0.5f; e->setParams(p);
      auto in = noise(n, 0.5f, 6);
      runFixed(*e, in, nSc ? &key : nullptr, nCh, nSc, 300);
      check(finite(in, nCh) && peak(in, nCh) <= 0.5001f, "layout " + std::to_string(nCh) + " ch / " + std::to_string(nSc) + " sc" + (ms ? " M/S" : "") + ": finite and bounded", peak(in, nCh));
    }
    // sidechain appears and disappears
    auto e = make(); e->prepare(48000); SpacerParams p; p.amount = 1; e->setParams(p);
    auto in = noise(n * 2, 0.5f, 7); auto k2 = noise(n * 2, 0.5f, 8); std::mt19937 r(2); bool have = true; size_t pos = 0;
    while (pos + 480 <= n * 2) { if (r() % 20 == 0) have = !have; float* x[2] = { in.c[0].data() + pos, in.c[1].data() + pos }; const float* s[2] = { k2.c[0].data() + pos, k2.c[1].data() + pos }; e->process(x, have ? s : nullptr, 2, have ? 2 : 0, 480); pos += 480; }
    check(finite(in) && peak(in) <= 0.5001f, "sidechain connecting/disconnecting at random stays finite and bounded", peak(in));
  }

  // 6. re-prepare and reset mid-stream
  {
    auto e = make(); std::mt19937 r(77); SpacerParams p; p.amount = 1; bool ok = true;
    for (int k = 0; k < 40 && ok; ++k) {
      const double fs = rates[r() % 11]; e->prepare(fs); e->setParams(randomParams(r, false));
      auto in = noise(20000, 0.7f, 100 + (uint32_t)k), sc = noise(20000, 0.7f, 200 + (uint32_t)k);
      runFixed(*e, in, &sc, 2, 2, 128);
      if (k % 3 == 0) e->reset();
      ok = finite(in) && peak(in) <= 1.2f;
    }
    check(ok, "40 random prepare()/reset() calls at random sample rates stay finite and bounded");
  }

  // 7. CPU at the worst case: stereo, everything active, smooth 1
  for (double fs : { 48000.0, 192000.0 }) {
    auto e = make(); e->prepare(fs); SpacerParams p; p.amount = 1; p.smooth = 1; e->setParams(p);
    const size_t n = (size_t)(fs * 5); auto in = noise(n, 0.5f, 11), sc = noise(n, 0.5f, 12);
    const auto t0 = std::chrono::steady_clock::now(); runFixed(*e, in, &sc, 2, 2, 256);
    const double load = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 5.0 * 100.0;
    check(load < 60.0, "CPU load at " + std::to_string((int)fs) + " Hz stereo, worst case (% of one core)", load);
  }
}

int main() {
  suite("Zero-latency engine", [] { return std::unique_ptr<Eng>(new Wrap<ZeroLatencyEngine>()); });
  std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
