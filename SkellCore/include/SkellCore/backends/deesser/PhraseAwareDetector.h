#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace skellcore::deesser {

// Lightweight real-time sibilance-pattern tracker. This is acoustic phrase
// segmentation, not lexical/linguistic understanding or an ML vocal model.
// A phrase is a run of broadband activity separated by ~450 ms of silence.
// Each completed sibilance event contributes one peak measurement, so one long
// consonant cannot bias the learned baseline by contributing thousands of frames.
// No dynamic memory, locking, file I/O, lookahead, or audio buffering.
class PhraseAwareDetector {
public:
 struct Decision {
  bool eligible = false;         // enough learned context and a sibilant is active
  bool ready = false;            // at least one completed sibilant in this phrase
  double baselineDb = -60.0;    // prior normal sibilance level
  double thresholdDb = 120.0;   // +120 means no adaptive gain reduction
  int learnedEvents = 0;
 };

 void prepare(double sampleRate) noexcept {
  rate = std::max(1000.0, sampleRate);
  voiceAttack = pole(0.005);
  voiceRelease = pole(0.040);
  sibilantAttack = pole(0.002);
  sibilantRelease = pole(0.016);
  phraseGapSamples = std::max(1, (int)std::lround(rate * 0.45));
  eventGapSamples = std::max(1, (int)std::lround(rate * 0.026));
  minEventSamples = std::max(1, (int)std::lround(rate * 0.011));
  onsetSamples = std::max(1, (int)std::lround(rate * 0.006));
  reset();
 }
 void reset() noexcept {
  voiceEnvelope = 0.0;
  sibilanceEnvelope = 0.0;
  resetPhrase();
 }

 // broadbandAbs is the max(abs(input)) across input channels; detectorAbs is
 // the max(abs(filtered sidechain sample)). The original de-esser supplies both.
 Decision process(double broadbandAbs, double detectorAbs,
                  double absoluteThresholdDb, double sensitivityDb) noexcept {
  broadbandAbs = std::clamp(std::isfinite(broadbandAbs) ? broadbandAbs : 0.0, 0.0, 32.0);
  detectorAbs = std::clamp(std::isfinite(detectorAbs) ? detectorAbs : 0.0, 0.0, 32.0);
  const double vc = broadbandAbs > voiceEnvelope ? voiceAttack : voiceRelease;
  voiceEnvelope = vc * voiceEnvelope + (1.0 - vc) * broadbandAbs;
  const double sc = detectorAbs > sibilanceEnvelope ? sibilantAttack : sibilantRelease;
  sibilanceEnvelope = sc * sibilanceEnvelope + (1.0 - sc) * detectorAbs;

  // This relative test excludes low/mid-band vowels even when their overall
  // loudness is high. The absolute floor prevents learning background hiss.
  // Gate in linear amplitude: no per-sample log() or expensive allocations.
  constexpr double voiceFloor = 0.00251188643151; // -52 dBFS
  constexpr double sibilantFloor = 0.00158489319246; // -56 dBFS
  constexpr double relativeGate = 0.0501187233627; // -26 dB
  const bool voicePresent = voiceEnvelope > voiceFloor;
  if (voicePresent) {
   phraseSilence = 0;
  } else if (phraseSilence < phraseGapSamples + 1) {
   ++phraseSilence;
  }
  if (phraseSilence >= phraseGapSamples) {
   // Reset only once per gap; keep envelope followers warm for the next onset.
   if (!gapReset) { resetPhrase(); gapReset = true; }
  } else if (voicePresent) {
   gapReset = false;
  }

  const bool candidate = voicePresent && sibilanceEnvelope > sibilantFloor
                      && sibilanceEnvelope > voiceEnvelope * relativeGate;
  if (candidate) {
   if (!eventActive) {
    eventActive = true;
    eventPeakAmplitude = 0.0;
    eventSamples = 0;
   }
   eventPeakAmplitude = std::max(eventPeakAmplitude, sibilanceEnvelope);
   if (eventSamples < 100000000) ++eventSamples;
   eventSilence = 0;
  } else if (eventActive && ++eventSilence >= eventGapSamples) {
   completeEvent();
  }

  Decision d;
  d.ready = learnedEvents > 0;
  d.baselineDb = d.ready ? baselineDb : -60.0;
  d.learnedEvents = learnedEvents;
  d.eligible = d.ready && candidate && eventSamples >= onsetSamples;
  if (d.eligible) {
   const double margin = std::clamp(sensitivityDb, 1.0, 12.0);
   // Relative loudness must work on both quiet and hot vocal recordings.
   // Map the existing Threshold fader to a bounded relative sensitivity bias:
   // default -24 dB => no bias; lower => more sensitive, higher => gentler.
   // Keep a >=0.5 dB excess gate so normal-level S sounds stay untouched.
   const double bias = std::clamp((absoluteThresholdDb + 24.0) * 0.25, -6.0, 6.0);
   d.thresholdDb = baselineDb + std::max(0.5, margin + bias);
  }
  return d;
 }

private:
 double pole(double seconds) const noexcept { return std::exp(-1.0 / (rate * seconds)); }
 static double toDb(double linear) noexcept {
  return 20.0 * std::log10(std::max(linear, 1.0e-9));
 }
 void resetPhrase() noexcept {
  baselineDb = -60.0;
  learnedEvents = 0;
  phraseSilence = 0;
  eventActive = false;
  eventPeakAmplitude = 0.0;
  eventSamples = 0;
  eventSilence = 0;
  // Keep gapReset as managed by the caller.
 }
 void completeEvent() noexcept {
  if (eventSamples >= minEventSamples && eventPeakAmplitude > 0.00158489319246) {
   const double eventPeakDb = toDb(eventPeakAmplitude);
   if (learnedEvents == 0) baselineDb = eventPeakDb;
   else {
    const double delta = eventPeakDb - baselineDb;
    // Resist outliers: an unusually sharp S does not redefine "normal".
    if (delta <= 6.0)
     baselineDb += std::clamp(delta, -5.0, 2.0) * (delta < 0.0 ? 0.27 : 0.16);
   }
   learnedEvents = std::min(1000000, learnedEvents + 1);
  }
  eventActive = false;
  eventSamples = 0;
  eventSilence = 0;
  eventPeakAmplitude = 0.0;
 }
 double rate = 48000.0, voiceEnvelope = 0.0, sibilanceEnvelope = 0.0;
 double voiceAttack = .995, voiceRelease = .9997, sibilantAttack = .99, sibilantRelease = .999;
 double baselineDb = -60.0, eventPeakAmplitude = 0.0;
 int phraseGapSamples = 21600, eventGapSamples = 1248, minEventSamples = 528, onsetSamples = 288;
 int phraseSilence = 0, eventSamples = 0, eventSilence = 0, learnedEvents = 0;
 bool eventActive = false, gapReset = false;
};

} // namespace skellcore::deesser
