// ============================================================================
//  Theme.h — the CSS roles, as data.
//  ---------------------------------------------------------------------------
//  The reference has one light parchment theme (classic) and four dark themes
//  (void, aurora, nebula, ember) that re-tint every surface, not just the ink.
//  The port mirrors that split: one authored light palette, four authored dark
//  palettes, each with its own surface colours and ink.
//
//  Everything here is presentation. No function in this file can advance the
//  voyage, and nothing here is saved (the voyage saves the theme's NAME).
// ============================================================================
#pragma once

#include <array>
#include <string>
#include <vector>

namespace ship {

struct Rgb {
  int r = 0;
  int g = 0;
  int b = 0;
};

// The palette the interface is drawn with. Names follow the stylesheet's roles
// rather than their colours, so a reviewer can compare the two editions.
struct Palette {
  Rgb white;
  Rgb panel;
  Rgb panel2;
  Rgb panel3;
  Rgb border;
  Rgb borderDark;
  Rgb coffee;
  Rgb coffeeDark;
  Rgb coffeeLight;
  Rgb ink;
  Rgb muted;
  Rgb terracotta;
  Rgb sage;
  Rgb amber;
  Rgb titleBackground, titleDialog, titleBorder, titleInk;
  // Separate source roles: chrome is not a panel; content is not an active tab
  // texture tint; menu colours are overridden independently in dark themes.
  Rgb content, chrome, hover, control, input, tabInk, activeInk, chart, chartInk;
  Rgb titleMenu, titleMenuInk, titleMenuBorder, titleMenuHover;
};

// One of THEMES: a name, a label for the settings screen, and the ink that the
// chart raster and every accent are mixed from.
struct ThemeSpec {
  std::string id;
  std::string label;
  Rgb ink;
};

struct Theme {
  std::string id = "classic";
  std::string label;
  Rgb ink;
  Palette palette;

  // Presentation flags that travel with the voyage's options.
  bool largeText = false;
  // Phase 9: the typography floor and its four steps. Step 0 is 18 px — the
  // smallest the port ever draws — and each step adds 5% of the floor, rounded.
  int fontStep = 0;
  bool hideCrewLabels = false;
  bool reducedMotion = false;

  // The compact deck: a Wren is drawn at 600x365, a Kestrel at 1160x460.
  bool stageCompact = false;

  // The five authored themes, in the reference's order.
  static const std::vector<ThemeSpec>& Specs();
  static Theme ById(const std::string& id);

  // "rgba(139,111,78,0.13)" as the stylesheet writes it, for the raster hairlines.
  std::string Ink(float alpha) const;

  // Native readability floor requested for the port. No rendered label should
  // fall below 18 px; the four settings steps may increase it further.
  static constexpr float kFontFloorPx = 18.0f;
  static float FontPxForStep(int step);
  static float LineHeightForPx(float px);
  float FontPx() const {
    return FontPxForStep(fontStep) + (largeText ? 2.0f : 0.0f);
  }
  float BodySize() const { return FontPx(); }
  float LineHeight() const { return LineHeightForPx(FontPx()); }
};

}  // namespace ship
