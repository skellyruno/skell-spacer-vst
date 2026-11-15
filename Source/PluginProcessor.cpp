#include "PluginProcessor.h"
#include "PluginEditor.h"

using juce::AudioParameterFloatAttributes;

static juce::NormalisableRange<float> freqRange() {
  return { 20.0f, 20000.0f,
           [](float s, float e, float p) { return s * std::pow(e / s, p); },
           [](float s, float e, float v) { return std::log(v / s) / std::log(e / s); },
           [](float s, float e, float v) { return juce::jlimit(s, e, v); } };
}

static juce::String freqText(float v) {
  return v >= 1000.0f ? juce::String(v / 1000.0f, 1) + " kHz" : juce::String(v, 1) + " Hz";
}
static float freqFromText(const juce::String& t) {
  auto v = t.getFloatValue();
  return t.containsIgnoreCase("k") ? v * 1000.0f : v;
}

SkellAudioProcessor::SkellAudioProcessor()
 : AudioProcessor(BusesProperties()
     .withInput("Input", juce::AudioChannelSet::stereo(), true)
     .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
   parameters(*this, nullptr, "PARAMETERS", createLayout()) {
  pAmount = parameters.getRawParameterValue("amount");
  pSmooth = parameters.getRawParameterValue("smooth");
  pAttack = parameters.getRawParameterValue("attack");
  pRelease = parameters.getRawParameterValue("release");
  pLow = parameters.getRawParameterValue("lowcut");
  pHigh = parameters.getRawParameterValue("highcut");
  pFreeze = parameters.getRawParameterValue("freeze");
  pPower = parameters.getRawParameterValue("power");
  pPan = parameters.getRawParameterValue("pan");
  pMs = parameters.getRawParameterValue("ms");
  pListen = parameters.getRawParameterValue("listen");
  pScLow = parameters.getRawParameterValue("schp");
  pScHigh = parameters.getRawParameterValue("sclp");
  setLatencySamples(0);
}

juce::AudioProcessorValueTreeState::ParameterLayout SkellAudioProcessor::createLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;
  auto pct = [](float v, int) { return juce::String(juce::roundToInt(v)) + "%"; };
  auto ms = [](float v, int) { return juce::String(juce::roundToInt(v)) + " ms"; };

  layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"amount", 1}, "Amount",
    juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 50.0f,
    AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(pct)));
  layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"smooth", 1}, "Smooth",
    juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 30.0f,
    AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(pct)));

  // Legacy automation IDs are intentionally retained for existing FL Studio sessions.
  // The V16 detector is mono-keyed, so legacy Link is stored but not evaluated.
  layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"link", 1}, "Stereo Link (Legacy)",
    juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f,
    AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction(pct)));

  juce::NormalisableRange<float> atk(1.0f, 200.0f, 0.1f);  atk.setSkewForCentre(20.0f);
  juce::NormalisableRange<float> rel(10.0f, 1000.0f, 1.0f); rel.setSkewForCentre(150.0f);
  layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"attack", 1}, "Attack", atk, 10.0f,
    AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction(ms)));
  layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"release", 1}, "Release", rel, 150.0f,
    AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction(ms)));

  layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"lowcut", 1}, "Low Cut", freqRange(), 20.0f,
    AudioParameterFloatAttributes().withLabel("Hz")
      .withStringFromValueFunction([](float v, int) { return freqText(v); })
      .withValueFromStringFunction([](const juce::String& t) { return freqFromText(t); })));
  layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"highcut", 1}, "High Cut", freqRange(), 20000.0f,
    AudioParameterFloatAttributes().withLabel("Hz")
      .withStringFromValueFunction([](float v, int) { return freqText(v); })
      .withValueFromStringFunction([](const juce::String& t) { return freqFromText(t); })));

  // sidechain filters: shape what the detector hears (20 Hz / 20 kHz = off), 24 dB/oct
  layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"schp", 1}, "Sidechain High-Pass", freqRange(), 20.0f,
    AudioParameterFloatAttributes().withLabel("Hz")
      .withStringFromValueFunction([](float v, int) { return v <= 20.5f ? juce::String("Off") : freqText(v); })
      .withValueFromStringFunction([](const juce::String& t) { return t.trim().startsWithIgnoreCase("o") ? 20.0f : freqFromText(t); })));
  layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"sclp", 1}, "Sidechain Low-Pass", freqRange(), 20000.0f,
    AudioParameterFloatAttributes().withLabel("Hz")
      .withStringFromValueFunction([](float v, int) { return v >= 19500.0f ? juce::String("Off") : freqText(v); })
      .withValueFromStringFunction([](const juce::String& t) { return t.trim().startsWithIgnoreCase("o") ? 20000.0f : freqFromText(t); })));

  layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"freeze", 1}, "Freeze", false));
  layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"power", 1}, "Power", true));
  layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"pan", 1}, "Pan",
    juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f), 0.0f,
    AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction([](float v, int) {
      const int r = juce::roundToInt(v); return r == 0 ? juce::String("C") : juce::String(std::abs(r)) + (r < 0 ? " L" : " R"); })
      .withValueFromStringFunction([](const juce::String& t) {
        if (t.trim().startsWithIgnoreCase("c")) return 0.0f;
        const float v = std::abs(t.getFloatValue());
        return t.containsIgnoreCase("l") ? -v : v; })));
  layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"ms", 1}, "Mid/Side", false));
  layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"listen", 1}, "Listen", false));
  // Keep the original parameter ID to decode old saved host automation/state.
  // V16 always runs the zero-latency engine; historical Studio DSP is retired.
  layout.add(std::make_unique<juce::AudioParameterBool>(
    juce::ParameterID{"liveMode", 1}, "Live Tracking (Legacy)", true));
  return layout;
}

void SkellAudioProcessor::prepareToPlay(double sampleRate, int) {
  engine.prepare(sampleRate);
  sampleRateHz.store(sampleRate);
  sidechainHold = 0;
  sidechainActive.store(false);
  listenMix = 0.f;
}

void SkellAudioProcessor::readSpectrum(float* mainDb, float* sideDb, float* cutDb) const {
  if (mainDb && sideDb && cutDb) engine.getDisplay(mainDb, sideDb, cutDb);
}

bool SkellAudioProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
  auto in = l.getMainInputChannelSet();
  if (in != l.getMainOutputChannelSet()) return false;
  if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
  auto sc = l.getChannelSet(true, 1);
  return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void SkellAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
  juce::ScopedNoDenormals guard;

  // A buffer smaller than the negotiated layout (or empty) is never valid; leave it untouched instead of reading past it.
  if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() < juce::jmax(getTotalNumInputChannels(), getTotalNumOutputChannels())) return;

  skell::SpacerParams p;
  p.amount = pAmount->load() * 0.01f;
  p.smooth = pSmooth->load() * 0.01f;
  p.attackMs = pAttack->load();
  p.releaseMs = pRelease->load();
  p.lowHz = pLow->load();
  p.highHz = pHigh->load();
  p.scLowHz = pScLow->load();
  p.scHighHz = pScHigh->load();
  p.freeze = pFreeze->load() > 0.5f;
  p.power = pPower->load() > 0.5f;
  p.pan = pPan->load();
  p.ms = pMs->load() > 0.5f;
  engine.setParams(p);

  auto mainBuf = getBusBuffer(buffer, true, 0);
  float* mainPtrs[2] = { nullptr, nullptr };
  const int nMain = juce::jmin(2, mainBuf.getNumChannels());
  for (int c = 0; c < nMain; ++c) mainPtrs[c] = mainBuf.getWritePointer(c);

  const float* scPtrs[2] = { nullptr, nullptr };
  int nSc = 0;
  float scPeak = 0.0f;
  const int n = buffer.getNumSamples();
  if (auto* scBus = getBus(true, 1); scBus != nullptr && scBus->isEnabled()) {
    auto scBuf = getBusBuffer(buffer, true, 1);
    nSc = juce::jmin(2, scBuf.getNumChannels());
    for (int c = 0; c < nSc; ++c) {
      scPtrs[c] = scBuf.getReadPointer(c);
      scPeak = juce::jmax(scPeak, scBuf.getMagnitude(c, 0, n));
    }
  }

  if (scPeak > 1.0e-4f) sidechainHold = (int) sampleRateHz.load();
  else sidechainHold = juce::jmax(0, sidechainHold - n);
  sidechainActive.store(sidechainHold > 0, std::memory_order_relaxed);

  // Listen: monitor the sidechain (as the detector hears it, after its filters) instead of the track, with a 10 ms
  // crossfade; nothing happens without a sidechain. Work in fixed chunks so the monitor copy needs no allocation.
  const float listenTarget = (pListen->load() > 0.5f && nSc > 0) ? 1.f : 0.f;
  if (nSc == 0) listenMix = 0.f;
  const float listenStep = (float) (1.0 / (0.010 * juce::jmax(1000.0, sampleRateHz.load())));
  for (int pos = 0; pos < n; pos += kChunk) {
    const int m = juce::jmin(kChunk, n - pos);
    float* chunk[2] = { nMain > 0 ? mainPtrs[0] + pos : nullptr, nMain > 1 ? mainPtrs[1] + pos : nullptr };
    const float* scChunk[2] = { nSc > 0 ? scPtrs[0] + pos : nullptr, nSc > 1 ? scPtrs[1] + pos : nullptr };
    float* mon[2] = { scMon[0], scMon[1] };
    engine.process(chunk, scChunk, nMain, nSc, m, mon);
    if (nSc == 0 || (listenMix == listenTarget && listenTarget == 0.f)) continue;
    for (int i = 0; i < m; ++i) {
      listenMix = listenMix < listenTarget ? juce::jmin(listenTarget, listenMix + listenStep) : juce::jmax(listenTarget, listenMix - listenStep);
      for (int c = 0; c < nMain; ++c) {
        float s = scMon[0][i];
        if (nSc >= 2) s = nMain == 1 ? 0.5f * (scMon[0][i] + scMon[1][i]) : scMon[c][i];
        chunk[c][i] += (s - chunk[c][i]) * listenMix;
      }
    }
  }
}

juce::AudioProcessorEditor* SkellAudioProcessor::createEditor() { return new SkellAudioProcessorEditor(*this); }

void SkellAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
  auto state = parameters.copyState();
  state.setProperty("theme", themeIndex.load(), nullptr);
  if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}
void SkellAudioProcessor::setStateInformation(const void* data, int size) {
  if (auto xml = getXmlFromBinary(data, size))
    if (xml->hasTagName(parameters.state.getType())) {
      themeIndex.store(juce::jlimit(0, 3, xml->getIntAttribute("theme", 0)));
      parameters.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SkellAudioProcessor(); }
