// ============================================================================
//  Theme.cpp — one palette, five inks.
//  ---------------------------------------------------------------------------
//  The hex values below are the stylesheet's roles, converted once. `classic`
//  is the parchment cockpit the game ships in; the other four themes only
//  re-tint the ink, exactly as the reference's THEMES table does.
// ============================================================================
#include "ui/Theme.h"

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
  Palette p;
  p.white = Hex("#ffffff");
  p.panel = Hex("#faf7f2");
  p.panel2 = Hex("#f4efe6");
  p.panel3 = Hex("#efe8dc");
  p.border = Hex("#e4dbcb");
  p.borderDark = Hex("#cbb89c");
  p.coffee = Hex("#8c6f4e");
  p.coffeeDark = Hex("#6b5338");
  p.coffeeLight = Hex("#b89b76");
  p.ink = Hex("#3b3630");
  p.muted = Hex("#8a7f72");
  p.terracotta = Hex("#b05a42");
  p.sage = Hex("#7f9161");
  p.amber = Hex("#c08a3e");
  return p;
}

}  // namespace

const std::vector<ThemeSpec>& Theme::Specs() {
  static const std::vector<ThemeSpec> specs = {
      {"classic", "Flight Deck - parchment", {139, 111, 78}},
      {"void", "Deep Space - cyan", {91, 184, 213}},
      {"aurora", "Aurora - mint", {91, 190, 157}},
      {"nebula", "Nebula - violet", {155, 121, 208}},
      {"ember", "Ember - copper", {215, 144, 91}},
  };
  return specs;
}

Theme Theme::ById(const std::string& id) {
  Theme theme;
  theme.palette = ClassicPalette();
  for (const ThemeSpec& spec : Specs()) {
    if (spec.id == id) {
      theme.id = spec.id;
      theme.label = spec.label;
      theme.ink = spec.ink;
      return theme;
    }
  }
  // Unknown ids fall back to classic rather than to an unstyled interface.
  const ThemeSpec& fallback = Specs().front();
  theme.id = fallback.id;
  theme.label = fallback.label;
  theme.ink = fallback.ink;
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
