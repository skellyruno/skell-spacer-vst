#pragma once
#include <JuceHeader.h>
#include "SpacerDSP.h"

class SkellAudioProcessor final : public juce::AudioProcessor {
public:
  SkellAudioProcessor();
  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override {}
  bool isBusesLayoutSupported(const BusesLayout& l) const override;
  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override { return true; }
  const juce::String getName() const override { return "Skell Spacer"; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String&) override {}
  void getStateInformation(juce::MemoryBlock&) override;
  void setStateInformation(const void*, int) override;

  const skell::SpacerEngine& getEngine() const { return engine; }
  bool isSidechainActive() const { return sidechainActive.load(std::memory_order_relaxed); }
  double getCurrentSampleRate() const { return sampleRateHz.load(std::memory_order_relaxed); }

  // look of the editor (0 Bone, 1 Neon, 2 Graveyard, 3 Clean); saved with the plugin state
  int getTheme() const { return themeIndex.load(); }
  void setTheme(int t) { themeIndex.store(t); }

  juce::AudioProcessorValueTreeState parameters;
  static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
  skell::SpacerEngine engine;
  std::atomic<float> *pAmount{}, *pSmooth{}, *pAttack{}, *pRelease{}, *pLow{}, *pHigh{}, *pFreeze{}, *pPower{};
  std::atomic<bool> sidechainActive{false};
  std::atomic<double> sampleRateHz{44100.0};
  std::atomic<int> themeIndex{0};
  int sidechainHold = 0;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellAudioProcessor)
};
