#pragma once
// Skell house look: warm "ossuary" black, bone-white type, one accent colour,
// flat square controls, corner-bracket cards, four switchable skins
// (Bone, Neon, Graveyard, Clean).
#include <JuceHeader.h>

namespace skelltheme {

inline juce::Colour kBg, kPanel, kPanelHi, kLine, kBone, kBoneDim, kBoneFaint, kAccent, kOnAccent, kBtnHover, kBtnDown;
const juce::Colour kWarn { 0xffe58a6b };

struct ThemeDef { const char* name; unsigned bg, panel, panelHi, line, bone, boneDim, boneFaint, accent, onAccent, btnHover, btnDown; };
inline const ThemeDef themes[] = {
  {"Bone",      0xff0b0a09, 0xff131210, 0xff1b1915, 0xff2f2b23, 0xffe9e1cc, 0xff9b937d, 0xff4d483c, 0xffb6f56a, 0xff0b0c08, 0xff26231c, 0xff3a4a22},
  {"Neon",      0xff07060f, 0xff0f0c1c, 0xff191330, 0xff33285c, 0xffeae6ff, 0xff9d95c9, 0xff463e75, 0xff2ff3e0, 0xff04130f, 0xff241b45, 0xff0f5a55},
  {"Graveyard", 0xff0a0d0b, 0xff111611, 0xff1a211a, 0xff2c382c, 0xffd7dccb, 0xff8e9a86, 0xff424b3f, 0xffff9a3c, 0xff160c02, 0xff222b22, 0xff6b3f12},
  {"Clean",     0xfff3f0e8, 0xffeae6da, 0xffdcd7c8, 0xffc2bba8, 0xff1f1d18, 0xff6a6556, 0xffb9b2a0, 0xff3a8a1e, 0xfff7f5ee, 0xffe0dbcb, 0xffb9d6a6},
};
constexpr int numThemes = 4;
inline int appliedTheme = -1;

inline void applyTheme(int index) {
  index = juce::jlimit(0, numThemes - 1, index);
  if (index == appliedTheme) return;
  appliedTheme = index;
  const auto& t = themes[index];
  kBg = juce::Colour(t.bg); kPanel = juce::Colour(t.panel); kPanelHi = juce::Colour(t.panelHi); kLine = juce::Colour(t.line);
  kBone = juce::Colour(t.bone); kBoneDim = juce::Colour(t.boneDim); kBoneFaint = juce::Colour(t.boneFaint);
  kAccent = juce::Colour(t.accent); kOnAccent = juce::Colour(t.onAccent);
  kBtnHover = juce::Colour(t.btnHover); kBtnDown = juce::Colour(t.btnDown);
}

inline juce::Font makeFont(float size, bool bold = false, float kerning = 0.0f) {
  juce::Font f(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
  return kerning != 0.0f ? f.withExtraKerningFactor(kerning) : f;
}

inline float textWidth(const juce::Font& f, const juce::String& s) {
  juce::GlyphArrangement ga;
  ga.addLineOfText(f, s, 0.0f, 0.0f);
  return ga.getBoundingBox(0, -1, true).getWidth();
}

inline void drawText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float size, bool bold,
                     juce::Colour colour, juce::Justification j = juce::Justification::centredLeft, float kerning = 0.0f) {
  g.setColour(colour);
  g.setFont(makeFont(size, bold, kerning));
  g.drawText(text, r, j, true);
}

// A card with corner "bone brackets" instead of a heavy frame.
inline void drawCard(juce::Graphics& g, juce::Rectangle<float> r) {
  g.setColour(kPanel); g.fillRect(r);
  g.setColour(kLine);  g.drawRect(r, 1.0f);
  const float L = 9.0f;
  g.setColour(kBoneDim.withAlpha(0.75f));
  const auto a = r.getTopLeft(), b = r.getTopRight(), c = r.getBottomLeft(), d = r.getBottomRight();
  juce::Path p;
  p.startNewSubPath(a.x, a.y + L); p.lineTo(a.x, a.y); p.lineTo(a.x + L, a.y);
  p.startNewSubPath(b.x - L, b.y); p.lineTo(b.x, b.y); p.lineTo(b.x, b.y + L);
  p.startNewSubPath(c.x, c.y - L); p.lineTo(c.x, c.y); p.lineTo(c.x + L, c.y);
  p.startNewSubPath(d.x - L, d.y); p.lineTo(d.x, d.y); p.lineTo(d.x, d.y - L);
  g.strokePath(p, juce::PathStrokeType(1.4f));
}

// Skeleton spacer mark, about 74 x 30 units: a skull and a spine whose vertebrae
// are held apart, with the carved-out space marked in the accent colour.
inline void drawSpacerMark(juce::Graphics& g, float x, float y, juce::Colour bone, juce::Colour accent, juce::Colour hole) {
  g.setColour(bone);
  g.fillEllipse(x + 1.0f, y + 3.0f, 22.0f, 20.0f);                       // cranium
  g.fillRoundedRectangle(x + 6.0f, y + 19.0f, 12.0f, 8.0f, 2.5f);        // jaw
  g.setColour(hole);
  g.fillEllipse(x + 5.5f, y + 10.0f, 5.6f, 6.2f);                        // eye sockets
  g.fillEllipse(x + 13.2f, y + 10.0f, 5.6f, 6.2f);
  juce::Path nose; nose.addTriangle(x + 12.0f, y + 16.5f, x + 10.6f, y + 19.5f, x + 13.4f, y + 19.5f);
  g.fillPath(nose);
  for (int i = 0; i < 2; ++i) g.fillRect(x + 9.8f + 3.2f * (float) i, y + 22.5f, 0.9f, 4.5f);   // teeth gaps

  static const float h[8] = { 13.f, 16.f, 14.f, 0.f, 0.f, 14.f, 16.f, 13.f };
  const float cy = y + 15.0f, step = 5.6f, w = 4.0f, x0 = x + 27.0f;
  for (int i = 0; i < 8; ++i) {
    if (h[i] <= 0.0f) continue;
    g.setColour(bone);
    g.fillRoundedRectangle(x0 + step * (float) i, cy - h[i] * 0.5f, w, h[i], 1.4f);   // vertebra
  }
  g.setColour(accent);                                                    // the space between
  const float gl = x0 + step * 3.0f - 1.6f, gr = x0 + step * 5.0f - 1.8f;
  g.fillRect(gl, cy - 9.0f, 1.4f, 18.0f);
  g.fillRect(gr, cy - 9.0f, 1.4f, 18.0f);
  g.fillRect(gl + 1.4f, cy - 0.7f, gr - gl - 1.4f, 1.4f);
}

} // namespace skelltheme
