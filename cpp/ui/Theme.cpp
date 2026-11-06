// ============================================================================
//  Theme.cpp — five calm palettes.
//  ---------------------------------------------------------------------------
//  Classic keeps the HTA parchment roles. The other saved theme IDs use
//  light, softly tinted surfaces by request rather than the HTA dark surfaces.
//
//  Classic roles are read from the HTA; semantic red/green/amber stay
//  distinct from the optional palette accents.
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

Palette ClassicPalette() {
  // Literal Section 1 CSS roles; do not derive one role from another.
  Palette p;
  p.white = Hex("#ffffff"); p.panel = Hex("#faf7f2");
  p.panel2 = Hex("#f4efe6"); p.panel3 = Hex("#efe8dc");
  p.border = Hex("#e4dbcb"); p.borderDark = Hex("#cbb89c");
  p.coffee = Hex("#8c6f4e"); p.coffeeDark = Hex("#6b5338");
  p.coffeeLight = Hex("#b89b76"); p.ink = Hex("#3b3630");
  p.muted = Hex("#8a7f72"); p.terracotta = Hex("#b05a42");
  p.sage = Hex("#7f9161"); p.amber = Hex("#c08a3e");
  p.titleBackground = Hex("#d8c4a6"); p.titleDialog = Hex("#f5ead8");
  p.titleBorder = Hex("#8e7757"); p.titleInk = Hex("#443528");
  p.content = Hex("#ffffff"); p.chrome = Hex("#faf7f2");
  p.hover = Hex("#f6efe3"); p.control = Hex("#ffffff"); p.input = Hex("#ffffff");
  p.tabInk = Hex("#6b6157"); p.activeInk = Hex("#3b3630");
  p.chart = Hex("#ece2d1"); p.chartInk = Hex("#6b5338");
  p.titleMenu = Hex("#3f342b"); p.titleMenuInk = Hex("#f2e5cc");
  p.titleMenuBorder = Hex("#a48e72"); p.titleMenuHover = Hex("#6b543f");
  return p;
}

Palette PaletteForId(const std::string& id) {
  // Classic is the unchanged HTA parchment. The user requested light versions
  // of the other four choices; retain their IDs and accents for saved games.
  Palette p = ClassicPalette();
  const char* surface = nullptr;
  const char* raised = nullptr;
  const char* border = nullptr;
  const char* accent = nullptr;
  if (id == "void") {
    surface = "#f2f7f8"; raised = "#e7f0f2"; border = "#b8ced2"; accent = "#437b87";
  } else if (id == "aurora") {
    surface = "#f2f7f3"; raised = "#e7f0e9"; border = "#bbcec0"; accent = "#4a7965";
  } else if (id == "nebula") {
    surface = "#f7f3f8"; raised = "#eee8f2"; border = "#ccbed5"; accent = "#796487";
  } else if (id == "ember") {
    surface = "#faf5f0"; raised = "#f2e9df"; border = "#d6c2ac"; accent = "#956a4d";
  }
  if (surface != nullptr) {
    p.panel = Hex(surface); p.panel2 = Hex(raised); p.panel3 = Hex(raised);
    p.chrome = Hex(surface); p.hover = Hex(raised); p.control = Hex(surface);
    p.border = Hex(border); p.borderDark = Hex(border);
    p.coffee = Hex(accent); p.coffeeDark = Hex(accent); p.chartInk = Hex(accent);
    p.chart = Hex(raised); p.titleDialog = Hex(surface); p.titleBorder = Hex(border);
  }
  return p;
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
      {"classic", "Flight Deck - parchment", {139, 111, 78}},
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
