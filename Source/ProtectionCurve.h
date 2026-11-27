#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace skell {
struct ProtectionCurve {
  static constexpr int count=21;
  using Nodes=std::array<float,count>;
  static float safe(float v) { return std::isfinite(v)?std::clamp(v,0.f,1.f):0.f; }
  // Half-octave nodes, 20 Hz to 20.48 kHz. Band positions use octave units.
  static float at(const Nodes& values,float octave) {
    const float pos=std::clamp(octave*2.f,0.f,static_cast<float>(count-1));
    const int i=static_cast<int>(pos), j=std::min(i+1,count-1);
    const float t=pos-static_cast<float>(i);
    return values[static_cast<size_t>(i)]*(1.f-t)+values[static_cast<size_t>(j)]*t;
  }
  // Snap the time constant (not musical phase) to the nearest power-of-two
  // subdivision, using log-distance. Fall back to the exact requested time
  // when a host does not supply a sane tempo. The release remains causal.
  static float syncedMs(float ms,float bpm) {
    if(!std::isfinite(bpm)||bpm<20.f||bpm>400.f) return ms;
    const float beat=60000.f/bpm;
    const float subdivision=std::clamp(std::round(std::log2(ms/beat)),-3.f,1.f);
    return std::clamp(beat*std::exp2(subdivision),5.f,2000.f);
  }
};
} // namespace skell
