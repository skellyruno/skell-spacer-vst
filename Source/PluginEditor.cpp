#include "PluginEditor.h"
#include "SkellTheme.h"
using namespace skelltheme;

// ---------------------------------------------------------------- look & feel
void SkellLookAndFeel::sync() const { if (themeIndex != nullptr) applyTheme(*themeIndex); }

void SkellLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float start, float end, juce::Slider& s) {
  sync();
  auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h);
  const float d = juce::jmin(b.getWidth(), b.getHeight());
  const auto c = b.getCentre();
  const float outer = d * 0.5f - d * 0.09f, thick = juce::jmax(2.0f, d * 0.05f);
  const float arcR = outer - thick * 0.5f;
  const float ang = start + pos * (end - start);

  // ticks
  g.setColour(kBoneFaint);
  for (int i = 0; i <= 10; ++i) {
    const float a = start + (end - start) * (float) i / 10.0f;
    const float r0 = outer + 0.5f, r1 = outer + (i % 5 == 0 ? d * 0.07f : d * 0.04f);
    g.drawLine(c.x + r0 * std::sin(a), c.y - r0 * std::cos(a), c.x + r1 * std::sin(a), c.y - r1 * std::cos(a), 1.0f);
  }

  juce::Path track, value;
  track.addCentredArc(c.x, c.y, arcR, arcR, 0, start, end, true);
  const float from = s.getMinimum() < 0.0 ? 0.5f * (start + end) : start;   // bipolar knobs fill from centre
  value.addCentredArc(c.x, c.y, arcR, arcR, 0, from, ang, true);
  g.setColour(kLine);
  g.strokePath(track, juce::PathStrokeType(thick));
  g.setColour(kAccent);
  g.strokePath(value, juce::PathStrokeType(thick));

  const float bodyR = arcR - thick - d * 0.06f;
  g.setColour(s.isMouseOverOrDragging() ? kBtnHover : kPanelHi);
  g.fillEllipse(c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2);
  g.setColour(kLine);
  g.drawEllipse(c.x - bodyR, c.y - bodyR, bodyR * 2, bodyR * 2, 1.0f);

  // square marker at the arc end
  const float ms = juce::jmax(3.0f, d * 0.08f);
  g.setColour(kBone);
  g.fillRect(juce::Rectangle<float>(ms, ms).withCentre({ c.x + arcR * std::sin(ang), c.y - arcR * std::cos(ang) }));

  const auto txt = s.getTextFromValue(s.getValue());
  float fs = d * 0.19f;
  const float room = bodyR * 1.7f, wide = textWidth(makeFont(fs, true, 0.02f), txt);
  if (wide > room) fs *= room / wide;
  drawText(g, txt, juce::Rectangle<float>(bodyR * 2, bodyR * 2).withCentre(c),
           fs, true, s.isMouseButtonDown() ? kAccent : kBone, juce::Justification::centred, 0.02f);
}

void SkellLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float, float minPos,
                                        float maxPos, juce::Slider::SliderStyle, juce::Slider&) {
  sync();
  const float fh = (float) h, cy = (float) y + fh * 0.5f, th = juce::jmax(2.0f, fh * 0.12f);
  g.setColour(kLine);
  g.fillRect(juce::Rectangle<float>((float)x + 6, cy - th * 0.5f, (float)w - 12, th));
  g.setColour(kAccent);
  g.fillRect(juce::Rectangle<float>(minPos, cy - th * 0.5f, maxPos - minPos, th));
  const float tw = juce::jmax(6.0f, fh * 0.30f), tH = fh * 0.78f;
  for (float px : { minPos, maxPos }) {
    auto t = juce::Rectangle<float>(tw, tH).withCentre({ px, cy });
    g.setColour(kBone);
    g.fillRect(t);
    g.setColour(kBg);
    g.fillRect(t.withSizeKeepingCentre(1.2f, tH * 0.5f));
    g.setColour(kLine);
    g.drawRect(t, 1.0f);
  }
}

// round icon buttons: a power symbol and a pair of headphones, drawn as vectors so they stay crisp at any size
void SkellLookAndFeel::drawIconButton(juce::Graphics& g, juce::Button& button, bool highlighted, bool down) {
  const bool on = button.getToggleState();
  const auto b = button.getLocalBounds().toFloat().reduced(1.0f);
  const float d = juce::jmin(b.getWidth(), b.getHeight());
  const auto c = b.getCentre();
  const auto circle = juce::Rectangle<float>(d, d).withCentre(c);
  g.setColour(on ? kAccent : (down ? kBtnDown : (highlighted ? kBtnHover : kPanelHi)));
  g.fillEllipse(circle);
  g.setColour(on ? kAccent : (highlighted ? kBoneDim : kLine));
  g.drawEllipse(circle.reduced(0.5f), 1.0f);

  const auto ink = on ? kOnAccent : (highlighted ? kAccent : kBone);
  g.setColour(ink);
  const float u = d / 30.0f, lw = juce::jmax(1.4f, 2.0f * u);
  if (button.getComponentID() == "icon_power") {
    juce::Path arc;
    const float r = 7.0f * u;
    arc.addCentredArc(c.x, c.y + 0.6f * u, r, r, 0.0f, 0.62f, juce::MathConstants<float>::twoPi - 0.62f, true);
    g.strokePath(arc, juce::PathStrokeType(lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine(c.x, c.y - 8.0f * u, c.x, c.y - 1.0f * u, lw);
    g.fillEllipse(c.x - lw * 0.5f, c.y - 8.0f * u - lw * 0.5f, lw, lw);
  } else {
    juce::Path band;
    const float r = 7.5f * u;
    band.addCentredArc(c.x, c.y + 0.5f * u, r, r, 0.0f, -1.5f, 1.5f, true);   // angles run clockwise from 12 o'clock
    // headband meets the ear cups
    const float cupW = 3.8f * u, cupH = 7.0f * u, cupY = c.y + 0.3f * u;
    g.strokePath(band, juce::PathStrokeType(lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillRoundedRectangle(c.x - r - cupW * 0.5f - 0.2f * u, cupY, cupW, cupH, 1.2f * u);
    g.fillRoundedRectangle(c.x + r - cupW * 0.5f + 0.2f * u, cupY, cupW, cupH, 1.2f * u);
  }
}

void SkellLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                            bool highlighted, bool down) {
  sync();
  if (button.getComponentID() == "logo") {
    if (highlighted || down) {
      g.setColour(kAccent.withAlpha(down ? 0.28f : 0.16f));
      g.fillRoundedRectangle(button.getLocalBounds().toFloat().reduced(1.0f), 5.0f);
    }
    return;
  }
  if (button.getComponentID() == "titleswitch") return;   // invisible: the editor draws the title text itself
  if (button.getComponentID() == "labelbutton") {      // text is drawn by the editor; just show it is clickable
    if (highlighted || down) {
      g.setColour(kAccent.withAlpha(down ? 0.28f : 0.16f));
      g.drawRect(button.getLocalBounds().toFloat().reduced(0.5f), 1.0f);
    }
    return;
  }
  if (button.getComponentID().startsWith("icon_")) { drawIconButton(g, button, highlighted, down); return; }
  auto r = button.getLocalBounds().toFloat().reduced(0.5f);
  auto fill = kPanelHi;
  if (down) fill = kBtnDown; else if (highlighted) fill = kBtnHover;
  const bool on = button.getToggleState();
  if (on) fill = kAccent;
  g.setColour(fill); g.fillRect(r);
  g.setColour(on ? kAccent : (highlighted ? kBoneDim : kLine));
  g.drawRect(r, 1.0f);
}

juce::Font SkellLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
  return makeFont(juce::jmin(12.5f * (float) buttonHeight / 24.0f, (float) buttonHeight * 0.46f), true, 0.06f);
}

void SkellLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) {
  sync();
  if (button.getComponentID() == "logo" || button.getComponentID() == "labelbutton" || button.getComponentID() == "titleswitch" || button.getComponentID().startsWith("icon_")) return;
  g.setColour(button.getToggleState() ? kOnAccent : (button.isMouseOver() ? kAccent : kBone));
  g.setFont(getTextButtonFont(button, button.getHeight()));
  g.drawFittedText(button.getButtonText().toUpperCase(), button.getLocalBounds().reduced(4, 1),
                   juce::Justification::centred, 1, 0.85f);
}

// ---------------------------------------------------------------- spectrum
void SpectrumView::tick() {
  proc.readSpectrum(mainDb, scDb, cutDb);
  float pk = 0.0f;
  for (int i = 0; i < N; ++i) {
    if (!primed) { sMain[i] = mainDb[i]; sSc[i] = scDb[i]; sCut[i] = cutDb[i]; }
    else {
      auto follow = [](float& s, float v) { s += (v - s) * (v > s ? 0.6f : 0.15f); };
      follow(sMain[i], mainDb[i]); follow(sSc[i], scDb[i]);
      sCut[i] += (cutDb[i] - sCut[i]) * 0.35f;
    }
    pk = juce::jmax(pk, sCut[i]);
  }
  peakCut = pk;
  primed = true;
  repaint();
}

void SpectrumView::paint(juce::Graphics& g) {
  auto r = getLocalBounds().toFloat();
  const float s = r.getWidth() / 494.0f;   // design-space scale
  g.setColour(kBg);
  g.fillRect(r);

  auto area = r.reduced(8.0f * s, 10.0f * s);
  auto xAt = [&](int i) { return area.getX() + area.getWidth() * (float) i / (N - 1); };
  auto xHz = [&](float f) { return area.getX() + area.getWidth() * std::log(f / 20.0f) / std::log(1000.0f); };

  const char* labs[] = { "100", "1k", "10k" }; int li = 0;
  for (float f : { 100.0f, 1000.0f, 10000.0f }) {
    g.setColour(kLine);
    g.fillRect(xHz(f), area.getY(), 1.0f, area.getHeight());
    drawText(g, labs[li++], { xHz(f) + 3.0f * s, area.getY() + 1.0f * s, 30.0f * s, 12.0f * s }, 10.0f * s, false, kBoneDim);
  }

  const float y0 = area.getY() + area.getHeight() * 0.42f, span = area.getHeight() * 0.42f;
  auto yDb = [&](float dbv) { return area.getBottom() - area.getHeight() * juce::jlimit(0.0f, 1.0f, (dbv + 100.0f) / 100.0f); };

  // track spectrum (filled, bone)
  juce::Path fill;
  fill.startNewSubPath(xAt(0), area.getBottom());
  for (int i = 0; i < N; ++i) fill.lineTo(xAt(i), yDb(sMain[i]));
  fill.lineTo(xAt(N - 1), area.getBottom());
  fill.closeSubPath();
  g.setGradientFill(juce::ColourGradient(kBoneDim.withAlpha(0.55f), 0, area.getY(), kBoneFaint.withAlpha(0.12f), 0, area.getBottom(), false));
  g.fillPath(fill);

  // sidechain line (bone)
  juce::Path sc;
  for (int i = 0; i < N; ++i) { auto y = yDb(sSc[i] - 5.0f); i ? sc.lineTo(xAt(i), y) : sc.startNewSubPath(xAt(i), y); }
  g.setColour(kBone.withAlpha(0.8f));
  g.strokePath(sc, juce::PathStrokeType(1.0f * s));

  // cut scale: 0 dB baseline down to -24 dB
  g.setColour(kLine);
  g.fillRect(area.getX(), y0, area.getWidth(), 1.0f);
  drawText(g, "0 dB", { area.getX() + 3.0f * s, y0 - 12.0f * s, 40.0f * s, 11.0f * s }, 9.5f * s, false, kBoneDim);
  drawText(g, "-24", { area.getX() + 3.0f * s, y0 + span - 12.0f * s, 40.0f * s, 11.0f * s }, 9.5f * s, false, kBoneDim);

  // cut curve (accent)
  juce::Path cut;
  for (int i = 0; i < N; ++i) {
    auto y = y0 + span * sCut[i] / skell::ZeroLatencyEngine::kMaxCutDb;
    i ? cut.lineTo(xAt(i), y) : cut.startNewSubPath(xAt(i), y);
  }
  g.setColour(kAccent.withAlpha(0.22f));
  g.strokePath(cut, juce::PathStrokeType(4.5f * s));
  g.setColour(kAccent);
  g.strokePath(cut, juce::PathStrokeType(1.8f * s));

  // shade outside the band
  const float lo = proc.parameters.getRawParameterValue("lowcut")->load();
  const float hi = proc.parameters.getRawParameterValue("highcut")->load();
  g.setColour(kBg.withAlpha(0.6f));
  if (lo > 20.5f) g.fillRect(juce::Rectangle<float>(area.getX(), area.getY(), xHz(lo) - area.getX(), area.getHeight()));
  if (hi < 19500.0f) g.fillRect(juce::Rectangle<float>(xHz(hi), area.getY(), area.getRight() - xHz(hi), area.getHeight()));

  {   // legend, on a backing plate so the curves don't cut through it
    const struct { const char* name; int kind; } items[] = { { "CUT", 2 }, { "SIDECHAIN", 1 }, { "TRACK", 0 } };
    const auto legendFont = makeFont(9.0f * s, true, 0.1f);
    float widths[3], total = 0.0f;
    for (int i = 0; i < 3; ++i) { widths[i] = textWidth(legendFont, items[i].name) * 1.12f + 3.0f * s; total += widths[i] + 19.0f * s; }
    float lx = area.getRight() - 4.0f * s;
    g.setColour(kBg.withAlpha(0.8f));
    g.fillRect(juce::Rectangle<float>(lx - total - 2.0f * s, area.getBottom() - 16.0f * s, total + 6.0f * s, 15.0f * s));
    for (int i = 0; i < 3; ++i) {
      lx -= widths[i];
      drawText(g, items[i].name, { lx, area.getBottom() - 13.0f * s, widths[i] + 2.0f * s, 11.0f * s }, 9.0f * s, true, kBoneDim, juce::Justification::centredLeft, 0.1f);
      lx -= 11.0f * s;
      const juce::Rectangle<float> sw(8.0f * s, items[i].kind == 0 ? 7.0f * s : 2.0f * s);
      g.setColour(items[i].kind == 2 ? kAccent : (items[i].kind == 1 ? kBone : kBoneDim));
      g.fillRect(sw.withCentre({ lx + 4.0f * s, area.getBottom() - 7.5f * s }));
      lx -= 8.0f * s;
    }
  }
  const auto readout = "MAX CUT  " + juce::String(-peakCut, 1) + " dB";
  drawText(g, readout, { area.getX() + 4.0f * s, area.getY() + 1.0f * s, 160.0f * s, 12.0f * s },
           10.0f * s, true, kAccent, juce::Justification::centredLeft, 0.1f);

  if (proc.parameters.getRawParameterValue("freeze")->load() > 0.5f) {
    auto tag = juce::Rectangle<float>(58.0f * s, 13.0f * s).withX(area.getX() + 4.0f * s).withBottomY(area.getBottom() - 2.0f * s);
    g.setColour(kAccent); g.fillRect(tag);
    drawText(g, "FROZEN", tag, 9.5f * s, true, kOnAccent, juce::Justification::centred, 0.15f);
  }
  if (proc.parameters.getRawParameterValue("power")->load() < 0.5f) {
    g.setColour(kBg.withAlpha(0.6f)); g.fillRect(r);
    drawText(g, "BYPASSED", r, 16.0f * s, true, kBoneDim, juce::Justification::centred, 0.25f);
  }

  g.setColour(kLine);
  g.drawRect(r, 1.0f);
}

BandControl::BandControl(juce::RangedAudioParameter& lo, juce::RangedAudioParameter& hi, std::function<void()> changed)
 : lowP(lo), highP(hi),
   lowA(lo, [this, changed](float) { refresh(); changed(); }),
   highA(hi, [this, changed](float) { refresh(); changed(); }) {
  slider.setSliderStyle(juce::Slider::TwoValueHorizontal);
  slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
  slider.setRange(0.0, 1.0, 0.0);
  slider.setPopupDisplayEnabled(false, false, nullptr);
  slider.onDragStart = [this] {
    draggedThumb = slider.getThumbBeingDragged();
    if (draggedThumb == 1) lowA.beginGesture();
    if (draggedThumb == 2) highA.beginGesture();
  };
  slider.onDragEnd = [this] {
    if (draggedThumb == 1) lowA.endGesture();
    if (draggedThumb == 2) highA.endGesture();
    draggedThumb = 0;
  };
  slider.onValueChange = [this] {
    // Work out which thumb moved by comparing with the parameters, so a callback that
    // arrives after the drag ended (they can be asynchronous) is still applied.
    const double minGap = std::log(1.25) / std::log(1000.0);
    double lo = slider.getMinValue(), hi = slider.getMaxValue();
    const bool lowMoved  = std::abs(lo - (double) lowP.getValue())  > 1.0e-6;
    const bool highMoved = std::abs(hi - (double) highP.getValue()) > 1.0e-6;
    if (hi - lo < minGap) {
      if (lowMoved && !highMoved) lo = hi - minGap; else hi = lo + minGap;
      if (lo < 0.0) { lo = 0.0; hi = minGap; }
      if (hi > 1.0) { hi = 1.0; lo = 1.0 - minGap; }
      slider.setMinAndMaxValues(lo, hi, juce::dontSendNotification);
    }
    auto push = [](juce::ParameterAttachment& a, juce::RangedAudioParameter& prm, double norm, bool inGesture) {
      const float v = prm.convertFrom0to1((float) norm);
      if (inGesture) a.setValueAsPartOfGesture(v); else a.setValueAsCompleteGesture(v);
    };
    if (lowMoved)  push(lowA,  lowP,  lo, draggedThumb == 1);
    if (highMoved) push(highA, highP, hi, draggedThumb == 2);
  };
}

// ---------------------------------------------------------------- editor
SkellAudioProcessorEditor::SkellAudioProcessorEditor(SkellAudioProcessor& p)
 : AudioProcessorEditor(&p), proc(p), spectrum(p),
   amountA(p.parameters, "amount", amount), smoothA(p.parameters, "smooth", smooth),
   panA(p.parameters, "pan", pan),
   attackA(p.parameters, "attack", attack), releaseA(p.parameters, "release", release),
   freezeA(p.parameters, "freeze", freeze), powerA(p.parameters, "power", power),
    listenA(p.parameters, "listen", listen), msA(p.parameters, "ms", msToggle),
   band(*p.parameters.getParameter("lowcut"), *p.parameters.getParameter("highcut"),
        [this] { repaintDesign({ 14.0f, 368.0f, 522.0f, 70.0f }); }),
   scBand(*p.parameters.getParameter("schp"), *p.parameters.getParameter("sclp"),
          [this] { repaintDesign({ 14.0f, 368.0f, 522.0f, 70.0f }); }) {
  setTitle("Skell Spacer");
  theme = proc.getTheme();
  applyTheme(theme);
  look = std::make_unique<SkellLookAndFeel>();
  look->themeIndex = &theme;

  setSize(defaultWidth, defaultHeight);
  setResizable(true, true);
  setResizeLimits(defaultWidth * 4 / 5, defaultHeight * 4 / 5, defaultWidth * 2, defaultHeight * 2);
  if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) designWidth / (double) designHeight);

  for (auto* s : { &amount, &smooth, &pan, &attack, &release }) {
    s->setLookAndFeel(look.get());
    s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    s->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible(*s);
  }

  band.slider.setTooltip("Low / High Cut: the range of the track that can be ducked");
  scBand.slider.setTooltip("Sidechain filter: high-pass (left) and low-pass (right) on what the detector hears, "
                           "e.g. to keep rumble or sibilance from driving the ducking. Use Listen to hear the result.");
  for (auto* b : { &band, &scBand }) {
    b->slider.setLookAndFeel(look.get());
    addAndMakeVisible(b->slider);
  }
  scBand.slider.setVisible(false);

  for (auto* b : { &freeze, &power, &listen }) {
    b->setLookAndFeel(look.get());
    b->setClickingTogglesState(true);
    b->setWantsKeyboardFocus(false);
    addAndMakeVisible(*b);
  }
  freeze.setTooltip("Hold the current cut shape, even after the sidechain stops");
  power.setTooltip("Power: on = ducking active, off = bypass (crossfaded, delay-aligned)");
  power.setComponentID("icon_power");
  listen.setComponentID("icon_listen");
  listen.setTooltip("Listen: hear the sidechain instead of the track, to tune what drives the ducking");
  pan.setTooltip("Pan: where the ducking is applied. Centre = both sides; left/right = only that side "
                 "(in M/S mode: left = Mid only, right = Side only)");
  msToggle.setTooltip("Switch Pan between Left/Right and Mid/Side");
  msToggle.setLookAndFeel(look.get());
  msToggle.setComponentID("labelbutton");
  msToggle.setClickingTogglesState(true);
  msToggle.setWantsKeyboardFocus(false);
  msToggle.onStateChange = [this] { repaintDesign({ 14.0f, 336.0f, 522.0f, 16.0f }); };
  addAndMakeVisible(msToggle);

  bandToggle.setTooltip("Click the title to switch this slider between the affected BAND (Low / High Cut) and the SIDECHAIN FILTER "
                        "(high-pass / low-pass on what the detector hears; Listen plays the result)");
  bandToggle.setLookAndFeel(look.get());
  bandToggle.setComponentID("titleswitch");
  bandToggle.setMouseCursor(juce::MouseCursor::PointingHandCursor);
  bandToggle.onStateChange = [this] { repaintDesign({ 150.0f, 372.0f, 250.0f, 18.0f }); };
  bandToggle.setWantsKeyboardFocus(false);
  bandToggle.onClick = [this] { showSidechainFilter(!scView); };
  addAndMakeVisible(bandToggle);

  entry.setVisible(false);
  entry.setJustificationType(juce::Justification::centred);
  entry.setEditable(false, false, false);
  entry.onEditorHide = [this] { entry.setVisible(false); };   // entryFor stays valid: onTextChange runs after this
  entry.onTextChange = [this] {
    if (entryFor) entryFor->setValue(entryFor->getValueFromText(entry.getText()), juce::sendNotificationSync);
  };
  addChildComponent(entry);
  addMouseListener(this, true);       // right-click on a knob = type a value


  skinButton.setLookAndFeel(look.get());
  skinButton.setComponentID("logo");
  skinButton.setWantsKeyboardFocus(false);
  skinButton.setTooltip("Click the logo to change the look  (now: " + juce::String(themes[theme].name) + ")");
  skinButton.onClick = [this] {
    theme = (theme + 1) % numThemes;
    proc.setTheme(theme);
    applyTheme(theme);
    skinButton.setTooltip("Click the logo to change the look  (now: " + juce::String(themes[theme].name) + ")");
    repaint();
  };
  addAndMakeVisible(skinButton);
  addAndMakeVisible(spectrum);

  band.sendInitialUpdate();
  scBand.sendInitialUpdate();
  resized();
  startTimerHz(30);
}

SkellAudioProcessorEditor::~SkellAudioProcessorEditor() {
  stopTimer();
  for (auto* c : std::initializer_list<juce::Component*> { &amount, &smooth, &pan, &attack, &release, &band.slider, &scBand.slider, &freeze, &power, &listen, &msToggle, &bandToggle, &skinButton })
    c->setLookAndFeel(nullptr);
}

void SkellAudioProcessorEditor::mouseDown(const juce::MouseEvent& e) {
  if (!e.mods.isPopupMenu()) return;
  if (auto* s = dynamic_cast<juce::Slider*>(e.eventComponent); s != nullptr && s->isRotary()) beginTypedEntry(*s);
}

void SkellAudioProcessorEditor::beginTypedEntry(juce::Slider& s) {
  entryFor = &s;
  entry.setColour(juce::Label::textColourId, kBone);
  entry.setColour(juce::Label::backgroundColourId, kBg);
  entry.setColour(juce::Label::outlineColourId, kAccent);
  entry.setColour(juce::TextEditor::textColourId, kBone);
  entry.setColour(juce::TextEditor::backgroundColourId, kBg);
  entry.setColour(juce::TextEditor::highlightColourId, kAccent.withAlpha(0.4f));
  entry.setColour(juce::TextEditor::highlightedTextColourId, kBone);
  entry.setColour(juce::TextEditor::outlineColourId, kAccent);
  entry.setFont(makeFont(13.0f * scale(), true, 0.02f));
  entry.setEditable(true, true, false);
  entry.setText(s.getTextFromValue(s.getValue()), juce::dontSendNotification);
  entry.setBounds(s.getBounds().reduced(juce::roundToInt(10.0f * scale()), juce::roundToInt(22.0f * scale())));
  entry.setVisible(true);
  entry.toFront(false);
  entry.showEditor();
}

void SkellAudioProcessorEditor::repaintDesign(juce::Rectangle<float> r) {
  repaint((r * scale()).getSmallestIntegerContainer().expanded(1));
}

void SkellAudioProcessorEditor::showSidechainFilter(bool show) {
  scView = show;
  band.slider.setVisible(!show);
  scBand.slider.setVisible(show);
  repaintDesign({ 14.0f, 368.0f, 522.0f, 70.0f });
}

void SkellAudioProcessorEditor::timerCallback() {
  spectrum.tick();
  const bool a = proc.isSidechainActive();
  if (a != scActive) scActive = a;
  repaintDesign({ 0.0f, 48.0f, (float) designWidth, 14.0f });     // header meter
  repaintDesign({ 200.0f, 68.0f, 330.0f, 18.0f });               // status line
  repaintDesign({ 14.0f, 336.0f, 522.0f, 16.0f });               // knob labels (pan mode)
}

void SkellAudioProcessorEditor::paint(juce::Graphics& g) {
  applyTheme(theme);
  g.fillAll(kBg);
  g.addTransform(juce::AffineTransform::scale(scale()));

  // header
  drawSpacerMark(g, 16.0f, 11.0f, kBone, kAccent, kBg);
  {
    const float tx = 100.0f, ty = 9.0f, tw = textWidth(makeFont(19.0f, true, 0.16f), "SKELL");
    drawText(g, "SKELL", { tx, ty, 120.0f, 26.0f }, 19.0f, true, kBone, juce::Justification::centredLeft, 0.16f);
    drawText(g, "SPACER", { tx + tw, ty, 140.0f, 26.0f }, 19.0f, true, kAccent, juce::Justification::centredLeft, 0.16f);
  }
  drawText(g, "SPACE / DUCK / SIDECHAIN", { 101.0f, 33.0f, 172.0f, 14.0f }, 10.0f, false, kBoneDim,
           juce::Justification::centredLeft, 0.18f);

  // header strip: segments light with the deepest cut (gain reduction)
  {
    const float y = 54.0f;
    const int n = 48;
    const float slot = (float) (designWidth - 28) / (float) n;
    const int lit = juce::jlimit(0, n, juce::roundToInt(spectrum.maxCutDb() / skell::ZeroLatencyEngine::kMaxCutDb * (float) n));
    for (int i = 0; i < n; ++i) {
      const bool on = i < lit;
      const float h = on ? 3.4f : 2.0f;
      g.setColour(on ? kAccent.withAlpha(0.55f + 0.45f * (float) i / (float) n) : kBoneFaint.withAlpha(0.9f));
      g.fillRoundedRectangle(14.0f + slot * (float) i, y + 3.0f - h * 0.5f, slot - 2.2f, h, 1.0f);
    }
  }

  // spectrum card
  drawCard(g, { 14.0f, 66.0f, 522.0f, 184.0f });
  drawText(g, "SPECTRUM", { 30.0f, 72.0f, 120.0f, 14.0f }, 10.5f, true, kBoneDim, juce::Justification::centredLeft, 0.2f);
  const auto status = scActive ? juce::String("SIDECHAIN OK") : juce::String("NO SIDECHAIN");
  drawText(g, status, { 330.0f, 72.0f, 192.0f, 14.0f }, 10.5f, true, scActive ? kAccent : kWarn, juce::Justification::centredRight, 0.2f);
  drawText(g, juce::String(proc.getCurrentSampleRate() / 1000.0, 1) + " kHz   /   ZERO LATENCY",
           { 150.0f, 72.0f, 190.0f, 14.0f }, 10.0f, false, kBoneDim, juce::Justification::centredLeft, 0.1f);

  // controls card
  drawCard(g, { 14.0f, 256.0f, 522.0f, 104.0f });
  const float cellW = 522.0f / 5.0f;
  const bool msOn = proc.parameters.getRawParameterValue("ms")->load() > 0.5f;
  const juce::String panName = msOn ? "PAN  M / S" : "PAN  L / R";
  const char* names[5] = { "AMOUNT", "SMOOTH", panName.toRawUTF8(), "ATTACK", "RELEASE" };
  for (int i = 0; i < 5; ++i)
    drawText(g, names[i], { 14.0f + cellW * (float) i, 336.0f, cellW, 16.0f }, 10.5f, true, kBoneDim, juce::Justification::centred, 0.2f);
  g.setColour(kLine);
  for (int i = 1; i < 5; ++i) g.fillRect(14.0f + cellW * (float) i, 268.0f, 1.0f, 80.0f);

  // band / sidechain-filter card: the title row is a two-way switch
  drawCard(g, { 14.0f, 368.0f, 522.0f, 70.0f });
  auto& vis = scView ? scBand : band;
  drawText(g, scView ? "HIGH-PASS" : "LOW CUT", { 30.0f, 374.0f, 90.0f, 14.0f }, 10.5f, true, kBoneDim, juce::Justification::centredLeft, 0.2f);
  drawText(g, vis.lowP.getCurrentValueAsText(), { scView ? 122.0f : 92.0f, 374.0f, 100.0f, 14.0f }, 11.5f, true, kBone, juce::Justification::centredLeft);
  drawText(g, scView ? "LOW-PASS" : "HIGH CUT", { 450.0f, 374.0f, 72.0f, 14.0f }, 10.5f, true, kBoneDim, juce::Justification::centredRight, 0.2f);
  drawText(g, vis.highP.getCurrentValueAsText(), { 340.0f, 374.0f, 100.0f, 14.0f }, 11.5f, true, kBone, juce::Justification::centredRight);
  // the title is the switch: BAND <-> SIDECHAIN FILTER (brighter on hover; a dot marks an active filter while it is hidden)
  const bool scFilterOn = scBand.lowP.getValue() > 0.0f || scBand.highP.getValue() < 1.0f;
  const juce::String title = scView ? "SIDECHAIN FILTER" : "BAND";
  drawText(g, title, { 150.0f, 374.0f, 250.0f, 14.0f }, 10.5f, true, bandToggle.isMouseOver() ? kAccent : kBone, juce::Justification::centred, 0.2f);
  if (!scView && scFilterOn) {
    g.setColour(kAccent);
    const float cx = 275.0f + textWidth(makeFont(10.5f, true, 0.2f), "BAND") * 0.5f + 8.0f;   // design-space coordinates
    g.fillEllipse(cx - 2.5f, 381.0f - 2.5f, 5.0f, 5.0f);
  }

  drawText(g, "v" + juce::String(JucePlugin_VersionString), { 14.0f, 440.0f, 80.0f, 10.0f }, 8.5f, false, kBoneFaint, juce::Justification::centredLeft);
}

void SkellAudioProcessorEditor::resized() {
  const float s = scale();
  auto fit = [s](float x, float y, float w, float h) {
    return juce::Rectangle<int>(juce::roundToInt(x * s), juce::roundToInt(y * s), juce::roundToInt(w * s), juce::roundToInt(h * s));
  };
  skinButton.setBounds(fit(10.0f, 6.0f, 84.0f, 38.0f));
  power.setBounds(fit(506.0f, 10.0f, 30.0f, 30.0f));          // top-right corner
  listen.setBounds(fit(468.0f, 10.0f, 30.0f, 30.0f));
  freeze.setBounds(fit(404.0f, 15.0f, 56.0f, 20.0f));
  spectrum.setBounds(fit(28.0f, 90.0f, 494.0f, 152.0f));
  const float cellW = 522.0f / 5.0f, kd = 62.0f;
  juce::Slider* knobs[5] = { &amount, &smooth, &pan, &attack, &release };
  for (int i = 0; i < 5; ++i)
    knobs[i]->setBounds(fit(14.0f + cellW * ((float) i + 0.5f) - kd * 0.5f, 270.0f, kd, kd));
  msToggle.setBounds(fit(14.0f + cellW * 2.0f + 4.0f, 334.0f, cellW - 8.0f, 20.0f));
  band.slider.setBounds(fit(26.0f, 392.0f, 498.0f, 34.0f));
  scBand.slider.setBounds(fit(26.0f, 392.0f, 498.0f, 34.0f));
  bandToggle.setBounds(fit(200.0f, 371.0f, 150.0f, 20.0f));
}
