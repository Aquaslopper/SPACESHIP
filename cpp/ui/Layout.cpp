// ============================================================================
//  Layout.cpp — the arithmetic behind the cockpit.
// ============================================================================
#include "ui/Layout.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ship {

namespace {
float ClampF(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }
}  // namespace

float GiveFeedHeight(float contentHeight, float others) {
  const float leftover = contentHeight - others - 4.0f;
  return ClampF(leftover, Metrics::kFeedMin, Metrics::kFeedMax);
}

float GiveMapHeight(float contentHeight, float others, float mapViewWidth) {
  const float leftover = contentHeight - others - 4.0f;
  // The map may grow, but never past its authored ceiling, and never past half
  // the plot's width - beyond that the perspective raster stops reading as a
  // floor and starts reading as a wall.
  const float ceiling = std::min(Metrics::kMapH, std::round(mapViewWidth / 2.0f));
  return ClampF(leftover, Metrics::kMapParkH, std::max(Metrics::kMapParkH, ceiling));
}

Area StageRect(const Area& content, bool compactStage) {
  const float w = compactStage ? Metrics::kStageCompactW : Metrics::kStageW;
  const float h = compactStage ? Metrics::kStageCompactH : Metrics::kStageH;
  Area stage;
  stage.w = w;
  stage.h = h;
  stage.x = content.x + std::round((content.w - w) / 2.0f);
  if (stage.x < content.x) { stage.x = content.x; }
  stage.y = content.y;
  return stage;
}

ShellLayout ComputeLayout(float windowW, float windowH, float contentHeight,
                          float others, bool compactStage, float homeWidth) {
  ShellLayout out;
  // Keep the arrows their original width and let the Home slot grow to fit
  // the currently selected font. Narrow windows retain valid, bounded boxes.
  const float homeW = std::min(std::max(0.0f, windowW), std::max(Metrics::kHomeW, homeWidth));
  const float arrowW = std::min((Metrics::kControlsW - Metrics::kHomeW) / 2.0f,
                                std::max(0.0f, (windowW - homeW) / 2.0f));
  const float controlsW = homeW + 2.0f * arrowW;

  out.taskbar = Area{0.0f, 0.0f, windowW, Metrics::kTaskbarH};
  out.controls = Area{windowW - controlsW, 0.0f, controlsW, Metrics::kTaskbarH};
  out.tabStrip = Area{0.0f, 0.0f, std::max(0.0f, windowW - controlsW), Metrics::kTaskbarH};
  out.home = Area{out.controls.Right() - homeW, 0.0f, homeW, Metrics::kTaskbarH};
  out.scrollLeft = Area{out.controls.x, 0.0f, arrowW, Metrics::kTaskbarH};
  out.scrollRight = Area{out.controls.x + arrowW, 0.0f, arrowW, Metrics::kTaskbarH};

  out.sidepanel = Area{0.0f, Metrics::kSideTop, Metrics::kSideW,
                       std::max(0.0f, windowH - Metrics::kSideTop - Metrics::kSideBottom)};
  out.sidefoot = Area{0.0f, windowH - Metrics::kFootBottom - Metrics::kFootH,
                      Metrics::kSideW, Metrics::kFootH};

  out.content = Area{Metrics::kContentLeft, Metrics::kContentTop,
                     std::max(0.0f, windowW - Metrics::kContentLeft),
                     std::max(0.0f, windowH - Metrics::kContentTop - Metrics::kStatusH)};

  out.statusbar = Area{0.0f, windowH - Metrics::kStatusH, windowW, Metrics::kStatusH};

  out.stage = StageRect(out.content, compactStage);

  out.feedHeight = GiveFeedHeight(contentHeight, others);
  out.feed = Area{out.content.x + 8.0f,
                  out.stage.Bottom() + 86.0f,  // stage note + nav strip + info rail
                  std::max(0.0f, out.content.w - 16.0f), out.feedHeight};
  return out;
}

std::vector<IndicatorRect> PlaceIndicators(const std::vector<IndicatorLine>& lines) {
  std::vector<IndicatorRect> out;
  out.reserve(lines.size());
  for (std::size_t i = 0; i < lines.size(); ++i) {
    IndicatorLine ln = lines[i];
    int pct = std::max(0, std::min(100, ln.pct));
    float h = std::round(Metrics::kIndH * (static_cast<float>(pct) / 100.0f));
    if (h < 1.0f) { h = 1.0f; }
    if (h > Metrics::kIndH) { h = Metrics::kIndH; }
    const float x = static_cast<float>(i) * Metrics::kIndGap;
    IndicatorRect r;
    r.grow = ln.grow;
    // Ghost first: the empty top of the column, so a shrinking line reads as a
    // line running out and a growing one as a line climbing.
    r.ghost = Area{x, 0.0f, 2.0f, Metrics::kIndH - h};
    r.line = Area{x, Metrics::kIndH - h, 2.0f, h};
    out.push_back(r);
  }
  return out;
}

}  // namespace ship
