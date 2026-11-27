#pragma once
#include <algorithm>
#include <cmath>

namespace skell {
// Independent, bounded control-rate heuristics. No classifier, learned
// psychoacoustic model or third-party algorithm is implied.
struct SpectralIntelligence {
  static float unit(float x) { return std::clamp(x, 0.f, 1.f); }
  static float ramp(float x) { x = unit(x); return x*x*(3.f-2.f*x); }
  static float blend(float strength, float factor) { return 1.f - unit(strength)*(1.f-unit(factor)); }

  // Prefer the original narrow detector where smoothing has spread its
  // energy into adjacent bands. Broadband spectra retain their width.
  static float narrow(float raw, float spread) {
    return ramp(raw / std::max(spread, 1e-18f));
  }
  // High-band-dominant key energy is a conservative sibilance/noise proxy.
  // Never claim vowel or consonant classification from this feature.
  static float vocal(float hz, float highFraction) {
    const float high = ramp((hz-3500.f)/4500.f);
    return 1.f - .65f*high*ramp((highFraction-.25f)/.55f);
  }
  // Gentle speech-presence weighting, only reducing cuts at extremes.
  static float perceptual(float hz) {
    const float low = ramp((hz-90.f)/350.f);
    const float high = 1.f-ramp((hz-4500.f)/8000.f);
    return .35f+.65f*low*high;
  }
  // Preserve a prominent instrumental partial when the key has little
  // local overlap. Use neighboring bands as a prominence reference.
  static float harmonic(float main, float neighbors, float overlap) {
    const float prominence = ramp((main / std::max(neighbors, 1e-18f)-1.5f)/4.f);
    return 1.f-.7f*prominence*(1.f-unit(overlap));
  }
  // When the key already dominates a band, use less attenuation.
  static float budget(float main, float key) {
    const float ratio = main / std::max(main+key, 1e-18f);
    return .25f+.75f*ramp(ratio/.65f);
  }
};
} // namespace skell
