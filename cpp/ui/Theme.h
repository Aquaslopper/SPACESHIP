// ============================================================================
//  Theme.h — the CSS roles, as data.
//  ---------------------------------------------------------------------------
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
  Rgb content, chrome, hover, control, input, tabInk, activeInk, chart, chartInk;
  Rgb titleMenu, titleMenuInk, titleMenuBorder, titleMenuHover;
  Rgb progressFill;
};


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
  int fontStep = 0;
  bool hideCrewLabels = false;
  bool reducedMotion = false;


  bool stageCompact = false;

 
  static const std::vector<ThemeSpec>& Specs();
  static Theme ById(const std::string& id);


  std::string Ink(float alpha) const;

 
  static constexpr float kFontFloorPx = 18.0f;
  static float FontPxForStep(int step);
  static float LineHeightForPx(float px);
  float FontPx() const {
    return FontPxForStep(fontStep) + (largeText ? 2.0f : 0.0f);
  }

  int TextPx(int reference) const;
  float SidebarPx() const { return FontPx() - 2.0f; }
  int HomeInfoLabelPx() const;
  int HomeInfoValuePx() const;
  float BodySize() const { return FontPx(); }
  float LineHeight() const { return LineHeightForPx(FontPx()); }
};

}  // namespace ship
