// ============================================================================
//  IdleScreen.cpp — Home, built in the reference's own order.
// ============================================================================
#include "ui/IdleScreen.h"

#include <algorithm>
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
// ShellView::TypePx exactly: the minimum and larger type both follow the
// theme's selected font px. Used so a container's box is never shorter than the text it
// paints, at any typography step.
float TextLineHeight(const Theme& theme, int refSize) {
  return static_cast<float>(theme.TextPx(refSize));
}

// Stat cell: compact padding around the label, value and short status bar.
// The value ends above the bar; nothing overlaps.
float StatCellHeight(const Theme& theme) {
  const float pad = 4.0f;
  const float gap = 1.0f;
  const float label = TextLineHeight(theme, 11);
  const float value = TextLineHeight(theme, 20);
  return pad + label + gap + value + 4.0f + 4.0f + 4.0f;
}

// Strip (nav or dock): pad + label + gap + main + pad.
float StripHeight(const Theme& theme) {
  const float pad = 6.0f;
  const float gap = 6.0f;
  const float bottomPad = 6.0f;
  return pad + TextLineHeight(theme, 11) + gap + TextLineHeight(theme, 13) + bottomPad;
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

IdleScreenModel BuildIdleScreen(const Simulation& sim, const ShellLayout& layout,
                                const Theme& theme, bool hideCrewLabels,
                                float othersHeight, FxRng& rng) {
  IdleScreenModel out;
  out.content = layout.content;

  const float inner = std::max(0.0f, layout.content.w - 16.0f);
  const float statH = StatCellHeight(theme);
  out.stats = StatRail(sim, Area{layout.content.x + 8.0f, layout.content.y + 4.0f, inner, statH});
  const float statsBottom =
      out.stats.empty() ? layout.content.y + 4.0f + statH : out.stats.front().box.Bottom();

  out.stageArea = layout.stage;
  out.stageArea.y = statsBottom + 108.0f;
  out.stageArea.h = std::max(out.stageArea.h, Metrics::kHomeStageMinH);

  const float infoH = InfoCellHeight(theme);
  const float infoY = out.stageArea.Bottom() + 14.0f;
  out.infos = InfoRail(sim, Area{layout.content.x + 8.0f, infoY, inner, infoH});
  const float infoBottom = out.infos.empty() ? infoY + infoH : out.infos.front().box.Bottom();

  const float stripH = StripHeight(theme);
  const float leftColumnW = std::max(1.0f, inner / 3.0f - 4.0f);
  const float leftX = layout.content.x + 8.0f;
  const float lowerY = infoBottom + 10.0f;
  out.nav = NavStrip(sim, Area{leftX, lowerY, leftColumnW, stripH});
  out.nav.right.clear();

  const float eventX = leftX + inner / 3.0f + 4.0f;
  const float eventW = std::max(1.0f, layout.content.x + 8.0f + inner - eventX);
  out.eventsBox = Area{eventX, lowerY, eventW, stripH};
  if (sim.State().docked) {
    const Body* berth = sim.FindBodyById(sim.Map().here);
    out.events.push_back({"Docked", berth == nullptr ? std::string() : berth->name,
                          sim.Map().here, true, {}});
  }
  for (const Incident& incident : sim.Events()) {
    out.events.push_back({"Events", incident.kind, incident.id, false, {}});
  }
  while (out.events.size() < 2) {
    out.events.push_back({"Events", "No active events.", "", false, {}});
  }
  const std::size_t rows = (out.events.size() + 1) / 2;
  out.eventsBox.h = static_cast<float>(rows) * stripH + static_cast<float>(rows - 1) * 8.0f;
  const float cardW = (eventW - 8.0f) / 2.0f;
  for (std::size_t i = 0; i < out.events.size(); ++i) {
    const std::size_t row = i / 2;
    const std::size_t column = i % 2;
    out.events[i].box = Area{eventX + static_cast<float>(column) * (cardW + 8.0f),
                             lowerY + static_cast<float>(row) * (stripH + 8.0f),
                             cardW, stripH};
  }

  const float after = std::max(out.nav.box.Bottom(), out.eventsBox.Bottom());

  // The activity feed is the final home panel and uses the remaining content
  // height so its lower edge reaches the bottom of the viewport.
  const float feedStart = after;
  const float bottomPad = 8.0f;
  const float available = layout.content.Bottom() - feedStart - bottomPad;
  out.feedHeight = std::clamp(available, Metrics::kFeedMin, Metrics::kHomeFeedMax);
  const float feedY = std::max(feedStart,
                               layout.content.Bottom() - bottomPad - out.feedHeight);
  const float panelShift = feedY - feedStart;
  out.nav.box.y += panelShift;
  out.eventsBox.y += panelShift;
  for (auto& event : out.events) { event.box.y += panelShift; }
  (void)othersHeight;
  out.feedBox = Area{layout.content.x + 8.0f, feedY, inner, out.feedHeight};

  out.diagram = BuildDiagram(sim, out.stageArea, hideCrewLabels, rng);
  out.feed = FeedLinesNewestFirst(sim, 200);
  (void)theme;  // the palette travels with the model; the arithmetic does not need it
  return out;
}

IdleScreenModel BuildIdleScreen(const Simulation& sim, const ShellLayout& layout,
                                const Theme& theme, bool hideCrewLabels, FxRng& rng) {
  return BuildIdleScreen(sim, layout, theme, hideCrewLabels, 0, rng);
}

}  // namespace ship
