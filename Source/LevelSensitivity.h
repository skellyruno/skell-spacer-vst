#pragma once
#include <cmath>
#include <algorithm>
namespace skell {
// Original bounded power-domain knee. At -24 dBFS band peak, half of
// the relative detector target remains when sensitivity is fully enabled.
struct LevelSensitivity {
  static constexpr float kneePower = 0.0039810717f;
  static float scale(float peakPower, float blend) noexcept {
    if (!(blend > 0.f)) return 1.f;
    if (!std::isfinite(peakPower) || peakPower <= 0.f) return 1.f - blend;
    const float knee = peakPower / (peakPower + kneePower);
    return 1.f + blend * (knee - 1.f);
  }
  void prepare(float stepSeconds) noexcept {
    coefficient = 1.f - std::exp(-stepSeconds / 0.025f);
    value = 0.f;
  }
  float advance(float target) noexcept {
    value += coefficient * (target - value);
    if (std::abs(value - target) < 1.e-6f) value = target;
    return value;
  }
  void reset() noexcept { value = 0.f; }
private:
  float value = 0.f, coefficient = 1.f;
};
}
