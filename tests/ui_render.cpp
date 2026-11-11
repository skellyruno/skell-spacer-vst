// Offscreen editor check (no display needed): runs real audio through the processor,
// renders every skin at several sizes to PNG, and exercises the controls.
// See tests/ui_render.sh.   usage: ui_render <output-dir>
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

// Hooks that juce_gui_extra normally provides; unused in an offscreen run.
namespace juce {
void privatePostSystemNotification(const String&, const String&) {}
bool juce_handleXEmbedEvent(ComponentPeer*, void*) { return false; }
unsigned long juce_getCurrentFocusWindow(ComponentPeer*) { return 0; }
}

static int failures = 0;
static void check(bool ok, const juce::String& what) {
  std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
  if (!ok) ++failures;
}

template <typename T> static T* find(juce::Component& c, std::function<bool(T&)> pred) {
  for (auto* ch : c.getChildren())
    if (auto* t = dynamic_cast<T*>(ch)) if (pred(*t)) return t;
  return nullptr;
}

static void feed(SkellAudioProcessor& p, int blocks) {
  static double ph[8] = {};
  juce::AudioBuffer<float> buf(4, 512);
  juce::MidiBuffer midi;
  juce::Random rng(7);
  const double fs = 48000.0;
  for (int b = 0; b < blocks; ++b) {
    for (int i = 0; i < 512; ++i) {
      float track = 0.f;
      for (int h = 1; h <= 12; ++h) track += (float)std::sin(2 * juce::MathConstants<double>::pi * (110.0 * h) * (b * 512 + i) / fs) * (0.18f / (float)h);
      track += (rng.nextFloat() - 0.5f) * 0.05f;
      const float sc = 0.4f * (float)std::sin(2 * juce::MathConstants<double>::pi * 880.0 * (b * 512 + i) / fs)
                     + 0.2f * (float)std::sin(2 * juce::MathConstants<double>::pi * 2640.0 * (b * 512 + i) / fs);
      buf.setSample(0, i, track); buf.setSample(1, i, track);
      buf.setSample(2, i, sc);    buf.setSample(3, i, sc);
    }
    p.processBlock(buf, midi);
  }
  (void) ph;
}

static void pump(int ms = 60) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

static void savePng(const juce::Image& img, const juce::File& f) {
  f.deleteFile();
  juce::FileOutputStream out(f);
  juce::PNGImageFormat().writeImageToStream(img, out);
}

int main(int argc, char** argv) {
  juce::ScopedJuceInitialiser_GUI init;
  const juce::File outDir(argc > 1 ? argv[1] : ".");
  outDir.createDirectory();

  SkellAudioProcessor proc;
  proc.enableAllBuses();
  proc.setRateAndBufferSizeDetails(48000.0, 512);
  proc.prepareToPlay(48000.0, 512);
  check(proc.getTotalNumInputChannels() == 4, "main + sidechain buses enabled (4 input channels)");
  check(proc.getLatencySamples() == 0, "Live (default) reports zero latency");

  // ---- render every skin at three sizes
  struct Size { int w, h; const char* tag; };
  const Size sizes[] = { { SkellAudioProcessorEditor::defaultWidth, SkellAudioProcessorEditor::defaultHeight, "default" },
                         { 396, 324, "min" }, { 990, 810, "max" } };
  const char* skins[] = { "bone", "neon", "graveyard", "clean" };
  for (int t = 0; t < 4; ++t)
    for (auto& s : sizes) {
      proc.setTheme(t);
      auto* pAmount = proc.parameters.getParameter("amount");
      pAmount->setValueNotifyingHost(0.85f);
      proc.parameters.getParameter("link")->setValueNotifyingHost(0.4f);
      feed(proc, 60);
      std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
      ed->setSize(s.w, s.h);
      juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
      feed(proc, 20);
      juce::MessageManager::getInstance()->runDispatchLoopUntil(120);
      auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f);
      savePng(img, outDir.getChildFile(juce::String(skins[t]) + "_" + s.tag + ".png"));
      check(img.getWidth() == s.w * 2, juce::String(skins[t]) + " " + s.tag + " rendered");
    }

  // ---- controls respond (default skin, default size)
  proc.setTheme(0);
  std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
  ed->setSize(SkellAudioProcessorEditor::defaultWidth, SkellAudioProcessorEditor::defaultHeight);
  int rotaries = 0;
  for (auto* c : ed->getChildren())
    if (auto* s = dynamic_cast<juce::Slider*>(c)) if (s->isRotary()) ++rotaries;
  check(rotaries == 6, "six rotary controls present (incl. Pan)");

  auto* amount = find<juce::Slider>(*ed, [](juce::Slider& s) { return s.isRotary() && s.getMaximum() == 100.0 && s.getValue() == 85.0; });
  check(amount != nullptr, "amount knob mirrors the host parameter");
  if (amount) {
    amount->setValue(30.0, juce::sendNotificationSync);
    check(std::abs(proc.parameters.getRawParameterValue("amount")->load() - 30.0f) < 0.01f, "dragging the knob updates the parameter");
    check(amount->getTextFromValue(30.0) == "30%", "knob text reads 30%");
  }

  auto* panK = find<juce::Slider>(*ed, [](juce::Slider& s) { return s.isRotary() && s.getMinimum() < 0.0; });
  check(panK != nullptr, "bipolar pan knob present");
  if (panK) {
    panK->setValue(-40.0, juce::sendNotificationSync);
    check(std::abs(proc.parameters.getRawParameterValue("pan")->load() + 40.0f) < 0.01f, "pan knob updates the parameter");
    check(panK->getTextFromValue(-40.0) == "40 L" && panK->getTextFromValue(0.0) == "C", "pan reads 40 L / C");
    panK->setValue(0.0, juce::sendNotificationSync);
  }
  auto* msBtn = find<juce::TextButton>(*ed, [](juce::TextButton& b) { return b.getComponentID() == "labelbutton"; });
  check(msBtn != nullptr, "L/R - M/S label button present");
  if (msBtn) { msBtn->triggerClick(); pump(); check(proc.parameters.getRawParameterValue("ms")->load() > 0.5f, "label button switches to M/S");
    savePng(ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f), outDir.getChildFile("ms_mode.png"));
    msBtn->triggerClick(); pump(); check(proc.parameters.getRawParameterValue("ms")->load() < 0.5f, "label button switches back to L/R"); }

  auto* freeze = find<juce::TextButton>(*ed, [](juce::TextButton& b) { return b.getButtonText() == "Freeze"; });
  auto* power = find<juce::TextButton>(*ed, [](juce::TextButton& b) { return b.getButtonText() == "Power"; });
  check(freeze && power, "freeze and power buttons present");
  if (freeze) { freeze->triggerClick(); pump(); check(proc.parameters.getRawParameterValue("freeze")->load() > 0.5f, "freeze button toggles the parameter"); freeze->triggerClick(); pump(); }
  if (power) { check(power->getToggleState(), "power starts on"); power->triggerClick(); pump(); check(proc.parameters.getRawParameterValue("power")->load() < 0.5f, "power button toggles the parameter"); power->triggerClick(); pump(); }

  // band slider: real mouse drag on the low thumb, then on the high thumb
  auto* band = find<juce::Slider>(*ed, [](juce::Slider& s) { return s.getSliderStyle() == juce::Slider::TwoValueHorizontal; });
  check(band != nullptr, "band slider present");
  if (band) {
    auto drag = [&](juce::Point<float> from, juce::Point<float> to) {
      auto src = juce::Desktop::getInstance().getMainMouseSource();
      auto mk = [&](juce::Point<float> p, bool dragged) {
        return juce::MouseEvent(src, p, juce::ModifierKeys::leftButtonModifier, 0.f, 0.f, 0.f, 0.f, 0.f, band, band,
                                juce::Time::getCurrentTime(), from, juce::Time::getCurrentTime(), 1, dragged);
      };
      band->mouseDown(mk(from, false));
      band->mouseDrag(mk(to, true));
      band->mouseUp(mk(to, true));
      pump();
    };
    const auto b = band->getLocalBounds().toFloat();
    const float lowX = b.getX() + 6, highX = b.getRight() - 6, cy = b.getCentreY();
    drag({ lowX, cy }, { b.getX() + b.getWidth() * 0.45f, cy });
    const float lo = proc.parameters.getRawParameterValue("lowcut")->load();
    check(lo > 300.0f && lo < 1500.0f, "dragging the low thumb moves Low Cut (" + juce::String(lo, 0) + " Hz)");
    drag({ highX, cy }, { b.getX() + b.getWidth() * 0.80f, cy });
    const float hi = proc.parameters.getRawParameterValue("highcut")->load();
    check(hi > 3000.0f && hi < 15000.0f, "dragging the high thumb moves High Cut (" + juce::String(hi, 0) + " Hz)");
    // push the low thumb past the high thumb: must keep >= 1.25x apart
    drag({ b.getX() + b.getWidth() * 0.45f, cy }, { b.getRight(), cy });
    const float lo2 = proc.parameters.getRawParameterValue("lowcut")->load(), hi2 = proc.parameters.getRawParameterValue("highcut")->load();
    check(hi2 >= lo2 * 1.249f, "thumbs stay at least 1.25x apart (" + juce::String(lo2, 0) + " / " + juce::String(hi2, 0) + " Hz)");
  }

  // parameter text round trips (what a host shows and what a user can type)
  auto textRoundTrip = [&](const char* id, const juce::String& typed, float expected, float tol) {
    auto* prm = proc.parameters.getParameter(id);
    const float v = prm->convertFrom0to1(prm->getValueForText(typed));
    check(std::abs(v - expected) <= tol, juce::String(id) + ": typing \"" + typed + "\" gives " + juce::String(v, 1));
  };
  textRoundTrip("lowcut", "1.5 kHz", 1500.0f, 1.0f);
  textRoundTrip("highcut", "2k", 2000.0f, 1.0f);
  textRoundTrip("lowcut", "250", 250.0f, 0.5f);
  textRoundTrip("amount", "75%", 75.0f, 0.2f);
  textRoundTrip("attack", "40 ms", 40.0f, 0.2f);
  check(proc.parameters.getParameter("highcut")->getText(proc.parameters.getParameter("highcut")->convertTo0to1(20000.0f), 0) == "20.0 kHz", "high cut displays 20.0 kHz");

  // state round trip keeps the skin
  proc.setTheme(2);
  juce::MemoryBlock blk; proc.getStateInformation(blk);
  proc.setTheme(0); proc.setStateInformation(blk.getData(), (int) blk.getSize());
  check(proc.getTheme() == 2, "skin restored from saved state");

  // freeze / bypass overlays render
  proc.parameters.getParameter("freeze")->setValueNotifyingHost(1.0f);
  proc.parameters.getParameter("power")->setValueNotifyingHost(0.0f);
  std::unique_ptr<juce::AudioProcessorEditor> ed2(proc.createEditor());
  ed2->setSize(SkellAudioProcessorEditor::defaultWidth, SkellAudioProcessorEditor::defaultHeight);
  juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
  savePng(ed2->createComponentSnapshot(ed2->getLocalBounds(), true, 2.0f), outDir.getChildFile("frozen_bypassed.png"));

  ed.reset(); ed2.reset();
  std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
