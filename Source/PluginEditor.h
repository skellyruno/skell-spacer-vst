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
  void drawIconButton(juce::Graphics&, juce::Button&, bool highlighted, bool down);
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
  static constexpr int N = skell::ZeroLatencyEngine::kDisp;
  float mainDb[N] = {}, scDb[N] = {}, cutDb[N] = {};
  float sMain[N] = {}, sSc[N] = {}, sCut[N] = {};
  float peakCut = 0.0f;
  bool primed = false;
};

// A two-thumb log-frequency range slider bound to a low and a high parameter (used for the band and the sidechain filter).
class BandControl {
public:
  BandControl(juce::RangedAudioParameter& lo, juce::RangedAudioParameter& hi, std::function<void()> changed);
  void refresh() { slider.setMinAndMaxValues(lowP.getValue(), highP.getValue(), juce::dontSendNotification); }
  void sendInitialUpdate() { lowA.sendInitialUpdate(); highA.sendInitialUpdate(); }

  juce::Slider slider;
  juce::RangedAudioParameter &lowP, &highP;
private:
  juce::ParameterAttachment lowA, highA;
  int draggedThumb = 0;
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
  float scale() const { return (float) getWidth() / (float) designWidth; }
  void repaintDesign(juce::Rectangle<float> r);
  void mouseDown(const juce::MouseEvent&) override;
  void beginTypedEntry(juce::Slider&);

  SkellAudioProcessor& proc;
  int theme = 0;
  std::unique_ptr<SkellLookAndFeel> look;
  SpectrumView spectrum;
  juce::Slider amount, smooth, pan, attack, release;
  juce::TextButton freeze { "Freeze" }, power { "Power" }, listen { "Listen" }, skinButton, msToggle, bandToggle;

  juce::AudioProcessorValueTreeState::SliderAttachment amountA, smoothA, panA, attackA, releaseA;
  juce::AudioProcessorValueTreeState::ButtonAttachment freezeA, powerA, listenA, msA;
  BandControl band, scBand;       // affected range (Low / High Cut) and sidechain filter (high-pass / low-pass), shown one at a time
  void showSidechainFilter(bool show);   // the bottom card is either the Low/High Cut band or the sidechain filter
  bool scActive = false;   // a sidechain is connected and carrying audio (status line)
  bool scView = false;     // the bottom card currently shows the sidechain filter
  juce::Label entry;       // inline value editor (right-click a knob)
  juce::Slider* entryFor = nullptr;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellAudioProcessorEditor)
};
