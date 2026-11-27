#pragma once
#include <algorithm>
#include <cmath>

namespace skell {
// Independent research laws: all inputs are nonnegative finite analysis
// powers unless otherwise specified. Nothing here performs stem separation.
struct MixIntelligence {
  static float unit(float x) { return std::clamp(x, 0.f, 1.f); }
  static float ramp(float x) { x=unit(x); return x*x*(3.f-2.f*x); }
  // L/R territory: retain cut on the more conflicting side, reduce only the
  // other side, and leave centered/equal sources linked.
  static float territory(float mainL,float mainR,float keyL,float keyR,int channel) {
    const float kl=keyL/std::max(keyL+keyR,1e-18f);
    const float kr=1.f-kl;
    // A centered key is not evidence of a spatial conflict asymmetry.
    const float evidence=ramp(std::fabs(kl-kr));
    const float l=mainL*kl,r=mainR*kr;
    const float mx=std::max(l,r);
    if(mx<1e-18f) return 1.f;
    const float local=channel==0?l:r;
    return 1.f-.8f*evidence*(1.f-ramp(local/mx));
  }
  // Glasberg-Moore-style ERB bandwidth approximation (Hz), used only to
  // define a local auditory neighborhood, not a calibrated hearing model.
  static float erb(float hz) { return 24.7f*(1.f+.00437f*hz); }
  static float auditoryWeight(float center,float other) {
    const float distance=std::fabs(center-other)/erb(center);
    return 1.f-ramp(distance/2.f);
  }
  static float auditoryNeed(float main,float key,float neighborKey) {
    const float ratio=main/std::max(main+key+.5f*neighborKey,1e-18f);
    return .30f+.70f*ramp(ratio/.65f);
  }
  // Component proxies: protect low sustained material and broadband onsets
  // unless local conflict is strong. No source labels or separated audio.
  static float component(float hz,float onset,float conflict) {
    const float bass=1.f-ramp((hz-60.f)/200.f);
    const float hit=ramp((hz-120.f)/800.f)*unit(onset);
    const float protect=std::max(.65f*bass,.8f*hit);
    return 1.f-protect*(1.f-.65f*unit(conflict));
  }
  // Causal onset trend, only accelerates existing attack; no future samples
  // and no nonzero request in the absence of a detected key.
  static float rising(float current,float history) {
    if(current<1e-10f) return 0.f;
    return ramp((current-history)/std::max(current,1e-18f));
  }
  static float attack(float coefficient,float trend,float strength) {
    const float c=unit(coefficient);
    return c+(c*c-c)*unit(trend)*unit(strength);
  }

  // Offline research only: orthogonal stereo steering preserves instantaneous
  // L^2+R^2. It can change mono fold-down and is NOT a transparent unmasker.
  struct Stereo { float l,r; };
  static Stereo steer(float l,float r,float angle) {
    angle=std::clamp(angle,-.174532925f,.174532925f);
    const float c=std::cos(angle),s=std::sin(angle);
    return {c*l-s*r,s*l+c*r};
  }
  // Offline research only: reallocate removed power to an existing
  // complementary component, at most +1 dB amplitude. No new frequencies.
  static float redistribute(float removedPower,float existingPower) {
    if(existingPower<1e-12f || removedPower<=0.f) return 1.f;
    return std::sqrt(1.f+std::min(removedPower/existingPower,.258925412f));
  }
};
} // namespace skell
