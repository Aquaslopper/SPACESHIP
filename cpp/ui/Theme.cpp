// ============================================================================
//  Theme.cpp — five calm palettes.
//  ---------------------------------------------------------------------------
//  The reference's five themes all sat on the same parchment and only re-tinted
//  the chart ink. Phase 9 replaces them with five complete, low-frequency
//  palettes: every one is a light paper with muted panels, deep low-chroma ink
//  and one soft accent, and none of them is dark or highly saturated. The ids
//  are unchanged so old saves keep their choice; only the colours and the
//  labels move.
//
//  Each palette is authored as six colours and derived once: panels darken by a
//  hair per level, borders are the panel colour with a little ink, and the
//  muted ink is the text colour blended towards the paper.
// ============================================================================
#include "ui/Theme.h"

#include <cmath>
#include <cstdio>

namespace ship {

namespace {

Rgb Hex(const char* hex) {
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return 0;
  };
  Rgb rgb;
  if (hex == nullptr || hex[0] != '#' || hex[1] == '\0') { return rgb; }
  rgb.r = nibble(hex[1]) * 16 + nibble(hex[2]);
  rgb.g = nibble(hex[3]) * 16 + nibble(hex[4]);
  rgb.b = nibble(hex[5]) * 16 + nibble(hex[6]);
  return rgb;
}

Rgb Mix(const Rgb& a, const Rgb& b, double towardsB) {
  const auto blend = [towardsB](int x, int y) {
    return static_cast<int>(std::lround(static_cast<double>(x) * (1.0 - towardsB) +
                                        static_cast<double>(y) * towardsB));
  };
  return Rgb{blend(a.r, b.r), blend(a.g, b.g), blend(a.b, b.b)};
}

Rgb Darken(const Rgb& colour, int amount) {
  return Rgb{colour.r > amount ? colour.r - amount : 0,
             colour.g > amount ? colour.g - amount : 0,
             colour.b > amount ? colour.b - amount : 0};
}

// One authored palette: paper, panel, ink, accent. Everything else is derived.
Palette FromFour(const char* paper, const char* panel, const char* ink,
                 const char* accent) {
  Palette p;
  const Rgb paperRgb = Hex(paper);
  const Rgb panelRgb = Hex(panel);
  const Rgb inkRgb = Hex(ink);
  const Rgb accentRgb = Hex(accent);
  p.white = Hex("#ffffff");
  p.panel = paperRgb;
  p.panel2 = panelRgb;
  p.panel3 = Darken(panelRgb, 6);
  p.border = Darken(panelRgb, 14);
  p.borderDark = Mix(Darken(panelRgb, 30), inkRgb, 0.25);
  p.coffee = accentRgb;
  p.coffeeDark = Mix(accentRgb, inkRgb, 0.35);
  p.coffeeLight = Mix(accentRgb, paperRgb, 0.45);
  p.ink = inkRgb;
  p.muted = Mix(inkRgb, paperRgb, 0.45);
  p.terracotta = Mix(accentRgb, inkRgb, 0.15);
  p.sage = accentRgb;
  p.amber = Mix(accentRgb, paperRgb, 0.25);
  return p;
}

Palette PaletteForId(const std::string& id) {
  if (id == "void") {          // Dusty Blue
    return FromFour("#f0f3f6", "#e2e8ee", "#2f3740", "#587085");
  }
  if (id == "aurora") {        // Greenish Tea
    return FromFour("#f2f5ee", "#e6ece0", "#333d32", "#5c7356");
  }
  if (id == "nebula") {        // Pressed Botanical
    return FromFour("#f4f5f0", "#e7e9e0", "#363830", "#7a8464");
  }
  if (id == "ember") {         // Oat & Linen
    return FromFour("#f7f3eb", "#ebe3d5", "#3d352e", "#8c6d46");
  }
  // Flight Deck: warm parchment and espresso.
  return FromFour("#faf7f2", "#f4efe6", "#6b5338", "#8c6f4e");
}

}  // namespace

float Theme::FontPxForStep(int step) {
  const int clamped = step < 0 ? 0 : (step > 3 ? 3 : step);
  // 12 px HTA baseline, then +5% per optional reading-size step: 12..14 px.
  return kFontFloorPx + static_cast<float>(std::lround(0.05 * kFontFloorPx * clamped));
}

float Theme::LineHeightForPx(float px) {
  // A 1.32 line box, rounded: 24 px at the floor, which is what keeps the
  // rows, the feed and the status bar from clipping as the steps grow.
  return static_cast<float>(std::lround(px * 1.32));
}

const std::vector<ThemeSpec>& Theme::Specs() {
  static const std::vector<ThemeSpec> specs = {
      // Phase 9: five calm, low-frequency palettes. The ids are the reference's
      // so a save keeps its choice; the labels name the colour, not the theme
      // it used to be.
      {"classic", "Flight Deck - warm parchment", {0x6b, 0x53, 0x38}},
      {"aurora", "Greenish Tea - matcha paper", {0x33, 0x3d, 0x32}},
      {"void", "Dusty Blue - overcast slate", {0x2f, 0x37, 0x40}},
      {"ember", "Oat & Linen - unbleached", {0x3d, 0x35, 0x2e}},
      {"nebula", "Pressed Botanical - celadon", {0x36, 0x38, 0x30}},
  };
  return specs;
}

Theme Theme::ById(const std::string& id) {
  Theme theme;
  theme.palette = PaletteForId("classic");
  for (const ThemeSpec& spec : Specs()) {
    if (spec.id == id) {
      theme.id = spec.id;
      theme.label = spec.label;
      theme.ink = spec.ink;
      theme.palette = PaletteForId(spec.id);
      return theme;
    }
  }
  // Unknown ids fall back to classic rather than to an unstyled interface.
  const ThemeSpec& fallback = Specs().front();
  theme.id = fallback.id;
  theme.label = fallback.label;
  theme.ink = fallback.ink;
  theme.palette = PaletteForId(fallback.id);
  return theme;
}

std::string Theme::Ink(float alpha) const {
  char digits[16];
  std::snprintf(digits, sizeof(digits), "%.3f", static_cast<double>(alpha));
  std::string value(digits);
  // Trim the padding so 0.13 reads as "0.13", not "0.130" - the stylesheet's
  // hand-written alphas all stop early.
  while (value.size() > 1 && value.back() == '0') { value.pop_back(); }
  if (!value.empty() && value.back() == '.') { value.pop_back(); }

  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "rgba(%d,%d,%d,%s)", ink.r, ink.g, ink.b,
                value.c_str());
  return buffer;
}

}  // namespace ship
