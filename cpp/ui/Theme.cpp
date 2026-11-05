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
//  Every role is a literal from HTA lines 1–2000. Distinct CSS surfaces
//  stay distinct; semantic red/green/amber are not replaced by the accent.
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
  // HTA 1301–1457: literal CSS roles. Never blend or darken authored colours.
  Palette p = ClassicPalette();
  if (id == "void") {
    p.panel = Hex("#152437");
    p.panel2 = Hex("#1c2e42");
    p.panel3 = Hex("#172638");
    p.border = Hex("#2e465d");
    p.borderDark = Hex("#41627c");
    p.coffee = Hex("#65d7ed");
    p.ink = Hex("#dce8f2");
    p.muted = Hex("#9bb1c1");
    p.content = Hex("#0b1320");
    p.chrome = Hex("#101d2c");
    p.hover = Hex("#263c52");
    p.control = Hex("#1c2e42");
    p.input = Hex("#0f1d2c");
    p.tabInk = Hex("#a9bdca");
    p.activeInk = Hex("#e5f2fb");
    p.chart = Hex("#0e1a2b");
    p.chartInk = Hex("#a9c5d5");
    p.titleBackground = Hex("#0b1320");
    p.titleDialog = Hex("#152437");
    p.titleBorder = Hex("#65d7ed");
    p.titleInk = Hex("#e5f2fb");
    p.titleMenu = Hex("#101d2c");
    p.titleMenuInk = Hex("#dce8f2");
    p.titleMenuBorder = Hex("#41627c");
    p.titleMenuHover = Hex("#263c52");
    return p;
  }
  if (id == "aurora") {
    p.panel = Hex("#162d2a");
    p.panel2 = Hex("#1c3934");
    p.panel3 = Hex("#18312e");
    p.border = Hex("#345b51");
    p.borderDark = Hex("#477466");
    p.coffee = Hex("#7cddb4");
    p.ink = Hex("#e2f1e9");
    p.muted = Hex("#9fbdb0");
    p.content = Hex("#0b1717");
    p.chrome = Hex("#102221");
    p.hover = Hex("#29463f");
    p.control = Hex("#1c3934");
    p.input = Hex("#102321");
    p.tabInk = Hex("#b0c9be");
    p.activeInk = Hex("#e8f6ee");
    p.chart = Hex("#10201e");
    p.chartInk = Hex("#b5d8c8");
    p.titleBackground = Hex("#0b1717");
    p.titleDialog = Hex("#162d2a");
    p.titleBorder = Hex("#7cddb4");
    p.titleInk = Hex("#e8f6ee");
    p.titleMenu = Hex("#102221");
    p.titleMenuInk = Hex("#e2f1e9");
    p.titleMenuBorder = Hex("#477466");
    p.titleMenuHover = Hex("#29463f");
    return p;
  }
  if (id == "nebula") {
    p.panel = Hex("#281f3b");
    p.panel2 = Hex("#332748");
    p.panel3 = Hex("#2a2040");
    p.border = Hex("#52436d");
    p.borderDark = Hex("#665385");
    p.coffee = Hex("#c8a3ff");
    p.ink = Hex("#eee6f7");
    p.muted = Hex("#c0b0cf");
    p.content = Hex("#130f20");
    p.chrome = Hex("#201831");
    p.hover = Hex("#413256");
    p.control = Hex("#332748");
    p.input = Hex("#21182f");
    p.tabInk = Hex("#c3b4d3");
    p.activeInk = Hex("#f4eafd");
    p.chart = Hex("#1a1429");
    p.chartInk = Hex("#d2bce6");
    p.titleBackground = Hex("#130f20");
    p.titleDialog = Hex("#281f3b");
    p.titleBorder = Hex("#c8a3ff");
    p.titleInk = Hex("#f4eafd");
    p.titleMenu = Hex("#201831");
    p.titleMenuInk = Hex("#eee6f7");
    p.titleMenuBorder = Hex("#665385");
    p.titleMenuHover = Hex("#413256");
    return p;
  }
  if (id == "ember") {
    p.panel = Hex("#30231d");
    p.panel2 = Hex("#3b2c23");
    p.panel3 = Hex("#34261f");
    p.border = Hex("#654a36");
    p.borderDark = Hex("#835d42");
    p.coffee = Hex("#ffba76");
    p.ink = Hex("#f5e7d9");
    p.muted = Hex("#c3a78f");
    p.content = Hex("#1a1210");
    p.chrome = Hex("#251b17");
    p.hover = Hex("#493326");
    p.control = Hex("#3b2c23");
    p.input = Hex("#291d18");
    p.tabInk = Hex("#d2b59a");
    p.activeInk = Hex("#fff0df");
    p.chart = Hex("#241915");
    p.chartInk = Hex("#e2c3a5");
    p.titleBackground = Hex("#1a1210");
    p.titleDialog = Hex("#30231d");
    p.titleBorder = Hex("#ffba76");
    p.titleInk = Hex("#fff0df");
    p.titleMenu = Hex("#251b17");
    p.titleMenuInk = Hex("#f5e7d9");
    p.titleMenuBorder = Hex("#835d42");
    p.titleMenuHover = Hex("#493326");
    return p;
  }
  return p;  // classic, including unknown ids
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
