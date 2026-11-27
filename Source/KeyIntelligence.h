#pragma once
#include <algorithm>
#include <cmath>
namespace skell {
// Opt-in causal research laws, not a source classifier or a phase equalizer.
struct KeyIntelligence {
  static float unit(float x) { return std::clamp(x,0.f,1.f); }
  static float ramp(float x) { x=unit(x); return x*x*(3.f-2.f*x); }
  // Relative level confidence. A sustained strong key retains full weight;
  // a decaying, much quieter band receives less influence. A new onset can
  // recover confidence. Does not assert that quiet sound is reverb or noise.
  static float confidence(float power,float reference,float trend) {
    const float ratio=power/std::max(reference,1e-18f);
    return .10f+.90f*std::max(ramp((ratio-.003f)/.060f),.75f*unit(trend));
  }
  // Causal quiet-phrase rescue: a new rise earns short-lived confidence.
  // Cancel on a pronounced fall rather than holding a decaying tail open.
  // This is a level-history heuristic, not a speech or reverb classifier.
  static float phraseHold(float held,float power,float history,float reference,float rise,float decay) {
    if(power<std::max(1e-9f,.0005f*reference)||power<.20f*history) return 0.f;
    return std::max(held*decay,rise>.65f?rise:0.f);
  }
  // Fraction of the wet/dry segment permitted by the existing linked peak
  // ceiling. The dry endpoint is inside that ceiling. Never add a boost.
  static float safeWet(float dry,float wet,float cap) {
    if(wet>cap) return unit(static_cast<float>((static_cast<double>(cap)-dry)/(static_cast<double>(wet)-dry)));
    if(wet< -cap) return unit(static_cast<float>((-static_cast<double>(cap)-dry)/(static_cast<double>(wet)-dry)));
    return 1.f;
  }
};
} // namespace skell
