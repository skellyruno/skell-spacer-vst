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
  pLink = parameters.getRawParameterValue("link");
  pAttack = parameters.getRawParameterValue("attack");
  pRelease = parameters.getRawParameterValue("release");
  pLow = parameters.getRawParameterValue("lowcut");
  pHigh = parameters.getRawParameterValue("highcut");
  pFreeze = parameters.getRawParameterValue("freeze");
  pPower = parameters.getRawParameterValue("power");
  pLive = parameters.getRawParameterValue("liveMode");
  pPan = parameters.getRawParameterValue("pan");
  pMs = parameters.getRawParameterValue("ms");
  setLatencySamples(engine.latencySamples());
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

  layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"link", 1}, "Stereo Link",
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

  layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"freeze", 1}, "Freeze", false));
  layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"power", 1}, "Power", true));
  layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"pan", 1}, "Pan",
    juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f), 0.0f,
    AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction([](float v, int) {
      const int r = juce::roundToInt(v); return r == 0 ? juce::String("C") : juce::String(std::abs(r)) + (r < 0 ? " L" : " R"); })));
  layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"ms", 1}, "Mid/Side", false));
  // Live = zero-latency engine (the default, like Trackspacer); Studio = FFT engine with N samples of latency.
  // Latency modes are lifecycle configuration, not per-sample effect automation.
  layout.add(std::make_unique<juce::AudioParameterBool>(
    juce::ParameterID{"liveMode", 1}, "Live Tracking", true));
  return layout;
}

void SkellAudioProcessor::prepareToPlay(double sampleRate, int) {
  prepared_.store(false, std::memory_order_release);
  engine.prepare(sampleRate);
  liveEngine.prepare(sampleRate);
  sampleRateHz.store(sampleRate);
  sidechainHold = 0;
  sidechainActive.store(false);
  lastCallbackTick_.store(0, std::memory_order_relaxed);
  // The active engine and the reported host delay are published together at
  // preparation. Never change JUCE host PDC from processBlock.
  const bool requested = pLive != nullptr && pLive->load() > 0.5f;
  setLatencySamples(requested ? liveEngine.latencySamples() : engine.latencySamples());
  liveActive_.store(requested, std::memory_order_release);
  prepared_.store(true, std::memory_order_release);
}

void SkellAudioProcessor::applyRequestedModeIfStopped() {
  // Called ONLY on the JUCE message thread by the editor timer, or while
  // initializing. A playing transport defers the PDC mode change.
  if (!prepared_.load(std::memory_order_acquire) || pLive == nullptr) return;
  // Hosts sometimes stop processBlock entirely when transport stops, leaving
  // the last observed playing flag stale. Only treat it as playing when
  // callbacks are still arriving. Tick wrap is safe with unsigned arithmetic.
  const auto last = lastCallbackTick_.load(std::memory_order_relaxed);
  const bool callbacksRecent = last != 0 &&
      (juce::Time::getMillisecondCounter() - last) < 150u;
  if (callbacksRecent && transportPlaying_.load(std::memory_order_acquire)) return;
  const bool requested = pLive->load(std::memory_order_relaxed) > 0.5f;
  if (requested == liveActive_.load(std::memory_order_acquire)) return;
  setLatencySamples(requested ? liveEngine.latencySamples() : engine.latencySamples());
  liveActive_.store(requested, std::memory_order_release);
}

void SkellAudioProcessor::readSpectrum(float* mainDb, float* sideDb, float* cutDb) const {
  if (!mainDb || !sideDb || !cutDb) return;
  if (isLiveActive()) liveEngine.getDisplay(mainDb, sideDb, cutDb);
  else engine.getDisplay(mainDb, sideDb, cutDb);
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
  lastCallbackTick_.store(juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
  // Host playback state is observed on the audio callback without publishing PDC
  // or changing processor topology there. Unknown state permits a stopped-mode
  // change; hosts that expose playback state prevent active transport switching.
  bool isPlaying = false;
  if (auto* playhead = getPlayHead()) {
    if (auto position = playhead->getPosition())
      isPlaying = position->getIsPlaying();
  }
  transportPlaying_.store(isPlaying, std::memory_order_release);

  skell::SpacerParams p;
  p.amount = pAmount->load() * 0.01f;
  p.smooth = pSmooth->load() * 0.01f;
  p.link = pLink->load() * 0.01f;
  p.attackMs = pAttack->load();
  p.releaseMs = pRelease->load();
  p.lowHz = pLow->load();
  p.highHz = pHigh->load();
  p.freeze = pFreeze->load() > 0.5f;
  p.power = pPower->load() > 0.5f;
  p.pan = pPan->load();
  p.ms = pMs->load() > 0.5f;
  const bool live = liveActive_.load(std::memory_order_acquire);
  if (live) liveEngine.setParams(p); else engine.setParams(p);

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

  if (live) liveEngine.process(mainPtrs, scPtrs, nMain, nSc, n);
  else engine.process(mainPtrs, scPtrs, nMain, nSc, n);
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
