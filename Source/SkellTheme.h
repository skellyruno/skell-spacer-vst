#pragma once
// Skell house look, shared with Skell Key: warm "ossuary" black, bone-white type,
// one accent colour, flat square controls, corner-bracket cards, skull-key mark,
// four switchable skins (Bone, Neon, Graveyard, Clean).
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

// Skull-bowed skeleton key, about 74 x 30 units.
inline void drawKeyMark(juce::Graphics& g, float x, float y, juce::Colour bone, juce::Colour hole) {
  g.setColour(bone);
  g.fillEllipse(x + 1.0f, y + 0.0f, 25.0f, 23.0f);
  g.fillRoundedRectangle(x + 6.5f, y + 17.0f, 14.0f, 11.0f, 3.0f);
  g.fillRoundedRectangle(x + 25.0f, y + 10.5f, 48.0f, 4.2f, 1.6f);
  g.fillRoundedRectangle(x + 33.0f, y + 7.0f, 3.2f, 11.5f, 1.2f);
  g.fillRect(x + 56.0f, y + 14.0f, 4.2f, 9.0f);
  g.fillRect(x + 63.0f, y + 14.0f, 4.2f, 6.0f);
  g.fillRect(x + 69.5f, y + 14.0f, 3.5f, 10.0f);
  g.setColour(hole);
  g.fillEllipse(x + 6.2f, y + 8.4f, 6.4f, 6.8f);
  g.fillEllipse(x + 14.6f, y + 8.4f, 6.4f, 6.8f);
  juce::Path nose; nose.addTriangle(x + 13.5f, y + 15.0f, x + 11.6f, y + 19.0f, x + 15.4f, y + 19.0f);
  g.fillPath(nose);
  for (int i = 0; i < 3; ++i) g.fillRect(x + 9.6f + 3.4f * (float) i, y + 22.0f, 1.0f, 5.0f);
}

} // namespace skelltheme
