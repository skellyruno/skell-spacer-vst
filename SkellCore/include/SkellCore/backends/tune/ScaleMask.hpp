#pragma once
// Scale/mask operations used by the audio processor, GUI and regression tests.
// A manual mask of zero means FOLLOW the selected root/scale preset.
#include <cstdint>

namespace skelly {
constexpr std::uint16_t kChromaticMask = 0x0fff;
constexpr std::uint16_t kMajorMask = 0x0ab5; // 0,2,4,5,7,9,11
constexpr std::uint16_t kMinorMask = 0x05ad; // 0,2,3,5,7,8,10

constexpr std::uint16_t scaleMask(int root, int scale) noexcept {
    const std::uint16_t pattern = (scale == 1) ? kMajorMask :
        ((scale == 2) ? kMinorMask : kChromaticMask);
    const unsigned offset = unsigned((root % 12 + 12) % 12);
    return std::uint16_t(((unsigned(pattern) << offset) |
                          (unsigned(pattern) >> (12u - offset))) & 0x0fffu);
}
constexpr std::uint16_t effectiveMask(int root, int scale, int manualMask) noexcept {
    const std::uint16_t manual = std::uint16_t(manualMask) & 0x0fffu;
    return manual != 0 ? manual : scaleMask(root, scale);
}
constexpr std::uint16_t toggleNote(std::uint16_t currentMask, int pitchClass) noexcept {
    if (pitchClass < 0 || pitchClass >= 12) return currentMask;
    const auto next = std::uint16_t(currentMask ^ (1u << unsigned(pitchClass)));
    // There must always be at least one permitted target note.
    return next == 0 ? currentMask : next;
}
} // namespace skelly
