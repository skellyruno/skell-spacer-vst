#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
class SkellAudioProcessorEditor final : public juce::AudioProcessorEditor {
public:
  explicit SkellAudioProcessorEditor(SkellAudioProcessor&);
  void paint(juce::Graphics&) override;
  void resized() override;
private:
  juce::Slider gain;
  juce::Label title;
  juce::AudioProcessorValueTreeState::SliderAttachment gainAttachment;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellAudioProcessorEditor)
};
