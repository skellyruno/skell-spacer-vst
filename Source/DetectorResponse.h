#pragma once
#include <algorithm>
#include <cmath>
namespace skell {
// Reject corrupt key samples before filtering or squaring them. Normal audio,
// including floating-point headroom up to +36 dBFS, stays unchanged.
struct DetectorResponse {
  static float safeKey(float x) noexcept {
    return std::isfinite(x) && std::abs(x) <= 64.f ? x : 0.f;
  }
  static float fastCoefficient(double fs, float hz) noexcept {
    // Independent experimental power detector, not a vendor gain law.
    const double tau = std::max(0.001, 1.0 / static_cast<double>(hz));
    return static_cast<float>(-std::expm1(-1.0 / (fs * tau)));
  }
  static float fastBlend(float sensitivity, float attackMs) noexcept {
    return sensitivity * std::clamp((4.f - attackMs) / 3.f, 0.f, 1.f);
  }
  static float coefficient(float classic, float fast, float blend) noexcept {
    return blend > 0.f ? classic + blend * (fast - classic) : classic;
  }
};
}
