// ============================================================================
//  Theme.cpp — five calm palettes.
//  ---------------------------------------------------------------------------
//  Classic keeps the HTA parchment roles. The other saved theme IDs use
//  light, softly tinted surfaces rather than the HTA dark surfaces.
//
//  Classic roles are read from the HTA; semantic red/green/amber stay
//  distinct from the optional palette accents.
// ============================================================================
#include "ui/Theme.h"

#include <algorithm>
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
  p.white = Hex("#fffaf2"); p.panel = Hex("#f4e6d0");
  p.panel2 = Hex("#ead5b5"); p.panel3 = Hex("#e3c9a3");
  p.border = Hex("#cfad7d"); p.borderDark = Hex("#a97949");
  p.coffee = Hex("#8c5f37"); p.coffeeDark = Hex("#684426");
  p.coffeeLight = Hex("#b38a5e"); p.ink = Hex("#3b3025");
  p.muted = Hex("#826e58"); p.terracotta = Hex("#a65337");
  p.sage = Hex("#718052"); p.amber = Hex("#b77b32");
  p.titleBackground = Hex("#d8c4a6"); p.titleDialog = Hex("#f5ead8");
  p.titleBorder = Hex("#8e7757"); p.titleInk = Hex("#443528");
  p.content = Hex("#fffaf2"); p.chrome = Hex("#f4e6d0");
  p.hover = Hex("#f1dfc2"); p.control = Hex("#fffaf2"); p.input = Hex("#fffaf2");
  p.tabInk = Hex("#6b6157"); p.activeInk = Hex("#3b3630");
  p.chart = Hex("#ece2d1"); p.chartInk = Hex("#6b5338");
  p.titleMenu = Hex("#3f342b"); p.titleMenuInk = Hex("#f2e5cc");
  p.titleMenuBorder = Hex("#a48e72"); p.titleMenuHover = Hex("#6b543f");
  return p;
}

// A hue and a lightness is all a role needs: the optional themes keep classic's
// depths - so nothing that read on parchment fails to read here - and take their
// own hue at the same strength.
struct Hsl {
  double h = 0.0;
  double s = 0.0;
  double l = 0.0;
};

double Channel(unsigned char c) { return static_cast<double>(c) / 255.0; }

Hsl ToHsl(const Rgb& c) {
  const double r = Channel(c.r), g = Channel(c.g), b = Channel(c.b);
  const double hi = std::max({r, g, b});
  const double lo = std::min({r, g, b});
  const double d = hi - lo;
  Hsl v;
  v.l = (hi + lo) / 2.0;
  if (d <= 0.0001) { return v; }
  v.s = d / (1.0 - std::fabs(2.0 * v.l - 1.0));
  if (hi == r) { v.h = 60.0 * std::fmod((g - b) / d, 6.0); }
  else if (hi == g) { v.h = 60.0 * ((b - r) / d + 2.0); }
  else { v.h = 60.0 * ((r - g) / d + 4.0); }
  if (v.h < 0.0) { v.h += 360.0; }
  return v;
}

Rgb FromHsl(const Hsl& v) {
  const double c = (1.0 - std::fabs(2.0 * v.l - 1.0)) * v.s;
  const double x = c * (1.0 - std::fabs(std::fmod(v.h / 60.0, 2.0) - 1.0));
  const double m = v.l - c / 2.0;
  double r = 0.0, g = 0.0, b = 0.0;
  if (v.h < 60.0) { r = c; g = x; }
  else if (v.h < 120.0) { r = x; g = c; }
  else if (v.h < 180.0) { g = c; b = x; }
  else if (v.h < 240.0) { g = x; b = c; }
  else if (v.h < 300.0) { r = x; b = c; }
  else { r = c; b = x; }
  const auto byte = [m](double f) {
    const double scaled = (f + m) * 255.0;
    return static_cast<unsigned char>(std::clamp(std::lround(scaled), 0L, 255L));
  };
  Rgb out;
  out.r = byte(r);
  out.g = byte(g);
  out.b = byte(b);
  return out;
}

// Hue turned, saturation deepened, lightness untouched: the parchment's depth in
// another colour. A near-neutral stays neutral, so nothing goes muddy.
Rgb Deepen(const Rgb& c, double hue, double strength) {
  Hsl v = ToHsl(c);
  if (v.s < 0.02) { return c; }
  v.h = hue;
  v.s = std::min(0.72, v.s * strength);
  return FromHsl(v);
}

Palette PaletteForId(const std::string& id) {
  // Classic is the unchanged HTA parchment. The other four are the same depths in
  // their own colour: surfaces, borders, inks and chrome all take the theme's hue
  // at classic's strength. The semantic trio - terracotta, sage, amber - is never
  // turned, because those three mean something.
  Palette p = ClassicPalette();
  double hue = -1.0;
  if (id == "void") { hue = 196.0; }        // deep space, petrol blue
  else if (id == "aurora") { hue = 148.0; }  // mint green
  else if (id == "nebula") { hue = 284.0; }  // violet
  else if (id == "ember") { hue = 20.0; }    // copper
  if (hue < 0.0) { return p; }

  const double strength = 1.15;
  Rgb* roles[] = {&p.white,         &p.panel,        &p.panel2,       &p.panel3,
                  &p.border,        &p.borderDark,   &p.coffee,       &p.coffeeDark,
                  &p.coffeeLight,   &p.ink,          &p.muted,        &p.titleBackground,
                  &p.titleDialog,   &p.titleBorder,  &p.titleInk,     &p.content,
                  &p.chrome,        &p.hover,        &p.control,      &p.input,
                  &p.tabInk,        &p.activeInk,    &p.chart,        &p.chartInk,
                  &p.titleMenu,     &p.titleMenuInk, &p.titleMenuBorder, &p.titleMenuHover};
  for (Rgb* role : roles) { *role = Deepen(*role, hue, strength); }
  return p;
}

}  // namespace

float Theme::FontPxForStep(int step) {
  const int clamped = step < 0 ? 0 : (step > 3 ? 3 : step);
  // 18 px minimum, then +5% per optional reading-size step: 18..21 px.
  return kFontFloorPx + static_cast<float>(std::lround(0.05 * kFontFloorPx * clamped));
}

int Theme::TextPx(int reference) const {
  if (reference <= 0) return reference;
  // The minimum must scale as well: a fixed 18px minimum made most small
  // labels ignore every reading-size step and the larger-text toggle.
  return std::max(static_cast<int>(std::lround(FontPx())),
                  static_cast<int>(std::lround(reference * FontPx() / 13.0f)));
}

int Theme::HomeInfoLabelPx() const {
  return static_cast<int>(std::lround(FontPx()));
}

int Theme::HomeInfoValuePx() const {
  return static_cast<int>(std::lround(FontPx()));
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
