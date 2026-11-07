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
  setLatencySamples(skell::SpacerEngine::kLatency);
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
  return layout;
}

void SkellAudioProcessor::prepareToPlay(double sampleRate, int) {
  engine.prepare(sampleRate);
  sampleRateHz.store(sampleRate);
  sidechainHold = 0;
  sidechainActive.store(false);
  setLatencySamples(skell::SpacerEngine::kLatency);
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

  skell::SpacerParams p;
  p.amount = pAmount->load() * 0.01f;
  p.smooth = pSmooth->load() * 0.01f;
  p.attackMs = pAttack->load();
  p.releaseMs = pRelease->load();
  p.lowHz = pLow->load();
  p.highHz = pHigh->load();
  p.freeze = pFreeze->load() > 0.5f;
  p.power = pPower->load() > 0.5f;
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

  engine.process(mainPtrs, scPtrs, nMain, nSc, n);
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
