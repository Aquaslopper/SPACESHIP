// ============================================================================
//  Theme.cpp — five calm palettes.
//  ---------------------------------------------------------------------------
//  The reference has one light parchment theme (classic) and four dark themes
//  (void, aurora, nebula, ember). The classic theme keeps the parchment palette;
//  the four dark themes each author their own surface colours, borders, inks
//  and accents, mirroring the HTA's theme-<id> CSS blocks. The ids are
//  unchanged so old saves keep their choice; only the colours and the labels
//  move.
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
  // FromFour(paper, panel, ink, accent): paper→panel bg, panel→body bg,
  // ink→text/chart ink, accent→highlight. Mapping mirrors the HTA's
  // theme-<id> CSS blocks.
  // classic: Flight Deck - parchment (the HTA's default, light theme)
  if (id == "classic") {
    return FromFour("#faf7f2", "#f4efe6", "#6b5338", "#8c6f4e");
  }
  // void: Deep Space - cyan (dark theme)
  if (id == "void") {
    return FromFour("#152437", "#0b1320", "#dce8f2", "#65d7ed");
  }
  // aurora: Aurora - mint (dark theme)
  if (id == "aurora") {
    return FromFour("#162d2a", "#0b1717", "#e2f1e9", "#7cddb4");
  }
  // nebula: Nebula - violet (dark theme)
  if (id == "nebula") {
    return FromFour("#281f3b", "#130f20", "#eee6f7", "#c8a3ff");
  }
  // ember: Ember - copper (dark theme)
  if (id == "ember") {
    return FromFour("#30231d", "#1a1210", "#f5e7d9", "#ffba76");
  }
  // Unknown ids fall back to classic.
  return FromFour("#faf7f2", "#f4efe6", "#6b5338", "#8c6f4e");
}

}  // namespace

float Theme::FontPxForStep(int step) {
  const int clamped = step < 0 ? 0 : (step > 3 ? 3 : step);
  // 18 px minimum, then +5% per optional reading-size step: 18..21 px.
  return kFontFloorPx + static_cast<float>(std::lround(0.05 * kFontFloorPx * clamped));
}

float Theme::LineHeightForPx(float px) {
  // A 1.32 line box, rounded: 24 px at the floor, which is what keeps the
  // rows, the feed and the status bar from clipping as the steps grow.
  return static_cast<float>(std::lround(px * 1.32));
}

const std::vector<ThemeSpec>& Theme::Specs() {
  static const std::vector<ThemeSpec> specs = {
      // The HTA's five themes, in the reference's order. The ids are the
      // reference's so a save keeps its choice; the labels match the HTA.
      {"classic", "Flight Deck - parchment", {0x6b, 0x53, 0x38}},
      {"void", "Deep Space - cyan", {0x5b, 0xb8, 0xd5}},
      {"aurora", "Aurora - mint", {0x5b, 0xbe, 0x9d}},
      {"nebula", "Nebula - violet", {0x9b, 0x79, 0xd0}},
      {"ember", "Ember - copper", {0xd7, 0x90, 0x5b}},
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
