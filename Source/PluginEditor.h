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

// Standard single-value controls with scale-aware fonts, separate from the
// range-slider skin (which deliberately draws two handles).
class SmartLookAndFeel final : public juce::LookAndFeel_V4 {
public:
  juce::Font getComboBoxFont(juce::ComboBox&) override;
  juce::Font getLabelFont(juce::Label&) override;
  int getSliderThumbRadius(juce::Slider&) override;
};

class SpectrumView final : public juce::Component, public juce::SettableTooltipClient {
public:
  explicit SpectrumView(SkellAudioProcessor& p) : proc(p) {}
  void tick();
  void paint(juce::Graphics&) override;
  float maxCutDb() const { return peakCut; }
  ~SpectrumView() override { endBrushGesture(); }
  void setSmartView(bool show) { smartView = show; if(!show) setBrushMode(false); repaint(); }
  void setBrushMode(bool enabled);
  void mouseDown(const juce::MouseEvent&) override;
  void mouseDrag(const juce::MouseEvent&) override;
  void mouseUp(const juce::MouseEvent&) override;
  void clearBrush();
  const skell::ZeroLatencyEngine::SmartSnapshot& smartData() const { return smartSnapshot; }
private:
  void paintBrush(juce::Graphics&,juce::Rectangle<float>,float);
  void brushAt(juce::Point<float>,bool);
  void endBrushGesture();
  bool brushMode=false, brushDragging=false, brushErase=false;
  int lastBrushNode=-1;
  float lastBrushValue=0.f;
  std::array<bool,skell::ProtectionCurve::count> brushGestures{};
  SkellAudioProcessor& proc;
  static constexpr int N = skell::ZeroLatencyEngine::kDisp;
  float mainDb[N] = {}, scDb[N] = {}, cutDb[N] = {};
  float sMain[N] = {}, sSc[N] = {}, sCut[N] = {};
  float peakCut = 0.0f;
  bool primed = false, smartView = false;
  skell::ZeroLatencyEngine::SmartSnapshot smartSnapshot;
  float overlap[skell::ZeroLatencyEngine::kBands] = {}, protection[skell::ZeroLatencyEngine::kBands] = {};
  double lastTickMs = 0.0, lastAnalysisMs = 0.0;
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
  void showSmartPage(bool);
  void bindSmartControl();
  void updateSmartColours();

  SkellAudioProcessor& proc;
  int theme = 0;
  std::unique_ptr<SkellLookAndFeel> look;
  SpectrumView spectrum;
  juce::Slider amount, smooth, pan, attack, release;
  juce::TextButton freeze { "Freeze" }, power { "Power" }, listen { "Listen" }, skinButton, msToggle, bandToggle;
  juce::TextButton smartPageButton {"Smart"}, deltaButton {"Delta"}, brushButton {"Brush"}, clearBrushButton {"Clear"};
  SmartLookAndFeel smartLook;
  juce::ComboBox smartSelector;
  juce::Slider smartValue;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> smartValueA;
  juce::AudioProcessorValueTreeState::ButtonAttachment deltaA;
  bool smartPage = false;

  juce::AudioProcessorValueTreeState::SliderAttachment amountA, smoothA, panA, attackA, releaseA;
  juce::AudioProcessorValueTreeState::ButtonAttachment freezeA, powerA, listenA, msA;
  BandControl band, scBand;       // affected range (Low / High Cut) and sidechain filter (high-pass / low-pass), shown one at a time
  void showSidechainFilter(bool show);   // the bottom card is either the Low/High Cut band or the sidechain filter
  int previousMeterSegments = -1;
  double previousDisplayRate = 0.0;
  bool previousMsMode = false;
  bool scActive = false;   // a sidechain is connected and carrying audio (status line)
  bool scView = false;     // the bottom card currently shows the sidechain filter
  juce::Label entry;       // inline value editor (right-click a knob)
  juce::Slider* entryFor = nullptr;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellAudioProcessorEditor)
};
