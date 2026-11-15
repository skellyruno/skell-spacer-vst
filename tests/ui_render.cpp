// Offscreen editor check (no display needed): runs real audio through the processor,
// renders every skin at several sizes to PNG, and exercises the controls.
// See tests/ui_render.sh.   usage: ui_render <output-dir>
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>

// Hooks that juce_gui_extra normally provides; unused in an offscreen run.
#if ! defined(JUCE_WINDOWS) || ! JUCE_WINDOWS
namespace juce {
void privatePostSystemNotification(const String&, const String&) {}
bool juce_handleXEmbedEvent(ComponentPeer*, void*) { return false; }
unsigned long juce_getCurrentFocusWindow(ComponentPeer*) { return 0; }
}
#endif

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
  check(proc.getLatencySamples() == 0, "V16 reports zero added latency");
  check(proc.getNumPrograms() == 1 && proc.getCurrentProgram() == 0 && proc.getProgramName(0) == "Default", "host default program has a visible name");

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
      proc.parameters.getParameter("pan")->setValueNotifyingHost(0.5f);
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
  check(rotaries == 5, "five rotary controls present (incl. Pan)");

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

  if (amount) {   // double-click returns to the default (50%)
    amount->setValue(80.0, juce::sendNotificationSync);
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    amount->mouseDoubleClick(juce::MouseEvent(src, { 30.f, 30.f }, juce::ModifierKeys::leftButtonModifier, 0.f, 0.f, 0.f, 0.f, 0.f, amount, amount,
                                              juce::Time::getCurrentTime(), { 30.f, 30.f }, juce::Time::getCurrentTime(), 2, false));
    pump();
    check(std::abs(proc.parameters.getRawParameterValue("amount")->load() - 50.0f) < 0.01f, "double-click resets Amount to its default");
    // right-click opens the inline value editor; committing text sets the parameter
    auto rc = juce::MouseEvent(src, { 30.f, 30.f }, juce::ModifierKeys::rightButtonModifier, 0.f, 0.f, 0.f, 0.f, 0.f, amount, amount,
                               juce::Time::getCurrentTime(), { 30.f, 30.f }, juce::Time::getCurrentTime(), 1, false);
    ed->mouseDown(rc);
    auto* lab = find<juce::Label>(*ed, [](juce::Label&) { return true; });
    check(lab && lab->isVisible() && lab->isBeingEdited(), "right-click opens the value editor");
    if (lab && lab->getCurrentTextEditor()) {
      lab->getCurrentTextEditor()->setText("65", true); lab->hideEditor(false); pump();
      check(std::abs(proc.parameters.getRawParameterValue("amount")->load() - 65.0f) < 0.01f, "typed 65 sets Amount to 65%");
    }
    amount->setValue(85.0, juce::sendNotificationSync);
  }
  { auto* prm = proc.parameters.getParameter("pan");
    check(std::abs(prm->convertFrom0to1(prm->getValueForText("30 L")) + 30.0f) < 0.2f && std::abs(prm->convertFrom0to1(prm->getValueForText("C"))) < 0.2f, "typing '30 L' / 'C' sets Pan"); }
  auto* listenB = find<juce::TextButton>(*ed, [](juce::TextButton& b) { return b.getButtonText() == "Listen"; });
  check(listenB != nullptr, "Listen button present");
  if (listenB) { listenB->triggerClick(); pump(); check(proc.parameters.getRawParameterValue("listen")->load() > 0.5f, "Listen toggles the parameter"); listenB->triggerClick(); pump(); }

  auto* freeze = find<juce::TextButton>(*ed, [](juce::TextButton& b) { return b.getButtonText() == "Freeze"; });
  auto* power = find<juce::TextButton>(*ed, [](juce::TextButton& b) { return b.getButtonText() == "Power"; });
  check(freeze && power, "freeze and power buttons present");
  if (freeze) { freeze->triggerClick(); pump(); check(proc.parameters.getRawParameterValue("freeze")->load() > 0.5f, "freeze button toggles the parameter"); freeze->triggerClick(); pump(); }
  if (power) { check(power->getToggleState(), "power starts on"); power->triggerClick(); pump(); check(proc.parameters.getRawParameterValue("power")->load() < 0.5f, "power button toggles the parameter"); power->triggerClick(); pump(); }

  { // Listen swaps the track for the sidechain (and back) without a click (fresh instance, so the engine has no history)
    SkellAudioProcessor lp; lp.enableAllBuses(); lp.setRateAndBufferSizeDetails(48000.0, 512); lp.prepareToPlay(48000.0, 512);
    lp.parameters.getParameter("power")->setValueNotifyingHost(0.0f);
    juce::AudioBuffer<float> b(4, 4800); juce::MidiBuffer mi;
    auto fill = [&] { for (int i = 0; i < 4800; ++i) { b.setSample(0, i, 0.5f); b.setSample(1, i, 0.5f); b.setSample(2, i, -0.25f); b.setSample(3, i, -0.25f); } };
    fill(); lp.processBlock(b, mi);
    lp.parameters.getParameter("listen")->setValueNotifyingHost(1.0f);
    fill(); lp.processBlock(b, mi);
    float worst = 0.f; for (int i = 1; i < 4800; ++i) worst = juce::jmax(worst, std::abs(b.getSample(0, i) - b.getSample(0, i - 1)));
    check(std::abs(b.getSample(0, 4799) + 0.25f) < 1e-3f, "Listen outputs the sidechain");
    check(worst < 0.01f, "Listen crossfades without a click (max step " + juce::String(worst, 4) + ")");
    lp.parameters.getParameter("listen")->setValueNotifyingHost(0.0f);
    fill(); lp.processBlock(b, mi);
    check(std::abs(b.getSample(0, 4799) - 0.5f) < 1e-3f, "Listen off returns the track");
    // Listen plays what the detector hears: a DC sidechain through a 1 kHz sidechain high-pass is silent
    auto* hpP = lp.parameters.getParameter("schp"); hpP->setValueNotifyingHost(hpP->convertTo0to1(1000.0f));
    lp.parameters.getParameter("listen")->setValueNotifyingHost(1.0f);
    for (int k = 0; k < 4; ++k) { fill(); lp.processBlock(b, mi); }
    check(std::abs(b.getSample(0, 4799)) < 0.01f, "Listen plays the filtered sidechain (DC removed by the high-pass)");
  }
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

  { // sidechain filter slider: a second two-thumb slider, bound to schp / sclp
    std::vector<juce::Slider*> bands;
    for (auto* ch : ed->getChildren()) if (auto* sl = dynamic_cast<juce::Slider*>(ch)) if (sl->getSliderStyle() == juce::Slider::TwoValueHorizontal) bands.push_back(sl);
    check(bands.size() == 2, "band and sidechain-filter sliders present");
    if (bands.size() == 2) {
      auto* sc = bands[1];
      check(bands[0]->isVisible() && !sc->isVisible(), "the band slider is shown by default, the sidechain filter hidden");
      juce::TextButton* sw = nullptr;                     // the band / sidechain-filter switch is the title above the slider
      for (auto* ch : ed->getChildren()) if (auto* tb = dynamic_cast<juce::TextButton*>(ch)) if (tb->getComponentID() == "titleswitch") sw = tb;
      check(sw != nullptr, "band / sidechain-filter switch present");
      if (sw) sw->triggerClick();
      pump();
      check(!bands[0]->isVisible() && sc->isVisible(), "clicking the switch shows the sidechain filter instead of the band");
      auto src = juce::Desktop::getInstance().getMainMouseSource();
      auto drag = [&](juce::Point<float> from, juce::Point<float> to) {
        auto mk = [&](juce::Point<float> p, bool dragged) {
          return juce::MouseEvent(src, p, juce::ModifierKeys::leftButtonModifier, 0.f, 0.f, 0.f, 0.f, 0.f, sc, sc,
                                  juce::Time::getCurrentTime(), from, juce::Time::getCurrentTime(), 1, dragged);
        };
        sc->mouseDown(mk(from, false)); sc->mouseDrag(mk(to, true)); sc->mouseUp(mk(to, true)); pump();
      };
      const auto b = sc->getLocalBounds().toFloat();
      const float cy = b.getCentreY();
      check(proc.parameters.getRawParameterValue("schp")->load() < 20.5f && proc.parameters.getRawParameterValue("sclp")->load() > 19500.f, "sidechain filters start off");
      drag({ b.getX() + 6, cy }, { b.getX() + b.getWidth() * 0.45f, cy });
      const float hp = proc.parameters.getRawParameterValue("schp")->load();
      check(hp > 300.0f && hp < 1500.0f, "dragging the left thumb moves the sidechain high-pass (" + juce::String(hp, 0) + " Hz)");
      drag({ b.getRight() - 6, cy }, { b.getX() + b.getWidth() * 0.80f, cy });
      const float lpv = proc.parameters.getRawParameterValue("sclp")->load();
      check(lpv > 3000.0f && lpv < 15000.0f, "dragging the right thumb moves the sidechain low-pass (" + juce::String(lpv, 0) + " Hz)");
      check(proc.parameters.getParameter("schp")->getCurrentValueAsText().contains("Hz"), "sidechain high-pass shows its frequency");
      savePng(ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f), outDir.getChildFile("sc_filter_view.png"));
      if (sw) sw->triggerClick();
      pump();
      savePng(ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.0f), outDir.getChildFile("band_view_filter_on.png"));   // hint that the hidden filter is active
      if (sw) sw->triggerClick();
      pump();
      proc.parameters.getParameter("schp")->setValueNotifyingHost(0.0f);
      proc.parameters.getParameter("sclp")->setValueNotifyingHost(1.0f); pump();
      check(proc.parameters.getParameter("schp")->getCurrentValueAsText() == "Off" && proc.parameters.getParameter("sclp")->getCurrentValueAsText() == "Off", "sidechain filters read Off at their limits");
      if (sw) sw->triggerClick();
      pump();
      check(bands[0]->isVisible() && !sc->isVisible(), "clicking again returns to the band");
    }
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
  {
    const float oldAmount = proc.parameters.getRawParameterValue("amount")->load();
    const float oldPan = proc.parameters.getRawParameterValue("pan")->load();
    const float oldHp = proc.parameters.getRawParameterValue("schp")->load();
    proc.parameters.getParameter("amount")->setValueNotifyingHost(0.0f);
    proc.parameters.getParameter("pan")->setValueNotifyingHost(0.0f);
    proc.parameters.getParameter("schp")->setValueNotifyingHost(0.0f);
    proc.setStateInformation(blk.getData(), (int) blk.getSize()); pump();
    check(std::abs(proc.parameters.getRawParameterValue("amount")->load() - oldAmount) < 0.02f &&
          std::abs(proc.parameters.getRawParameterValue("pan")->load() - oldPan) < 0.02f &&
          std::abs(proc.parameters.getRawParameterValue("schp")->load() - oldHp) < 0.2f,
          "state restore recalls Amount, Pan and sidechain high-pass");
  }

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
