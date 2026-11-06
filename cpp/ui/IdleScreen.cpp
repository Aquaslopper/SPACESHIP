// ============================================================================
//  IdleScreen.cpp — Home, built in the reference's own order.
// ============================================================================
#include "ui/IdleScreen.h"

#include <cmath>

#include "core/JsNumber.h"
#include "ui/Shell.h"

namespace ship {

namespace {

std::string Num(double value) { return js::NumToString(value); }

float Share(int count) {
  if (count <= 0) { return 100.0f; }
  // Math.floor(10000 / n) / 100, as the reference writes it.
  return std::floor(10000.0 / static_cast<double>(count)) / 100.0f;
}

// The height one line of text needs at the reference size, mirroring
// ShellView::TypePx exactly: an 18 px floor, then the size scales with the
// theme's font px. Used so a container's box is never shorter than the text it
// paints, at any typography step.
float TextLineHeight(const Theme& theme, int refSize) {
  return static_cast<float>(
      std::max(18, static_cast<int>(std::lround(refSize * theme.FontPx() / 13.0f))));
}

// Stat cell: pad + label + gap + value + bar zone (drawn at Bottom()-12) + pad.
// The value always ends above the bar; nothing overlaps.
float StatCellHeight(const Theme& theme) {
  const float pad = 6.0f;
  const float gap = 6.0f;
  const float bottomPad = 6.0f;
  return pad + TextLineHeight(theme, 11) + gap + TextLineHeight(theme, 20) +
         4.0f + TextLineHeight(theme, 10) + 12.0f + bottomPad;
}

// Strip (nav or dock): pad + label + gap + main + gap + right + pad.
float StripHeight(const Theme& theme) {
  const float pad = 6.0f;
  const float gap = 6.0f;
  const float bottomPad = 6.0f;
  return pad + TextLineHeight(theme, 11) + gap + TextLineHeight(theme, 13) +
         gap + TextLineHeight(theme, 10) + bottomPad;
}

// Info rail cell: pad + label + gap + value + pad.
float InfoCellHeight(const Theme& theme) {
  const float pad = 6.0f;
  const float gap = 6.0f;
  const float bottomPad = 6.0f;
  return pad + theme.HomeInfoLabelPx() + gap + theme.HomeInfoValuePx() + bottomPad;
}

}  // namespace

std::vector<RailCell> StatRail(const Simulation& sim, const Area& rail) {
  std::vector<RailCell> cells;
  const std::vector<Stat>& stats = sim.Stats();
  const float share = Share(static_cast<int>(stats.size()));
  float x = rail.x;
  const float w = rail.w * share / 100.0f;
  for (const Stat& stat : stats) {
    RailCell cell;
    cell.id = stat.id;
    cell.label = stat.label;
    cell.reading = stat.value;
    cell.value = Num(stat.value);
    cell.unit = stat.unit;
    cell.colour = stat.colour;
    cell.widthPercent = share;
    cell.box = Area{x, rail.y, w, rail.h};
    cells.push_back(cell);
    x += w;
  }
  return cells;
}

std::vector<RailCell> InfoRail(const Simulation& sim, const Area& rail) {
  std::vector<RailCell> cells;
  const std::vector<InfoRow>& infos = sim.Infos();
  const float share = Share(static_cast<int>(infos.size()));
  float x = rail.x;
  const float w = rail.w * share / 100.0f;
  for (const InfoRow& info : infos) {
    RailCell cell;
    cell.id = info.id;
    cell.label = info.label;
    cell.value = info.value;
    cell.widthPercent = share;
    cell.box = Area{x, rail.y, w, rail.h};
    cells.push_back(cell);
    x += w;
  }
  return cells;
}

Strip NavStrip(const Simulation& sim, const Area& box) {
  Strip strip;
  strip.id = "navstrip";
  strip.label = "Navigation";
  strip.box = box;

  const Task* travel = sim.FindOngoing("travel");
  const Body* here = sim.FindBodyById(sim.Map().here);
  const std::string where = here != nullptr ? here->name : std::string();
  if (travel != nullptr && travel->state == "running") {
    strip.main = travel->label + " - crossing in progress";
  } else if (travel != nullptr && travel->state == "done") {
    strip.main = "Crossing complete - awaiting acknowledgement";
  } else {
    strip.main = "Holding at " + where + " - no course plotted";
  }
  return strip;
}

Strip DockStrip(const Simulation& sim, const Area& box) {
  Strip strip;
  strip.id = "dockstrip";
  strip.label = "Docked";
  strip.box = box;
  const Body* body = sim.FindBodyById(sim.Map().here);
  strip.main = body != nullptr ? body->name : std::string();
  return strip;
}

IdleScreenModel BuildIdleScreen(const Simulation& sim, const ShellLayout& layout,
                                const Theme& theme, bool hideCrewLabels,
                                float othersHeight, FxRng& rng) {
  IdleScreenModel out;
  out.content = layout.content;
  out.stageNote =
      "Left-click anything here to read what it is. "
      "Right-click opens its task window.";

  const float inner = std::max(0.0f, layout.content.w - 16.0f);
  const float statH = StatCellHeight(theme);
  out.stats = StatRail(sim, Area{layout.content.x + 8.0f, layout.content.y + 4.0f, inner, statH});
  const float statsBottom =
      out.stats.empty() ? layout.content.y + 4.0f + statH : out.stats.front().box.Bottom();

  out.stageArea = layout.stage;
  out.stageArea.y = statsBottom + 8.0f;

  const float noteTop = out.stageArea.Bottom() + 6.0f;
  const float navTop = noteTop + 24.0f;
  const float stripH = StripHeight(theme);
  out.nav = NavStrip(sim, Area{layout.content.x + 8.0f, navTop, inner, stripH});

  float after = out.nav.box.Bottom();
  if (sim.State().docked) {
    out.docked = true;
    // The dock strip sits below the *actual* nav strip bottom, never at a
    // hard-coded 50 px offset, so growing the nav strip cannot overlap it.
    out.dock = DockStrip(sim, Area{layout.content.x + 8.0f, out.nav.box.Bottom() + 10.0f,
                                   inner, stripH});
    after = out.dock.box.Bottom();
  } else {
    out.dock.box = Area{layout.content.x + 8.0f, after, inner, 0.0f};
  }

  const float infoH = InfoCellHeight(theme);
  out.infos = InfoRail(sim,
                       Area{layout.content.x + 8.0f, after + 10.0f, inner, infoH});
  const float infoBottom = out.infos.empty() ? after + 10.0f : out.infos.front().box.Bottom();

  // measure-and-give, now that everything above the feed has a height.
  out.feedHeight = GiveFeedHeight(layout.content.h, othersHeight);
  out.feedBox = Area{layout.content.x + 8.0f, infoBottom + 26.0f, inner, out.feedHeight};

  out.diagram = BuildDiagram(sim, out.stageArea, hideCrewLabels, rng);
  out.feed = FeedLinesNewestFirst(sim, 200);
  (void)theme;  // the palette travels with the model; the arithmetic does not need it
  return out;
}

IdleScreenModel BuildIdleScreen(const Simulation& sim, const ShellLayout& layout,
                                const Theme& theme, bool hideCrewLabels, FxRng& rng) {
  auto out = BuildIdleScreen(sim, layout, theme, hideCrewLabels, 0, rng);
  const float other = out.feedBox.y - layout.content.y;
  out.feedHeight = GiveFeedHeight(layout.content.h, other);
  out.feedBox.h = out.feedHeight;
  return out;
}

}  // namespace ship
