#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// Flat, squared bone controls (Skell house style).
class SkellLookAndFeel final : public juce::LookAndFeel_V4 {
public:
  const int* themeIndex = nullptr;
  void sync() const;
  void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
  void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                        juce::Slider::SliderStyle, juce::Slider&) override;
  int getSliderThumbRadius(juce::Slider&) override { return 6; }
  void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
  juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
  void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
};

class SpectrumView final : public juce::Component {
public:
  explicit SpectrumView(SkellAudioProcessor& p) : proc(p) {}
  void tick();
  void paint(juce::Graphics&) override;
  float maxCutDb() const { return peakCut; }
private:
  SkellAudioProcessor& proc;
  static constexpr int N = skell::SpacerEngine::kDisp;
  float mainDb[N] = {}, scDb[N] = {}, cutDb[N] = {};
  float sMain[N] = {}, sSc[N] = {}, sCut[N] = {};
  float peakCut = 0.0f;
  bool primed = false;
};

// Everything is laid out in a fixed 550 x 450 design space and scaled to the window.
class SkellAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
  explicit SkellAudioProcessorEditor(SkellAudioProcessor&);
  ~SkellAudioProcessorEditor() override;
  void paint(juce::Graphics&) override;
  void resized() override;

  static constexpr int designWidth = 550, designHeight = 450;
  static constexpr int defaultWidth = 495, defaultHeight = 405;
private:
  void timerCallback() override;
  void bandChanged();
  float scale() const { return (float) getWidth() / (float) designWidth; }
  void repaintDesign(juce::Rectangle<float> r);

  SkellAudioProcessor& proc;
  int theme = 0;
  std::unique_ptr<SkellLookAndFeel> look;
  SpectrumView spectrum;
  juce::Slider amount, smooth, link, attack, release, band;
  juce::TextButton freeze { "Freeze" }, power { "Power" }, live { "Live" }, skinButton;

  juce::AudioProcessorValueTreeState::SliderAttachment amountA, smoothA, linkA, attackA, releaseA;
  juce::AudioProcessorValueTreeState::ButtonAttachment freezeA, powerA, liveA;
  juce::RangedAudioParameter *lowParam, *highParam;
  juce::ParameterAttachment lowA, highA;
  int draggedThumb = 0;
  bool scActive = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellAudioProcessorEditor)
};
