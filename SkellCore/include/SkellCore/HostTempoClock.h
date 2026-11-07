#pragma once
// Use only in JUCE AudioProcessor code after JUCE has been initialized.
// Tempo remains host-controlled; advance each processed block exactly once.
#include <SkellCore/Core.h>
#include <juce_audio_processors/juce_audio_processors.h>
namespace skellcore {
inline void updateTempoFromPlayhead(TempoClock& clock,
                                    const juce::AudioPlayHead::PositionInfo& position) noexcept {
    clock.update(position.getBpm().value_or(clock.bpm()),
                 position.getPpqPosition().value_or(clock.beatPosition()),
                 position.getIsPlaying());
}
} // namespace skellcore
