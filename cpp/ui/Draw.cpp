// ============================================================================
//  Draw.cpp — the skin.
//  ---------------------------------------------------------------------------
//  Raylib paints the ship; ImGui paints the cockpit around it. Both are told
//  what to paint by the view-models, and neither decides anything: a click is
//  turned into a (kind, id) pair here and handed to the shell, which is where
//  tabs open and briefings are chosen.
//
//  The palette is the stylesheet's: parchment panels, coffee ink, and one warm
//  cream for a tab that has just announced itself. There is no default ImGui
//  theme anywhere in this file - a stock blue-dark window in a parchment
//  cockpit would be the loudest possible lie about what this port is.
// ============================================================================
#include "ui/Draw.h"
#include "core/DisplaySettings.h"
#include "ui/First2000Styles.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <unordered_map>

#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"

#include "audio/Audio.h"
#include "ui/WikiScreen.h"

namespace ship {

namespace {

::Color ToRay(const Rgb& rgb, int alpha = 255) {
  return ::Color{static_cast<unsigned char>(rgb.r), static_cast<unsigned char>(rgb.g),
                 static_cast<unsigned char>(rgb.b),
                 static_cast<unsigned char>(alpha)};
}

// Spaceship.hta .pbtn-fill: #dff0cc, with a #7ec63f right edge.
// Share this across machine controls, generic bands, chart and quarters.
void PaintProgressFill(const Area& box, double percent) {
  const float width = box.w * static_cast<float>(std::clamp(percent, 0.0, 100.0)) / 100.0f;
  if (width <= 0.0f) { return; }
  DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                static_cast<int>(width), static_cast<int>(box.h), ::Color{223, 240, 204, 255});
  DrawLine(static_cast<int>(box.x + width - 1), static_cast<int>(box.y),
           static_cast<int>(box.x + width - 1), static_cast<int>(box.Bottom() - 1),
           ::Color{126, 198, 63, 255});
}

::Color HexColor(const std::string& value, ::Color fallback) {
  if (value.size() != 7 || value[0] != '#') { return fallback; }
  char* end = nullptr;
  const unsigned long rgb = std::strtoul(value.c_str() + 1, &end, 16);
  if (end != value.c_str() + value.size()) { return fallback; }
  return {static_cast<unsigned char>(rgb >> 16), static_cast<unsigned char>(rgb >> 8),
          static_cast<unsigned char>(rgb), 255};
}

bool PaintMessStockIcon(const MachinePart& part, const Area& origin) {
  const bool cookedFood = part.cls == "mess-food-bottle";
  const bool dryRation = part.cls == "mess-ration-pack";
  if (!cookedFood && !dryRation) { return false; }

  const float x = origin.x + part.box.x;
  const float y = origin.y + part.box.y;
  const float w = part.box.w;
  const float h = part.box.h;
  if (w <= 0.0f || h <= 0.0f) { return true; }

  const ::Color outline = cookedFood ? ::Color{107, 125, 80, 255}
                                      : ::Color{140, 111, 78, 255};
  const ::Color shell = cookedFood ? ::Color{223, 236, 205, 255}
                                    : ::Color{243, 230, 207, 255};
  const ::Color label = ::Color{250, 247, 242, 255};
  if (cookedFood) {
    const Rectangle neck{x + w * 0.32f, y + h * 0.10f,
                         w * 0.36f, h * 0.37f};
    DrawRectangleRounded(neck, 0.25f, 6, shell);
    DrawRectangleRoundedLinesEx(neck, 0.25f, 6, 1.0f, outline);

    const Rectangle body{x + w * 0.10f, y + h * 0.31f,
                         w * 0.80f, h * 0.65f};
    DrawRectangleRounded(body, 0.28f, 6, shell);
    DrawRectangleRoundedLinesEx(body, 0.28f, 6, 1.0f, outline);

    const Rectangle band{x + w * 0.22f, y + h * 0.56f,
                         w * 0.56f, h * 0.19f};
    DrawRectangleRec(band, label);
    const Rectangle cap{x + w * 0.25f, y + h * 0.02f,
                        w * 0.50f, h * 0.14f};
    DrawRectangleRounded(cap, 0.18f, 4, ::Color{127, 145, 97, 255});
  } else {
    const Rectangle pack{x + w * 0.10f, y + h * 0.10f,
                         w * 0.80f, h * 0.86f};
    DrawRectangleRounded(pack, 0.14f, 4, shell);
    DrawRectangleRoundedLinesEx(pack, 0.14f, 4, 1.0f, outline);
    DrawLineEx({pack.x + 1.0f, pack.y + 2.0f},
               {pack.x + pack.width / 2.0f, pack.y + pack.height * 0.20f},
               1.0f, outline);
    DrawLineEx({pack.x + pack.width - 1.0f, pack.y + 2.0f},
               {pack.x + pack.width / 2.0f, pack.y + pack.height * 0.20f},
               1.0f, outline);
    const Rectangle label{x + w * 0.24f, y + h * 0.48f,
                          w * 0.52f, h * 0.22f};
    DrawRectangleRec(label, ::Color{228, 219, 203, 255});
  }
  return true;
}

// WrapText(): greedy wrapping by measured width, so a long paragraph in a
// dialog or an article breaks where the reference's CSS would break it.
std::vector<std::string> WrapText(const std::string& text, float width, int size,
                                  const Font& font) {
  std::vector<std::string> lines;
  std::string line;
  std::size_t i = 0;
  auto fits = [&](const std::string& value) {
    return MeasureTextEx(font, value.c_str(), static_cast<float>(size), 1.0f).x <= width;
  };
  while (i < text.size()) {
    if (text[i] == '\n') { lines.push_back(line); line.clear(); ++i; continue; }
    const std::size_t end = text.find_first_of(" \n", i);
    const std::size_t stop = end == std::string::npos ? text.size() : end;
    if (stop == i) { // preserve spaces without manufacturing leading spaces
      if (!line.empty()) { line += ' '; }
      ++i;
      continue;
    }
    const std::string word = text.substr(i, stop - i);
    if (!line.empty() && !fits(line + word)) {
      while (!line.empty() && line.back() == ' ') { line.pop_back(); }
      lines.push_back(line); line.clear();
    }
    // Long unbroken values wrap too, at UTF-8 codepoint boundaries.
    for (std::size_t j = 0; j < word.size();) {
      std::size_t next = j + 1;
      while (next < word.size() && (static_cast<unsigned char>(word[next]) & 0xc0) == 0x80) { ++next; }
      const std::string ch = word.substr(j, next - j);
      if (!line.empty() && !fits(line + ch)) { lines.push_back(line); line.clear(); }
      line += ch;
      j = next;
    }
    i = stop;
  }
  lines.push_back(line);
  return lines;
}

ImU32 ToIm(const Rgb& rgb, int alpha = 255) {
  return IM_COL32(rgb.r, rgb.g, rgb.b, alpha);
}

std::string TimeStamp(double actionCount) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "A%04d",
                static_cast<int>(std::max(0.0, actionCount)));
  return buffer;
}

}  // namespace

int ShellView::TypePx(int reference) const { return theme_.TextPx(reference); }

// The font used to paint text at `size`: bold once the 18 px floor is reached.
// TextWidth/WrapText must measure with the same font Text paints with, or
// layout arithmetic (panel heights, FitText truncation) silently under- or
// over-estimates and panels overlap or text is clipped.
const Font& ShellView::SelectFont(int size, bool mono) const {
  return mono ? monoFont_
              : (size >= TypePx(16) ? boldFont_ : bodyFont_);
}

void ShellView::Text(const char* text, int x, int y, int size, ::Color tint,
                     bool mono) const {
  const Font& font = SelectFont(size, mono);
  if (paintingContent_) { contentPaintBottom_ = std::max(contentPaintBottom_, static_cast<float>(y + size + 8)); }
  DrawTextEx(font, text, Vector2{static_cast<float>(x), static_cast<float>(y)},
             static_cast<float>(size), 1.0f, tint);
}

float ShellView::TextWidth(const char* text, int size, bool mono) const {
  const Font& font = SelectFont(size, mono);
  return MeasureTextEx(font, text, static_cast<float>(size), 1.0f).x;
}

std::string ShellView::FitText(const std::string& text, float maxWidth, int size,
                               bool mono) const {
  // Never discard content: return the full text even if it exceeds maxWidth.
  // Layouts grow, wrap, or scroll — they do not silently truncate.
  (void)maxWidth;
  (void)size;
  (void)mono;
  return text;
}

float ShellView::PaintTaskHeader(const TaskHead& head, float left, float top) const {
  const Palette& p = theme_.palette;
  const float width = std::max(1.0f, layout_.content.Right() - left - 8.0f);
  const int kindSize = TypePx(9);
  const int titleSize = TypePx(20);
  const int subSize = TypePx(11);
  const float smallLine = theme_.LineHeight();
  const float titleLine = std::max(smallLine, TypeStep(28.0f));
  float y = top;
  Text(head.kind.c_str(), static_cast<int>(left), static_cast<int>(y), kindSize,
       ToRay(p.muted));
  y += smallLine + TypeStep(3.0f);
  for (const auto& title : WrapText(head.title, width, titleSize, SelectFont(titleSize, false))) {
    Text(title.c_str(), static_cast<int>(left), static_cast<int>(y), titleSize, ToRay(p.ink));
    y += titleLine;
  }
  y += TypeStep(3.0f);
  const std::vector<std::string> subtitle =
      WrapText(head.sub, width, subSize, SelectFont(subSize, false));
  for (const std::string& line : subtitle) {
    Text(line.c_str(), static_cast<int>(left), static_cast<int>(y), subSize,
         ToRay(p.muted));
    y += smallLine;
  }
  return y - top + TypeStep(4.0f);
}

ShellView::ShellView() { theme_ = Theme::ById("classic"); }

void ShellView::Configure(float width, float height, const Theme& theme) {
  width_ = width;
  height_ = height;
  theme_ = theme;
  layout_ = ComputeLayout(width_, height_, height_ - Metrics::kContentTop -
                                              Metrics::kStatusH,
                          others_, theme_.stageCompact);
  // A resize or font change can remove the overflow that required scrolling:
  // reset so the column isn't scrolled when nothing extends past the viewport.
  ResetContentScroll();
}

void ShellView::ResetContentScroll() { contentScroll_ = 0.0f; }

void ShellView::Draw(Simulation& sim, double nowMs) {
  frameTimeMs_ = nowMs;
  const float contentH = height_ - Metrics::kContentTop - Metrics::kStatusH;
  layout_ = ComputeLayout(width_, height_, contentH, others_, theme_.stageCompact);

  if (scrollTabKey_ != sim.State().activeTab) {
    scrollTabKey_ = sim.State().activeTab;
    ResetContentScroll();
    if (sim.State().activeTab == "home") { feedScroll_ = 0; draggingFeed_ = false; }
    draggingChart_ = false;
  }

  // Only Wren has supplied exterior artwork. The Kestrel and a missing asset
  // keep the existing fully interactive deck plan, without invented hull art.
  if (sim.State().activeTab == "home" && HasWrenExterior(sim)) {
    if (exteriorView_) {
      layout_.stage = {layout_.content.x + 8.0f, layout_.content.y,
                       std::max(1.0f, layout_.content.w - 16.0f),
                       Metrics::kHomeStageMinH};
      layout_.stage.h = BuildExteriorLayout(sim, layout_.stage).height;
    }
  }

  // Always build Home, including when reduced motion is enabled. Keep drift
  // state across frames and advance it at the reference cadence; rebuilding
  // random dots every frame caused teleporting and a blank reduced-motion view.
  if (sim.State().activeTab == "home") {
    FxRng seeded = rng_;
    idle_ = BuildIdleScreen(sim, layout_, theme_, theme_.hideCrewLabels, seeded);
    if (HasWrenExterior(sim)) {
      exterior_ = BuildExteriorLayout(sim, idle_.stageArea);
      // No tutorial copy or view-switch row is drawn above the ship.
    }
    // All six Home cards share one measured height. Grow the whole grid when
    // any wrapped info, navigation, or event text needs additional room.
    if (!idle_.infos.empty()) {
      const int labelPx = theme_.HomeInfoLabelPx();
      const int valuePx = theme_.HomeInfoValuePx();
      const int stripLabelPx = TypePx(11);
      const int stripValuePx = TypePx(13);
      float needed = idle_.infos.front().box.h;
      for (std::size_t i = 0; i < idle_.infos.size(); ++i) {
        const auto& cell = idle_.infos[i];
        const float width = std::max(1.0f, cell.box.w - 20.0f);
        const auto labels = WrapText(cell.label, width, labelPx, SelectFont(labelPx, false));
        const auto values = WrapText(cell.value, width, valuePx, SelectFont(valuePx, false));
        needed = std::max(needed, 6.0f + labels.size() * (labelPx + 3.0f) + 3.0f +
                                 values.size() * (valuePx + 3.0f) + 6.0f);
      }
      const float navWidth = std::max(1.0f, idle_.nav.box.w - 16.0f);
      const auto navLines = WrapText(idle_.nav.main, navWidth, stripValuePx,
                                     SelectFont(stripValuePx, false));
      needed = std::max(needed, 6.0f + stripLabelPx + 6.0f +
                               navLines.size() * (stripValuePx + 3.0f) + 6.0f);
      for (const auto& event : idle_.events) {
        const float width = std::max(1.0f, event.box.w - 16.0f);
        const auto heading = WrapText(event.heading, width, stripLabelPx,
                                      SelectFont(stripLabelPx, false));
        const auto lines = WrapText(event.label, width, stripValuePx,
                                    SelectFont(stripValuePx, false));
        needed = std::max(needed, 6.0f + heading.size() * (stripLabelPx + 3.0f) + 6.0f +
                                 lines.size() * (stripValuePx + 3.0f) + 6.0f);
      }

      const float extra = needed - idle_.infos.front().box.h;
      for (auto& cell : idle_.infos) { cell.box.h = needed; }
      const float lowerY = idle_.nav.box.y + extra;
      idle_.nav.box.y = lowerY;
      idle_.nav.box.h = needed;
      idle_.eventsBox.y = lowerY;
      const std::size_t eventRows = (idle_.events.size() + 1) / 2;
      idle_.eventsBox.h = static_cast<float>(eventRows) * needed +
                          static_cast<float>(eventRows - 1) * Metrics::kHomePanelGap;
      for (std::size_t i = 0; i < idle_.events.size(); ++i) {
        const std::size_t row = i / 2;
        idle_.events[i].box.y = lowerY + static_cast<float>(row) *
                                (needed + Metrics::kHomePanelGap);
        idle_.events[i].box.h = needed;
      }

      const float lowerBottom = std::max(idle_.nav.box.Bottom(), idle_.eventsBox.Bottom());
      const float feedStart = lowerBottom + Metrics::kHomePanelGap;
      const float available = layout_.content.Bottom() - feedStart - 8.0f;
      idle_.feedHeight = std::clamp(available, Metrics::kFeedMin, Metrics::kHomeFeedMax);
      idle_.feedBox.y = std::max(feedStart,
                                 layout_.content.Bottom() - 8.0f - idle_.feedHeight);
      const float panelShift = idle_.feedBox.y - feedStart;
      idle_.nav.box.y += panelShift;
      idle_.eventsBox.y += panelShift;
      for (auto& event : idle_.events) { event.box.y += panelShift; }
      idle_.feedBox.h = idle_.feedHeight;
    }
    rng_ = seeded;
    SyncIdleDots(sim, nowMs);
    // The whole home column scrolls when it is taller than the content area
    // (the HTA's #content { overflow:auto }). The ceiling is the feed's bottom
    // edge; anything past the content bottom is reachable only by scrolling.
    contentMaxScroll_ =
        std::max(0.0f, idle_.feedBox.Bottom() - layout_.content.Bottom());
  }

  actionBandTop_ = layout_.content.Bottom();
  actionBandHits_.clear();
  feedActive_ = sim.State().activeTab == "home" && !titleActive_;
  if (!feedActive_) draggingFeed_ = false;
  UpdateMarks(marks_, nowMs);

  // A lost voyage that has been dealt with (loaded or restarted) puts the
  // overlay away for good; a fresh loss brings it back.
  if (sim.State().gameOver.empty()) { lossDismissed_ = false; }

  ClearBackground(ToRay(theme_.palette.content));

  if (titleActive_) {
    PaintTitle(sim, nowMs);
    return;
  }

  // Main-content scroll: the whole content column moves with contentScroll_,
  // matching the HTA's #content { overflow:auto }. Every screen paints inside
  // this transform, and Click() offsets the cursor by the same amount so
  // obscured content can never be clicked.
  Camera2D scrollCam{{0.0f, 0.0f}, {0.0f, contentScroll_}, 0.0f, 1.0f};
  contentPaintBottom_ = layout_.content.y;
  paintingContent_ = true;
  BeginMode2D(scrollCam);

  if (sim.State().activeTab == "home") {
    // Screen hit lists belong to the window that was painted last frame. Clear
    // them when Home becomes active, otherwise a stale quarters/stores early
    // return can swallow every click on the ship.
    screenNav_ = false;
    screenQuarters_ = false;
    screenRecord_ = false;
    screenPanels_ = false;
    navHits_.clear();
    taskButtons_.clear();
    PaintIdle(sim);
  } else {
    // Ask the router for the window the active tab owns - the same function the
    // tests drive, so what is painted is what was proven.
    const Tab* tab = FindTab(sim.State(), sim.State().activeTab);
    const ScreenKind kind = tab == nullptr || !DescribeTargetExists(sim, tab->kind, tab->id)
                                ? ScreenKind::Described : ScreenKindFor(*tab);
    screenNav_ = kind == ScreenKind::Nav;
    screenQuarters_ = kind == ScreenKind::Quarters;
    screenRecord_ = kind == ScreenKind::CrewRecord;
    screenPanels_ = kind == ScreenKind::Station || kind == ScreenKind::Cargo ||
                    kind == ScreenKind::Science || kind == ScreenKind::Room ||
                    kind == ScreenKind::SimCore || kind == ScreenKind::Awards ||
                    kind == ScreenKind::Event || kind == ScreenKind::Wiki ||
                    kind == ScreenKind::System || kind == ScreenKind::Info;
    panelsKind_ = kind;
    if (taskTabKey_ != sim.State().activeTab) {
      pendingConfirm_.clear();
      pendingAction_.clear();
      // Another window is being described: the station's walk starts over.
      stationSelection_ = StationSelection();
    }
    taskTabKey_ = sim.State().activeTab;
    const Tab* activeTab = FindTab(sim.State(), taskTabKey_);
    if (screenNav_) {
      EnsureNavScreen(sim);
      taskScreen_ = navScreen_.screen;
      PaintNavScreen(sim);
    } else {
      taskScreen_ = BuildTabScreen(sim, *activeTab, wiki_, &stationSelection_);
      if (screenQuarters_) {
        quarters_ = BuildQuartersScreen(sim);
        PaintQuartersScreen(sim);
      } else if (screenRecord_) {
        record_ = BuildCrewRecord(sim, tab->id);
        PaintCrewRecord(sim);
      } else if (screenPanels_) {
        PaintPanelsScreen(sim);
      } else {
        PaintTaskScreen(sim);
      }
    }
  }
  EndMode2D();
  paintingContent_ = false;
  const float readingBottom = screenQuarters_ ? quartersBandTop_ : actionBandTop_;
  contentMaxScroll_ = std::max(0.0f, contentPaintBottom_ - readingBottom);
  contentScroll_ = std::min(contentScroll_, contentMaxScroll_);

  PaintChrome(sim, nowMs);
  if (!LossVisible(sim)) { PaintPopup(sim); }
  if (LossVisible(sim)) { PaintLoss(sim); }

  // Action marks last, over everything, because they are the player's hand.
  for (const ActionMark& mark : marks_) {
    const MarkStyle style = MarkAt(mark, nowMs);
    if (!style.alive) { continue; }
    const int alpha = static_cast<int>(style.opacity * 255.0f);
    const float x = mark.x;
    // Five-pixel soft green halo, behind the two crisp foreground bars.
    for (int radius = 5; radius > 0; --radius) {
      const Color glow{0xb6, 0xf0, 0x7d, static_cast<unsigned char>(alpha * (6 - radius) / 60)};
      DrawRectangle(static_cast<int>(x) - radius, static_cast<int>(style.top + 4) - radius,
                    11 + radius * 2, 2 + radius * 2, glow);
      DrawRectangle(static_cast<int>(x + 4) - radius, static_cast<int>(style.top) - radius,
                    2 + radius * 2, 11 + radius * 2, glow);
    }
    const Color markInk{0x7e, 0xc6, 0x3f, static_cast<unsigned char>(alpha)};
    DrawRectangle(static_cast<int>(x), static_cast<int>(style.top + 4), 11, 2, markInk);
    DrawRectangle(static_cast<int>(x + 4), static_cast<int>(style.top), 2, 11, markInk);
  }

}

void ShellView::SyncIdleDots(const Simulation& sim, double nowMs) {
  const bool compact = sim.State().shipClass == "wren";
  // Reconcile by identity, not just count. Retain cosmetic movement while
  // refreshing its effective room (job, medical, social or corridor) every frame.
  std::vector<DriftDot> current;
  for (const DiagramCrew& crew : idle_.diagram.crew) {
    const auto old = std::find_if(dots_.begin(), dots_.end(),
                                [&crew](const DriftDot& d) { return d.id == crew.id; });
    DriftDot dot;
    if (old != dots_.end()) { dot = *old; }
    else {
      dot.id = crew.id;
      dot.x = dot.tx = crew.dot.x;
      dot.y = dot.ty = crew.dot.y;
    }
    dot.roomId = crew.roomId;
    current.push_back(dot);
  }
  dots_ = std::move(current);
  if (!sim.State().reducedMotion && lastDriftMs_ <= 0.0) { lastDriftMs_ = nowMs; }
  const int ticks = sim.State().reducedMotion || lastDriftMs_ <= 0.0
                        ? 0
                        : std::min(8, static_cast<int>((nowMs - lastDriftMs_) / FxTiming::kDriftMs));
  if (ticks > 0) {
    for (DriftDot& dot : dots_) {
      const auto room = std::find_if(idle_.diagram.rooms.begin(), idle_.diagram.rooms.end(),
                                     [&dot](const Room& r) { return r.id == dot.roomId; });
      if (room == idle_.diagram.rooms.end() && dot.roomId != "corridor") { continue; }
      const Area roomBox = dot.roomId == "corridor" ? idle_.diagram.corridor :
          Area{static_cast<float>(room->box.x), static_cast<float>(room->box.y),
               static_cast<float>(room->box.w), static_cast<float>(room->box.h)};
      for (int i = 0; i < ticks; ++i) { DriftStep(dot, roomBox, compact, driftRng_); }
    }
    lastDriftMs_ += static_cast<double>(ticks * FxTiming::kDriftMs);
  }
  for (DiagramCrew& crew : idle_.diagram.crew) {
    const auto dot = std::find_if(dots_.begin(), dots_.end(),
                                  [&crew](const DriftDot& d) { return d.id == crew.id; });
    if (dot == dots_.end()) { continue; }
    const float dx = dot->x - crew.dot.x;
    const float dy = dot->y - crew.dot.y;
    crew.dot.x = dot->x;
    crew.dot.y = dot->y;
    crew.label.x += dx;
    crew.label.y += dy;
  }
}

void ShellView::PaintChrome(Simulation& sim, double nowMs) {
  ImGuiIO& io = ImGui::GetIO();
  (void)io;
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(width_, height_));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
  ImGui::Begin("##spaceship", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs);
  ImDrawList* draw = ImGui::GetWindowDrawList();

  // ---- taskbar -----------------------------------------------------------
  const ship::Palette& p = theme_.palette;
  draw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(width_, Metrics::kTaskbarH),
                      ToIm(theme_.id == "classic" ? p.panel2 : p.chrome));
  draw->AddLine(ImVec2(0.0f, Metrics::kTaskbarH - 1.0f),
                ImVec2(width_, Metrics::kTaskbarH - 1.0f), ToIm(p.borderDark));

  std::vector<TabView> tabs = TabBar(sim);
  tabs.erase(std::remove_if(tabs.begin(), tabs.end(), [](const TabView& tab) {
    return tab.home;
  }), tabs.end());
  std::vector<float> tabWidths;
  float tabsWidth = 4.0f;
  for (const TabView& tab : tabs) {
    const std::string label = tab.kindLabel + "  " + tab.title;
    const float tabW = ImGui::CalcTextSize(label.c_str()).x + (tab.closable ? 30.0f : 18.0f);
    tabWidths.push_back(tabW);
    tabsWidth += tabW + 4.0f;
  }
  maxTabScroll_ = std::max(0.0f, tabsWidth - layout_.tabStrip.w);
  tabScroll_ = std::max(0.0f, std::min(maxTabScroll_, tabScroll_));
  const auto active = std::find_if(tabs.begin(), tabs.end(), [](const TabView& t) { return t.active; });
  const std::string requested = sim.State().tabRevealKey;
  const auto reveal = requested.empty() ? active :
      std::find_if(tabs.begin(), tabs.end(), [&requested](const TabView& t) { return t.key == requested; });
  if (reveal != tabs.end() && (!requested.empty() || reveal->key != lastRevealedTab_)) {
    const std::size_t index = static_cast<std::size_t>(reveal - tabs.begin());
    float start = 4.0f;
    for (std::size_t i = 0; i < index; ++i) { start += tabWidths[i] + 4.0f; }
    const float end = start + tabWidths[index];
    if (end - tabScroll_ > layout_.tabStrip.w) { tabScroll_ = std::min(maxTabScroll_, end - layout_.tabStrip.w); }
    if (start - tabScroll_ < 0.0f) { tabScroll_ = start; }
    lastRevealedTab_ = active == tabs.end() ? "" : active->key;
  }
  sim.MutableState().tabRevealKey.clear();
  for (auto it = tabPulseStarts_.begin(); it != tabPulseStarts_.end();) {
    if (std::none_of(tabs.begin(), tabs.end(), [&](const TabView& t) { return t.key == it->first; }))
      it = tabPulseStarts_.erase(it);
    else ++it;
  }
  tabHits_.clear();
  draw->PushClipRect(ImVec2(layout_.tabStrip.x, 0.0f),
                     ImVec2(layout_.tabStrip.Right(), Metrics::kTaskbarH), true);
  float x = layout_.tabStrip.x + 4.0f - tabScroll_;
  for (std::size_t i = 0; i < tabs.size(); ++i) {
    const TabView& tab = tabs[i];
    const float w = tabWidths[i];
    const Area tabBox{x, 3.0f, w, Metrics::kTaskbarH - 5.0f};
    const Area closeBox{tabBox.Right() - 19.0f, tabBox.y, 18.0f, tabBox.h};
    tabHits_.push_back(TabHit{tab.key, tabBox, closeBox, tab.closable});
    ImU32 fill = tab.active ? ToIm(p.content) : ToIm(p.panel3);
    if (tab.fresh && !sim.State().reducedMotion) { tabPulseStarts_[tab.key] = nowMs; }
    const auto pulse = tabPulseStarts_.find(tab.key);
    if (pulse != tabPulseStarts_.end()) {
      const double elapsed = std::floor(std::max(0.0, nowMs - pulse->second) / 50.0) * 50.0;
      if (sim.State().reducedMotion || elapsed >= 1660.0) { tabPulseStarts_.erase(pulse); }
      else {
        const double t = std::clamp((elapsed - 160.0) / 1500.0, 0.0, 1.0);
        const double k = t * t * (3.0 - 2.0 * t);
        const int base[3] = {tab.active ? 255 : 239, tab.active ? 255 : 232, tab.active ? 255 : 220};
        const int warm[3] = {233, 214, 184};
        int c[3];
        for (int q = 0; q < 3; ++q) c[q] = static_cast<int>(std::round(warm[q] + (base[q] - warm[q]) * k));
        fill = IM_COL32(c[0], c[1], c[2], 255);
      }
    }
    draw->AddRectFilled(ImVec2(x, 3.0f), ImVec2(x + w, Metrics::kTaskbarH - 2.0f), fill, 3.0f);
    draw->AddRect(ImVec2(x, 3.0f), ImVec2(x + w, Metrics::kTaskbarH - 2.0f), ToIm(p.border), 3.0f);
    const ImU32 ink = ToIm(tab.active ? p.activeInk : p.tabInk);
    float titleX = x + 7.0f;
    draw->AddText(ImVec2(titleX, 9.0f), ToIm(p.muted), tab.kindLabel.c_str());
    titleX += ImGui::CalcTextSize((tab.kindLabel + "  ").c_str()).x;
    draw->AddText(ImVec2(titleX, 9.0f), ink, tab.title.c_str());
    if (tab.closable) { draw->AddText(ImVec2(x + w - 13.0f, 9.0f), ToIm(p.muted), "x"); }
    x += w + 4.0f;
  }
  draw->PopClipRect();

  // The ship identity is a permanent home control in the fixed sidebar slot.
  draw->AddRectFilled(ImVec2(layout_.home.x, layout_.home.y),
                      ImVec2(layout_.home.Right(), layout_.home.Bottom()), ToIm(p.panel2));
  draw->AddRect(ImVec2(layout_.home.x, layout_.home.y),
                ImVec2(layout_.home.Right(), layout_.home.Bottom()), ToIm(p.borderDark));
  draw->AddText(ImVec2(layout_.home.x + 8.0f, 9.0f), ToIm(p.coffee), "SHIP");
  draw->AddText(ImVec2(layout_.home.x + 50.0f, 9.0f), ToIm(p.activeInk),
                sim.Ship().Get("name").c_str());

  // Only the tab-scroll arrows remain in the top-right control slot.
  draw->AddRectFilled(ImVec2(layout_.controls.x, 0.0f),
                      ImVec2(layout_.controls.Right(), Metrics::kTaskbarH), ToIm(p.panel2));
  const float textY = std::max(0.0f, (Metrics::kTaskbarH - ImGui::GetFontSize()) * 0.5f);
  const float leftW = ImGui::CalcTextSize("<").x;
  const float rightW = ImGui::CalcTextSize(">").x;
  draw->AddText(ImVec2(layout_.scrollLeft.x + (layout_.scrollLeft.w - leftW) * 0.5f, textY), ToIm(p.muted), "<");
  draw->AddText(ImVec2(layout_.scrollRight.x + (layout_.scrollRight.w - rightW) * 0.5f, textY), ToIm(p.muted), ">");
  (void)tabScroll_;

  // ---- sidebar -----------------------------------------------------------
  // Sidebar text is compact enough for the narrow column, while tracking the
  // same reading-size preference as the central labels and other controls.
  ImGui::PushFont(nullptr, theme_.SidebarPx());
  const float sidebarLine = Theme::LineHeightForPx(ImGui::GetFontSize());
  draw->AddRectFilled(ImVec2(layout_.sidepanel.x, layout_.sidepanel.y),
                      ImVec2(layout_.sidepanel.Right(), layout_.sidepanel.Bottom()),
                      ToIm(p.chrome));

  const auto measureSide = [&](const std::string& text, float width) {
    const float h = ImGui::CalcTextSize(text.c_str(), nullptr, false, width).y;
    return std::max(1.0f, std::ceil(h / ImGui::GetFontSize())) * sidebarLine;
  };
  sideHits_ = SideHits(sim, layout_.sidepanel, layout_.sidefoot, sidebarLine,
                       sideScroll_, &sideContentH_, measureSide);
  // A browser clamps scrollTop when a section folds or a new voyage replaces
  // its contents. Do the same here; an old offset must not erase Vessel and
  // the other collapsed headings after the sidebar becomes shorter.
  const float boundedSideScroll = std::clamp(sideScroll_, 0.0f,
      std::max(0.0f, sideContentH_ - layout_.sidepanel.h));
  if (boundedSideScroll != sideScroll_) {
    sideScroll_ = boundedSideScroll;
    sideHits_ = SideHits(sim, layout_.sidepanel, layout_.sidefoot, sidebarLine,
                         sideScroll_, &sideContentH_, measureSide);
  }
  const std::vector<SideHit>& hits = sideHits_;
  const std::vector<SideBlock> blocks = SideBlocks(sim);
  draw->PushClipRect(ImVec2(layout_.sidepanel.x, layout_.sidepanel.y),
                     ImVec2(layout_.sidepanel.Right(), layout_.sidepanel.Bottom()), true);
  for (const SideHit& hit : hits) {
    if (hit.pinned) { continue; } // footer labels are tooltips, never sidebar text
    const Area& box = hit.paintBox;
    auto wrapped = [&](const std::string& text, float y, float width, ImU32 ink) {
      draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(box.x, y), ink,
                    text.c_str(), nullptr, width);
    };
    if (hit.header) {
      draw->AddText(ImVec2(box.x, box.y), ToIm(p.coffee), hit.text.c_str());
      bool open = true;
      for (const auto& block : blocks) { if (block.key == hit.key) { open = block.open; } }
      if (hit.key != "indicators") {
        const float x = box.Right() - 9, y = box.y + 5;
        if (open) {
          draw->AddTriangleFilled(ImVec2(x, y), ImVec2(x + 8, y),
                                  ImVec2(x + 4, y + 5), IM_COL32(140,111,78,255));
        } else {
          draw->AddTriangleFilled(ImVec2(x, y - 1), ImVec2(x, y + 7),
                                  ImVec2(x + 5, y + 3), IM_COL32(162,148,127,255));
        }
      } else {
        for (const auto& line : PlaceIndicators(IndicatorLines(sim))) {
          const float base = box.Bottom() + 4;
          draw->AddRectFilled(ImVec2(box.x + line.ghost.x, base + line.ghost.y),
                              ImVec2(box.x + line.ghost.x + 3, base + line.ghost.Bottom()), IM_COL32(236,229,216,255));
          draw->AddRectFilled(ImVec2(box.x + line.line.x, base + line.line.y),
                              ImVec2(box.x + line.line.x + 3, base + line.line.Bottom()),
                              ToIm(line.grow ? p.terracotta : p.sage));
        }
      }
      continue;
    }
    if (hit.action == SideAction::OpenEvent) {
      wrapped(hit.text, box.y, box.w - 60, ToIm(p.ink));
      const Area button{box.Right() - 52, box.y, 52, sidebarLine + 4};
      draw->AddRect(ImVec2(button.x, button.y), ImVec2(button.Right(), button.Bottom()), ToIm(p.borderDark));
      draw->AddText(ImVec2(button.x + 4, button.y + 2), ToIm(p.muted), "Open");
      continue;
    }
    wrapped(hit.text, box.y, box.w, ToIm(p.ink));
    if (!hit.rightValue.empty()) {
      if (hit.stackValue) {
        wrapped(hit.rightValue, box.y + hit.valueOffset, box.w, ToIm(hit.dim ? p.muted : p.ink));
      } else {
        const float valueW = ImGui::CalcTextSize(hit.rightValue.c_str()).x;
        draw->AddText(ImVec2(box.Right() - valueW, box.y), ToIm(hit.dim ? p.muted : p.ink), hit.rightValue.c_str());
      }
    }
    if (!hit.detail.empty()) { wrapped(hit.detail, box.y + hit.valueOffset, box.w, ToIm(p.muted)); }
  }
  draw->PopClipRect();

  // ---- pinned foot -------------------------------------------------------
  draw->AddRectFilled(ImVec2(layout_.sidefoot.x, layout_.sidefoot.y),
                      ImVec2(layout_.sidefoot.Right(), layout_.sidefoot.Bottom()), ToIm(p.chrome));
  draw->AddLine(ImVec2(0.0f, layout_.sidefoot.y),
                ImVec2(layout_.sidefoot.Right(), layout_.sidefoot.y), ToIm(p.border));
  for (const SideHit& hit : hits) {
    if (!hit.pinned) { continue; }
    if (hit.action == SideAction::Manifest || hit.action == SideAction::Log ||
        hit.action == SideAction::Files || hit.action == SideAction::Settings) {
      const ImVec2 iconMin(hit.box.x + 3.0f, hit.box.y + 1.0f);
      const ImVec2 iconMax(hit.box.Right() - 3.0f, hit.box.y + 27.0f);
      draw->AddRect(iconMin, iconMax, ToIm(p.borderDark));
      const ImVec2 iconCenter(hit.box.x + hit.box.w * 0.5f,
                             hit.box.y + 14.0f);
      const ImU32 iconInk = ToIm(p.coffee);
      if (hit.action == SideAction::Manifest) {
        draw->AddLine(ImVec2(iconCenter.x, iconCenter.y - 8.0f),
                      ImVec2(iconCenter.x, iconCenter.y + 4.0f), iconInk, 2.0f);
        draw->AddTriangleFilled(ImVec2(iconCenter.x + 1.0f, iconCenter.y - 6.0f),
                                ImVec2(iconCenter.x + 8.0f, iconCenter.y - 2.0f),
                                ImVec2(iconCenter.x + 1.0f, iconCenter.y + 1.0f),
                                ToIm(p.muted));
        draw->AddTriangleFilled(ImVec2(iconCenter.x - 8.0f, iconCenter.y + 4.0f),
                                ImVec2(iconCenter.x + 8.0f, iconCenter.y + 4.0f),
                                ImVec2(iconCenter.x, iconCenter.y + 9.0f), iconInk);
      } else if (hit.action == SideAction::Log) {
        for (int line = 0; line < 3; ++line) {
          const float yy = iconCenter.y - 5.0f + static_cast<float>(line) * 5.0f;
          draw->AddLine(ImVec2(iconCenter.x - 8.0f, yy),
                        ImVec2(iconCenter.x + (line == 2 ? 3.0f : 8.0f), yy),
                        iconInk, 2.0f);
        }
      } else if (hit.action == SideAction::Files) {
        draw->AddRectFilled(ImVec2(iconCenter.x - 7.0f, iconCenter.y - 8.0f),
                            ImVec2(iconCenter.x + 7.0f, iconCenter.y + 8.0f),
                            iconInk, 1.0f);
        draw->AddRectFilled(ImVec2(iconCenter.x - 2.0f, iconCenter.y - 7.0f),
                            ImVec2(iconCenter.x + 4.0f, iconCenter.y - 3.0f),
                            ToIm(p.panel));
        draw->AddRectFilled(ImVec2(iconCenter.x - 4.0f, iconCenter.y + 2.0f),
                            ImVec2(iconCenter.x + 5.0f, iconCenter.y + 6.0f),
                            ToIm(p.coffeeLight));
      } else {
        draw->AddCircle(iconCenter, 6.0f, iconInk, 0, 2.0f);
        draw->AddCircleFilled(iconCenter, 2.0f, iconInk);
        draw->AddLine(ImVec2(iconCenter.x, iconCenter.y - 10.0f),
                      ImVec2(iconCenter.x, iconCenter.y - 6.0f), iconInk, 2.0f);
        draw->AddLine(ImVec2(iconCenter.x, iconCenter.y + 6.0f),
                      ImVec2(iconCenter.x, iconCenter.y + 10.0f), iconInk, 2.0f);
        draw->AddLine(ImVec2(iconCenter.x - 10.0f, iconCenter.y),
                      ImVec2(iconCenter.x - 6.0f, iconCenter.y), iconInk, 2.0f);
        draw->AddLine(ImVec2(iconCenter.x + 6.0f, iconCenter.y),
                      ImVec2(iconCenter.x + 10.0f, iconCenter.y), iconInk, 2.0f);
      }
      if (ImGui::IsMouseHoveringRect(ImVec2(hit.box.x, hit.box.y),
                                     ImVec2(hit.box.Right(), hit.box.Bottom()))) {
        ImGui::SetTooltip("%s", hit.text.c_str());
      }
      continue;
    }
    draw->AddText(ImVec2(hit.box.x, hit.box.y), ToIm(p.ink), hit.text.c_str());
  }
  draw->AddText(ImVec2(8.0f, layout_.sidefoot.y + 68.0f), ToIm(p.muted),
                "Spaceship Command");
  ImGui::PopFont();

  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

bool ShellView::HasWrenExterior(const Simulation& sim) const {
  const Texture2D* texture = static_cast<const Texture2D*>(wrenExteriorArt_);
  if (sim.State().shipClass != "wren" || texture == nullptr || texture->id == 0 ||
      texture->width <= 0 || texture->height <= 0) return false;
  // Wren exterior artwork remains visible throughout the voyage. The callouts
  // are built from sim.Rooms(), so only installed rooms are usable.
  return true;
}

ShellView::ExteriorLayout ShellView::BuildExteriorLayout(const Simulation& sim, const Area& stage) const {
  ExteriorLayout out;
  const int textSize = TypePx(10);
  out.lineHeight = std::max(static_cast<float>(textSize) + 3.0f, theme_.LineHeight());
  const float sideW = std::max(100.0f, std::min(245.0f, stage.w * 0.235f));
  std::vector<Room> rooms = sim.Rooms();
  std::stable_sort(rooms.begin(), rooms.end(), [](const Room& a, const Room& b) {
    return a.box.x < b.box.x;
  });
  // Keep the approximate fore/aft split, then order each side top-to-bottom
  // so leader lines do not needlessly cross one another.
  const auto middle = rooms.begin() + static_cast<std::ptrdiff_t>((rooms.size() + 1) / 2);
  const auto byHeight = [](const Room& a, const Room& b) {
    return a.box.y + a.box.h / 2.0 < b.box.y + b.box.h / 2.0;
  };
  std::stable_sort(rooms.begin(), middle, byHeight);
  std::stable_sort(middle, rooms.end(), byHeight);
  const std::size_t rows = (rooms.size() + 1) / 2;
  struct CalloutPlacement {
    bool left = false;
    std::size_t row = 0;
    float xInset = 0.0f;
    float width = 0.0f;
    float rowFraction = 0.0f;
  };
  std::vector<CalloutPlacement> placements;
  std::vector<std::vector<std::string>> labels;
  placements.reserve(rooms.size());
  labels.reserve(rooms.size());
  std::size_t maxLines = 1;
  for (std::size_t i = 0; i < rooms.size(); ++i) {
    const bool left = i < rows;
    const std::size_t row = left ? i : i - rows;
    const float rowT = rows > 1 ? static_cast<float>(row) / static_cast<float>(rows - 1) : 0.0f;
    CalloutPlacement placement{left, row, 0.0f, sideW, 0.69f * rowT};
    if (rows == 3) {
      // Match the reference composition: each lower callout steps inward by
      // a different amount on each side instead of forming two rigid columns.
      static constexpr float leftY[] = {0.0f, 0.354f, 0.542f};
      static constexpr float rightY[] = {0.0f, 0.306f, 0.527f};
      static constexpr float leftWidth[] = {1.0f, 0.90f, 0.78f};
      static constexpr float rightWidth[] = {1.0f, 0.93f, 0.80f};
      const std::size_t slot = std::min<std::size_t>(row, 2);
      const float baseWidth = std::max(1.0f, sideW - 8.0f);
      placement.width = baseWidth * (left ? leftWidth[slot] : rightWidth[slot]);
      placement.xInset = baseWidth - placement.width;
      placement.rowFraction = left ? leftY[slot] : rightY[slot];
    }
    placements.push_back(placement);
    labels.push_back(WrapText(rooms[i].name, std::max(1.0f, placement.width - 16.0f),
                              textSize, SelectFont(textSize, false)));
    maxLines = std::max(maxLines, labels.back().size());
  }
  const float rowH = static_cast<float>(maxLines) * out.lineHeight + 10.0f;
  float bodyH = std::max(Metrics::kHomeStageMinH, stage.h);
  if (rows > 3) {
    bodyH = std::max(bodyH, (rowH + 8.0f) * static_cast<float>(rows - 1) / 0.69f);
  }
  const float bodyY = stage.y;
  out.height = bodyH;
  const Texture2D* texture = static_cast<const Texture2D*>(wrenExteriorArt_);
  const float aspect = texture && texture->width > 0 ? static_cast<float>(texture->height) / texture->width : 420.0f / 1340.0f;
  const float imageW = std::max(1.0f, stage.w - sideW * 2.0f - 32.0f);
  const float imageH = std::min(imageW * aspect, bodyH * 0.75f);
  const float fittedW = imageH / aspect;
  out.image = {stage.x + (stage.w - fittedW) / 2.0f,
               bodyY + bodyH * 0.40f - imageH / 2.0f, fittedW, imageH};
  for (std::size_t i = 0; i < rooms.size(); ++i) {
    const CalloutPlacement& placement = placements[i];
    ExteriorCallout item;
    item.id = rooms[i].id;
    item.lines = std::move(labels[i]);
    item.left = placement.left;
    const float boxX = placement.left
        ? stage.x + 2.0f + placement.xInset
        : stage.Right() - 2.0f - placement.width;
    item.box = {boxX, bodyY + bodyH * placement.rowFraction,
                placement.width, rowH - 4.0f};
    // Conceptual anchor only: it ties a callout to the exterior visually and
    // does not claim the supplied sketch is an engineering deck layout.
    const float nx = std::clamp(static_cast<float>((rooms[i].box.x + rooms[i].box.w * 0.5) / 600.0), 0.0f, 1.0f);
    const float ny = std::clamp(static_cast<float>((rooms[i].box.y + rooms[i].box.h * 0.5) / 365.0), 0.0f, 1.0f);
    item.anchor = {out.image.x + out.image.w * (0.07f + 0.86f * nx),
                   out.image.y + out.image.h * (0.30f + 0.42f * ny)};
    out.callouts.push_back(std::move(item));
  }
  return out;
}

void ShellView::PaintWrenExterior(const Simulation& sim) {
  const Texture2D* texture = static_cast<const Texture2D*>(wrenExteriorArt_);
  const Palette& p = theme_.palette;
  const Area image = exterior_.image;
  DrawTexturePro(*texture, Rectangle{0, 0, static_cast<float>(texture->width), static_cast<float>(texture->height)},
                 Rectangle{image.x, image.y, image.w, image.h}, Vector2{0, 0}, 0, WHITE);
  const auto hovered = [&](const ExteriorCallout& item) {
    return layout_.content.Contains(mouseX_, mouseY_) && item.box.Contains(mouseX_, mouseY_ + contentScroll_);
  };
  // Lines first and labels last: another room's leader can never paint over
  // a label. The highlights are decorative; neither they nor hover tick time.
  for (const ExteriorCallout& item : exterior_.callouts) {
    const bool hover = hovered(item);
    const Color line = ToRay(hover ? p.coffeeDark : p.borderDark);
    const float edgeX = item.left ? item.box.Right() : item.box.x;
    const float elbowX = item.left ? image.x - 8.0f : image.Right() + 8.0f;
    const Vector2 from{edgeX, item.box.y + item.box.h * 0.5f};
    const Vector2 elbow{elbowX, from.y};
    DrawLineEx(from, elbow, 1.0f, line);
    DrawLineEx(elbow, item.anchor, 1.0f, line);
    DrawCircleV(item.anchor, hover ? 4.0f : 2.5f, line);
  }
  for (const ExteriorCallout& item : exterior_.callouts) {
    const bool hover = hovered(item);
    if (hover) {
      const float pulse = sim.State().reducedMotion ? 0.20f :
          0.15f + 0.05f * static_cast<float>(0.5 + 0.5 * std::sin(frameTimeMs_ / 650.0));
      DrawEllipse(static_cast<int>(item.anchor.x), static_cast<int>(item.anchor.y),
                  std::max(12.0f, image.w * 0.045f), std::max(8.0f, image.h * 0.09f),
                  Fade(::Color{255, 224, 156, 255}, pulse));
    }
    DrawRectangle(static_cast<int>(item.box.x), static_cast<int>(item.box.y),
                  static_cast<int>(item.box.w), static_cast<int>(item.box.h), ToRay(hover ? p.panel3 : p.panel));
    DrawRectangleLines(static_cast<int>(item.box.x), static_cast<int>(item.box.y),
                       static_cast<int>(item.box.w), static_cast<int>(item.box.h), ToRay(p.border));
    float y = item.box.y + 4.0f;
    for (const auto& lineText : item.lines) {
      Text(lineText.c_str(), static_cast<int>(item.box.x + 8), static_cast<int>(y), TypePx(10), ToRay(p.ink));
      y += exterior_.lineHeight;
    }
  }
}

void ShellView::PaintIdle(Simulation& sim) {
  const Theme& theme = theme_;

  // ---- stat and info rails ----------------------------------------------
  for (const RailCell& cell : idle_.stats) {
    DrawRectangle(static_cast<int>(cell.box.x), static_cast<int>(cell.box.y),
                  static_cast<int>(cell.box.w), static_cast<int>(cell.box.h),
                  ToRay(theme.palette.panel));
    DrawRectangleLines(static_cast<int>(cell.box.x), static_cast<int>(cell.box.y),
                       static_cast<int>(cell.box.w), static_cast<int>(cell.box.h),
                       ToRay(theme.palette.border));
    // Tighter spacing makes the four gauges shallower without shrinking type.
    const int labelSize = TypePx(11);
    const int valueSize = TypePx(20);
    const int unitSize = TypePx(10);
    const float labelY = cell.box.y + 4.0f;
    const float valueY = cell.box.y + 4.0f + labelSize + 1.0f;
    Text(cell.label.c_str(), static_cast<int>(cell.box.x + 6.0f),
         static_cast<int>(labelY), labelSize, ToRay(theme.palette.muted));
    Text(cell.value.c_str(), static_cast<int>(cell.box.x + 6.0f),
         static_cast<int>(valueY), valueSize, ToRay(theme.palette.ink), true);
    if (!cell.unit.empty()) {
      const float valueW = TextWidth(cell.value.c_str(), valueSize, true);
      const float unitW = TextWidth(cell.unit.c_str(), unitSize, false);
      const float unitX = cell.box.x + 6.0f + valueW + 4.0f;
      if (unitX + unitW <= cell.box.Right() - 6.0f) {
        Text(cell.unit.c_str(), static_cast<int>(unitX), static_cast<int>(valueY),
             unitSize, ToRay(theme.palette.muted));
      }
    }
    const float bar = cell.box.w - 12.0f;
    DrawRectangle(static_cast<int>(cell.box.x + 6.0f),
                  static_cast<int>(cell.box.Bottom() - 8.0f), static_cast<int>(bar), 4,
                  ToRay(theme.palette.panel3));
    DrawRectangle(static_cast<int>(cell.box.x + 6.0f),
                  static_cast<int>(cell.box.Bottom() - 8.0f),
                  static_cast<int>(bar * std::max(0.0, std::min(100.0, cell.reading)) / 100.0),
                  4, HexColor(cell.colour, ToRay(theme.palette.coffee)));
  }

  if (HasWrenExterior(sim) && exteriorView_) {
    PaintWrenExterior(sim);
  } else {
    PaintDiagram(idle_.diagram, idle_.stageArea, idleDiagramScale_);
  }
  // ---- info rail, navigation and dock strip -----------------------------
  auto paintStrip = [this, &theme](const Strip& strip) {
    DrawRectangle(static_cast<int>(strip.box.x), static_cast<int>(strip.box.y),
                  static_cast<int>(strip.box.w), static_cast<int>(strip.box.h),
                  ToRay(theme.palette.panel));
    DrawRectangleLines(static_cast<int>(strip.box.x), static_cast<int>(strip.box.y),
                       static_cast<int>(strip.box.w), static_cast<int>(strip.box.h),
                       ToRay(theme.palette.border));
    const float mainY = strip.box.y + 6.0f + TypePx(11) + 6.0f;
    Text(strip.label.c_str(), static_cast<int>(strip.box.x + 8.0f),
         static_cast<int>(strip.box.y + 6.0f), TypePx(11), ToRay(theme.palette.muted));
    const auto mainLines = WrapText(strip.main, std::max(1.0f, strip.box.w - 16.0f),
                                    TypePx(13), SelectFont(TypePx(13), false));
    float y = mainY;
    for (const auto& line : mainLines) {
      Text(line.c_str(), static_cast<int>(strip.box.x + 8.0f), static_cast<int>(y),
           TypePx(13), ToRay(theme.palette.ink));
      y += TypePx(13) + 3.0f;
    }
  };

  const int labelPx = theme.HomeInfoLabelPx();
  const int valuePx = theme.HomeInfoValuePx();
  for (std::size_t i = 0; i < idle_.infos.size(); ++i) {
    const RailCell& cell = idle_.infos[i];
    const float width = std::max(0.0f, cell.box.w);
    DrawRectangle(static_cast<int>(cell.box.x), static_cast<int>(cell.box.y),
                  static_cast<int>(width), static_cast<int>(cell.box.h), ToRay(theme.palette.panel));
    DrawRectangleLines(static_cast<int>(cell.box.x), static_cast<int>(cell.box.y),
                       static_cast<int>(width), static_cast<int>(cell.box.h), ToRay(theme.palette.border));
    const float textWidth = std::max(1.0f, width - 20.0f);
    float y = cell.box.y + 6.0f;
    for (const auto& line : WrapText(cell.label, textWidth, labelPx, SelectFont(labelPx, false))) {
      Text(line.c_str(), static_cast<int>(cell.box.x + 10.0f), static_cast<int>(y),
           labelPx, ToRay(theme.palette.muted));
      y += labelPx + 3.0f;
    }
    y += 3.0f;
    for (const auto& line : WrapText(cell.value, textWidth, valuePx, SelectFont(valuePx, false))) {
      Text(line.c_str(), static_cast<int>(cell.box.x + 10.0f), static_cast<int>(y),
           valuePx, ToRay(theme.palette.ink));
      y += valuePx + 3.0f;
    }
  }
  paintStrip(idle_.nav);
  for (const HomeEventCard& event : idle_.events) {
    const Area& box = event.box;
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                  static_cast<int>(box.w), static_cast<int>(box.h), ToRay(theme.palette.panel));
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                       static_cast<int>(box.w), static_cast<int>(box.h), ToRay(theme.palette.border));
    Text(event.heading.c_str(), static_cast<int>(box.x + 8.0f),
         static_cast<int>(box.y + 6.0f), TypePx(11), ToRay(theme.palette.muted));
    const auto lines = WrapText(event.label, std::max(1.0f, box.w - 16.0f),
                                TypePx(13), SelectFont(TypePx(13), false));
    float eventY = box.y + 6.0f + TypePx(11) + 6.0f;
    for (const std::string& line : lines) {
      Text(line.c_str(), static_cast<int>(box.x + 8.0f), static_cast<int>(eventY),
           TypePx(13), ToRay(theme.palette.ink));
      eventY += TypePx(13) + 3.0f;
    }
  }

  // ---- feed --------------------------------------------------------------
  const float feedHeaderHeight = static_cast<float>(TypePx(11)) + 12.0f;
  DrawRectangle(static_cast<int>(idle_.feedBox.x), static_cast<int>(idle_.feedBox.y),
                static_cast<int>(idle_.feedBox.w), static_cast<int>(idle_.feedBox.h),
                ToRay(theme.palette.panel));
  DrawRectangleLines(static_cast<int>(idle_.feedBox.x),
                     static_cast<int>(idle_.feedBox.y),
                     static_cast<int>(idle_.feedBox.w),
                     static_cast<int>(idle_.feedBox.h), ToRay(theme.palette.border));
  Text(idle_.feedHead.c_str(), static_cast<int>(idle_.feedBox.x + 8.0f),
       static_cast<int>(idle_.feedBox.y + 6.0f), TypePx(11), ToRay(theme.palette.coffee));
  const bool trackFeed = paintingContent_;
  paintingContent_ = false;
  feedBox_ = idle_.feedBox;
  std::vector<std::string> log;
  for (auto it = sim.State().feed.rbegin(); it != sim.State().feed.rend(); ++it)
    log.push_back(it->stamp.empty() ? it->text : it->stamp + "  " + it->text);
  if (feedRevision_ != sim.FeedRevision() || feedSeen_ != log) {
    feedScroll_ = 0;
    draggingFeed_ = false;
    feedRevision_ = sim.FeedRevision();
    feedSeen_ = log;
  }
  const int size = TypePx(12);
  const float lineH = std::max(theme.LineHeight(), static_cast<float>(size) + 4);
  std::vector<std::string> rows;
  for (const auto& entry : log) {
    const auto wrapped = WrapText(entry, std::max(1.0f, feedBox_.w - 28), size, SelectFont(size, false));
    rows.insert(rows.end(), wrapped.begin(), wrapped.end());
  }
  const float feedContentTop = feedBox_.y + feedHeaderHeight;
  const float feedContentHeight = std::max(1.0f, feedBox_.h - feedHeaderHeight - 4.0f);
  const float visibleH = feedContentHeight;
  feedMaxScroll_ = std::max(0.0f, rows.size() * lineH - visibleH);
  feedScroll_ = std::clamp(feedScroll_, 0.0f, feedMaxScroll_);
  BeginScissorMode(static_cast<int>(feedBox_.x + 1),
                   static_cast<int>(feedContentTop - contentScroll_ + 1),
                   std::max(0, static_cast<int>(feedBox_.w - 2)),
                   std::max(0, static_cast<int>(feedContentHeight - 1.0f)));
  float y = feedContentTop + 2.0f - feedScroll_;
  for (const auto& row : rows) {
    if (y + lineH > feedContentTop && y < feedContentTop + feedContentHeight) {
      Text(row.c_str(), static_cast<int>(feedBox_.x + 6), static_cast<int>(y), size, ToRay(theme.palette.ink));
    }
    y += lineH;
  }
  feedThumb_ = {};
  if (feedMaxScroll_ > 0) {
    const float trackH = feedContentHeight;
    const float thumbH = std::min(trackH, std::max(18.0f, trackH * visibleH / (visibleH + feedMaxScroll_)));
    feedThumb_ = {feedBox_.Right() - 10, feedContentTop + (trackH - thumbH) * feedScroll_ / feedMaxScroll_, 8, thumbH};
    DrawRectangle(static_cast<int>(feedThumb_.x), static_cast<int>(feedContentTop), 8,
                  static_cast<int>(trackH), ToRay(theme.palette.panel3));
    DrawRectangle(static_cast<int>(feedThumb_.x), static_cast<int>(feedThumb_.y), 8,
                  static_cast<int>(thumbH), ToRay(theme.palette.borderDark));
  }
  EndScissorMode();
  paintingContent_ = trackFeed;
  contentPaintBottom_ = std::max(contentPaintBottom_, feedBox_.Bottom());
  (void)sim;
}

void ShellView::PaintPanel(const NavPanel& panel, const Area& box) {
  const Palette& p = theme_.palette;
  DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.panel));
  DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                     static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.border));
  BeginScissorMode(static_cast<int>(box.x + 1.0f), static_cast<int>(box.y + 1.0f - contentScroll_),
                   std::max(0, static_cast<int>(box.w - 2.0f)),
                   std::max(0, static_cast<int>(box.h - 2.0f)));
  const int small = TypePx(10);
  const float line = TypeStep(14.0f);
  Text(panel.title.c_str(), static_cast<int>(box.x + 8.0f),
       static_cast<int>(box.y + 5.0f), TypePx(11), ToRay(p.coffee));
  float y = box.y + TypeStep(24.0f);
  const float textW = std::max(1.0f, box.w - 16.0f);
    auto paintWrapped = [&](const std::string& value, float x, float width,
                            ::Color color) {
      const std::vector<std::string> rows =
          WrapText(value, std::max(1.0f, width), small, SelectFont(small, false));
      for (const std::string& row : rows) {
      if (y + line > box.Bottom() - 3.0f) { break; }
      Text(row.c_str(), static_cast<int>(x), static_cast<int>(y), small, color);
      y += line;
    }
  };
  for (const PanelLine& item : panel.lines) {
    if (y >= box.Bottom() - 3.0f) { break; }
    switch (item.kind) {
      case PanelLine::Kind::Name:
        for (const auto& row : WrapText(item.value, textW, TypePx(18), SelectFont(TypePx(18), false))) {
          Text(row.c_str(), static_cast<int>(box.x + 8.0f), static_cast<int>(y), TypePx(18), ToRay(p.ink));
          y += std::max(TypeStep(23.0f), static_cast<float>(TypePx(18)) + 4.0f);
        }
        break;
      case PanelLine::Kind::Sub:
        paintWrapped(item.value, box.x + 8.0f, textW, ToRay(p.muted));
        y += TypeStep(2.0f);
        break;
      case PanelLine::Kind::Bar: {
        const float w = box.w - 16.0f;
        DrawRectangle(static_cast<int>(box.x + 8.0f), static_cast<int>(y),
                      static_cast<int>(w), 5, ToRay(p.panel3));
        DrawRectangle(static_cast<int>(box.x + 8.0f), static_cast<int>(y),
          static_cast<int>(w * std::max(0.0, std::min(100.0, item.pct)) / 100.0),
          5, HexColor(item.tone, ToRay(p.coffee)));
        y += TypeStep(11.0f);
        break;
      }
      case PanelLine::Kind::Dim:
      case PanelLine::Kind::Note:
        paintWrapped(item.value, box.x + 8.0f, textW, ToRay(p.muted));
        y += TypeStep(2.0f);
        break;
      case PanelLine::Kind::Field: {
        const float x = box.x + 8.0f;
        const float keyW = TextWidth(item.key.c_str(), small);
        const float valueW = TextWidth(item.value.c_str(), small);
        const float roomW = textW - keyW - TypeStep(8.0f);
        Text(item.key.c_str(), static_cast<int>(x), static_cast<int>(y), small, ToRay(p.muted));
        if (valueW <= roomW) {
          Text(item.value.c_str(), static_cast<int>(box.Right() - 8.0f - valueW),
               static_cast<int>(y), small, ToRay(p.ink));
          y += line;
        } else {
          y += line;
          paintWrapped(item.value, x + TypeStep(6.0f), textW - TypeStep(6.0f), ToRay(p.ink));
        }
        break;
      }
    }
  }
  EndScissorMode();
}

float ShellView::MeasurePanelHeight(const NavPanel& panel, float width) const {
  const float textWidth = std::max(1.0f, width - 16.0f);
  const int textPx = TypePx(10);
  const float lineHeight = TypeStep(14.0f);
  const auto wrappedRows = [this, textPx](const std::string& text, float available) {
    return static_cast<float>(WrapText(text, std::max(1.0f, available), textPx,
                                       SelectFont(textPx, false)).size());
  };
  float y = TypeStep(24.0f);
  for (const PanelLine& item : panel.lines) {
    switch (item.kind) {
      case PanelLine::Kind::Name:
        y += WrapText(item.value, textWidth, TypePx(18), SelectFont(TypePx(18), false)).size() *
             std::max(TypeStep(23.0f), static_cast<float>(TypePx(18)) + 4.0f);
        break;
      case PanelLine::Kind::Sub:
      case PanelLine::Kind::Dim:
      case PanelLine::Kind::Note:
        y += wrappedRows(item.value, textWidth) * lineHeight + TypeStep(2.0f);
        break;
      case PanelLine::Kind::Bar:
        y += TypeStep(11.0f);
        break;
      case PanelLine::Kind::Field: {
        const float keyWidth = TextWidth(item.key.c_str(), textPx);
        const float valueWidth = TextWidth(item.value.c_str(), textPx);
        const float valueRoom = textWidth - keyWidth - TypeStep(8.0f);
        y += lineHeight;
        if (valueWidth > valueRoom) {
          y += wrappedRows(item.value, textWidth - TypeStep(6.0f)) * lineHeight;
        }
        break;
      }
    }
  }
  return y + lineHeight + 4.0f;
}

// Native painting of the literal scene styles in HTA lines 918–1199.
// Geometry stays in the existing part models; CSS borders are inside the box.
bool ShellView::PaintScenePart(const MachinePart& part, const Area& origin) {
  const SceneStyle style = SceneStyleFor(part.cls);
  if (!*style.cls) return false;
  const float x = origin.x + part.box.x, y = origin.y + part.box.y;
  const float w = std::max(0.0f, part.box.w), h = std::max(0.0f, part.box.h);
  const Rectangle box{x, y, w, h};
  const bool tonedFill = part.text.empty() && !part.tone.empty();
  const std::string background = tonedFill ? part.tone : style.background;
  const float rounding = std::min(1.0f, 2.0f * std::max(0, style.radius) / std::max(1.0f, std::min(w, h)));
  if (!background.empty() && w > 0 && h > 0) {
    const Color ink = HexColor(background, ToRay(theme_.palette.panel));
    if (style.radius == -1) DrawEllipse(static_cast<int>(x + w / 2), static_cast<int>(y + h / 2), w / 2, h / 2, ink);
    else if (style.radius > 0) DrawRectangleRounded(box, rounding, 12, ink);
    else DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), ink);
  }
  if (style.width > 0 && *style.border && w > 0 && h > 0) {
    const Color ink = HexColor(style.border, ToRay(theme_.palette.border));
    const float thick = static_cast<float>(style.width);
    if (style.radius == -1 && w == h) {
      const Vector2 center{x + w / 2, y + h / 2};
      if (style.dashed) {
        for (float angle = 0; angle < 360; angle += 12) DrawRing(center, std::max(0.0f, w / 2 - thick), w / 2, angle, angle + 6, 3, ink);
      } else DrawRing(center, std::max(0.0f, w / 2 - thick), w / 2, 0, 360, 64, ink);
    } else if (style.radius > 0 && !style.dashed && style.sides == 15) {
      DrawRectangleRoundedLinesEx(box, rounding, 12, thick, ink);
    } else {
      auto edge = [&](float ax, float ay, float bx, float by) {
        const float dx = bx - ax, dy = by - ay, length = std::sqrt(dx * dx + dy * dy);
        if (length <= 0) return;
        if (!style.dashed) { DrawLineEx({ax, ay}, {bx, by}, thick, ink); return; }
        for (float pos = 0; pos < length; pos += 6) {
          const float end = std::min(pos + 3, length);
          DrawLineEx({ax + dx * pos / length, ay + dy * pos / length},
                     {ax + dx * end / length, ay + dy * end / length}, thick, ink);
        }
      };
      const float d = thick / 2;
      if (style.sides & 1) edge(x + d, y + d, x + w - d, y + d);
      if (style.sides & 2) edge(x + w - d, y + d, x + w - d, y + h - d);
      if (style.sides & 4) edge(x + d, y + h - d, x + w - d, y + h - d);
      if (style.sides & 8) edge(x + d, y + d, x + d, y + h - d);
    }
  }
  if (!part.text.empty()) {
    const int size = TypePx(style.fontPx);
    float tx = x;
    if (style.align) {
      const float space = w - TextWidth(part.text.c_str(), size, style.mono);
      tx += style.align == 1 ? space / 2 : space;
    }
    const std::string ink = part.tone.empty() ? style.ink : part.tone;
    Text(part.text.c_str(), static_cast<int>(tx), static_cast<int>(y), size,
         HexColor(ink, ToRay(theme_.palette.ink)), style.mono);
  }
  return true;
}

void ShellView::PaintStage(const TaskStage& stage, const Area& origin) {
  const Palette& p = theme_.palette;
  // HTA #machine is not overridden by the dark palette; the filter and
  // galley housings are. Keep the frame behind (not over) the scenery.
  const bool darkHousing = theme_.id != "classic" && stage.id != "machine";
  const Color housing = darkHousing ? ToRay(p.chrome) : HexColor("#fdfbf7", ToRay(p.panel));
  const Color edge = darkHousing ? ToRay(p.borderDark) : HexColor("#8c6f4e", ToRay(p.borderDark));
  DrawRectangle(static_cast<int>(origin.x), static_cast<int>(origin.y),
                static_cast<int>(stage.width), static_cast<int>(stage.height), housing);
  DrawRectangleLines(static_cast<int>(origin.x), static_cast<int>(origin.y),
                     static_cast<int>(stage.width), static_cast<int>(stage.height), edge);
  for (const MachinePart& part : stage.parts) {
    if (PaintMessStockIcon(part, origin)) continue;
    if (PaintScenePart(part, origin)) continue;
    const int x = static_cast<int>(origin.x + part.box.x);
    const int y = static_cast<int>(origin.y + part.box.y);
    const int w = std::max(1, static_cast<int>(part.box.w));
    const int h = std::max(1, static_cast<int>(part.box.h));
    if (!part.tone.empty()) {
      const Color tone = HexColor(part.tone, ToRay(p.muted));
      if (!part.text.empty()) {
        Text(part.text.c_str(), x, y, TypePx(8), tone);
      } else if (part.box.w > 0 && part.box.h > 0) {
        DrawRectangle(x, y, w, h, tone);
      }
      continue;
    }
    if (part.cls.find("pipe") != std::string::npos ||
        part.cls.find("duct") != std::string::npos ||
        part.cls.find("counter") != std::string::npos) {
      DrawRectangle(x, y, w, h, ToRay(p.panel3));
      DrawRectangleLines(x, y, w, h, ToRay(p.border));
    } else if (part.cls.find("ring") != std::string::npos ||
               part.cls.find("wheel") != std::string::npos ||
               part.cls.find("dial") != std::string::npos) {
      DrawCircle(x + w / 2, y + h / 2, static_cast<float>(std::max(w, h)) / 2.0f,
                 ToRay(p.panel2));
      DrawCircleLines(x + w / 2, y + h / 2,
                      static_cast<float>(std::max(w, h)) / 2.0f, ToRay(p.coffee));
    } else if (part.cls.find("fill") != std::string::npos) {
      DrawRectangle(x, y, w, h, ToRay(p.sage));
    } else if (part.cls.find("track") != std::string::npos ||
               part.cls.find("box") != std::string::npos ||
               part.cls.find("slot") != std::string::npos ||
               part.cls.find("plate") != std::string::npos ||
               part.cls.find("rack") != std::string::npos) {
      DrawRectangle(x, y, w, h, ToRay(p.panel));
      DrawRectangleLines(x, y, w, h, ToRay(p.borderDark));
    } else {
      DrawRectangle(x, y, w, h, ToRay(p.coffeeLight));
    }
    if (!part.text.empty() && part.text[0] != 'm') {
      Text(part.text.c_str(), x + 2, y + 1, TypePx(8) , ToRay(p.muted));
    }
  }

  for (const LiveControl& control : stage.controls) {
    const Area box{origin.x + control.box.x, origin.y + control.box.y,
                   control.box.w, 26.0f};
    const bool locked = control.button.state == ButtonState::Cooldown;
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                  static_cast<int>(box.w), static_cast<int>(box.h),
                  ToRay(control.button.enabled ? p.panel2 : p.panel3));
    PaintProgressFill(box, control.button.fillPercent);
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                       static_cast<int>(box.w), static_cast<int>(box.h),
                       ToRay(p.borderDark));
    Text(control.button.text.c_str(), static_cast<int>(box.x + 6.0f),
             static_cast<int>(box.y + 7.0f), TypePx(10) ,
             ToRay(locked ? p.terracotta : p.ink));
    taskButtons_.push_back({box, control.button.taskId});
  }
}

float ShellView::PaintActionBand(const std::string& title, const std::string& note,
                                const std::vector<ProgressButton>& buttons, const std::string& warning, bool measureOnly) {
  const auto& p = theme_.palette;
  const float left = layout_.content.x + 8, width = layout_.content.w - 16;
  const float inner = std::max(1.0f, width - 24), gap = 10;
  const int size = TypePx(10);
  const float line = std::max(theme_.LineHeight(), static_cast<float>(size) + 4);
  struct ButtonBox { std::size_t index; float width, height; std::vector<std::string> text; };
  std::vector<std::vector<ButtonBox>> rows(1);
  float used = 0;
  for (std::size_t i = 0; i < buttons.size(); ++i) {
    const float w = std::min(inner, std::max(200.0f, TextWidth(buttons[i].text.c_str(), size) + 24));
    auto text = WrapText(buttons[i].text, std::max(1.0f, w - 20), size, SelectFont(size, false));
    if (!rows.back().empty() && used + gap + w > inner) { rows.emplace_back(); used = 0; }
    if (!rows.back().empty()) used += gap;
    used += w;
    rows.back().push_back({i, w, text.size() * line + 12, std::move(text)});
  }
  const auto notes = note.empty() ? std::vector<std::string>{} : WrapText(note, inner, size, SelectFont(size, false));
  const auto warnings = warning.empty() ? std::vector<std::string>{} : WrapText(warning, inner, size, SelectFont(size, false));
  float buttonsH = 0;
  for (const auto& row : rows) {
    float h = 0; for (const auto& button : row) h = std::max(h, button.height);
    if (h) buttonsH += h + gap;
  }
  const float h = 12 + line + 10 + buttonsH + (notes.size() + warnings.size()) * line + 12;
  if (measureOnly) return h;
  actionBandTop_ = layout_.content.Bottom() - 8 - h;
  const float top = actionBandTop_ + contentScroll_;
  // A pinned overlay must not inflate the scroll extent of the body beneath it.
  const bool tracked = paintingContent_; paintingContent_ = false;
  DrawRectangle(static_cast<int>(left), static_cast<int>(top), static_cast<int>(width), static_cast<int>(h), ToRay(p.panel));
  DrawRectangleLines(static_cast<int>(left), static_cast<int>(top), static_cast<int>(width), static_cast<int>(h), ToRay(p.border));
  Text(title.c_str(), static_cast<int>(left + (width - TextWidth(title.c_str(), size)) / 2), static_cast<int>(top + 8), size, ToRay(p.coffee));
  float y = top + 12 + line + 10;
  for (const auto& row : rows) {
    float rowW = 0, rowH = 0;
    for (const auto& button : row) { rowW += button.width; rowH = std::max(rowH, button.height); }
    if (!row.empty()) rowW += gap * (row.size() - 1);
    float x = left + (width - rowW) / 2;
    for (const auto& cell : row) {
      const auto& button = buttons[cell.index];
      const Area box{x, y, cell.width, rowH};
      DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(box.w), static_cast<int>(box.h), ToRay(button.enabled ? p.control : p.panel3));
      PaintProgressFill(box, button.fillPercent);
      DrawRectangleLines(static_cast<int>(x), static_cast<int>(y), static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.borderDark));
      float textY = y + (rowH - cell.text.size() * line) / 2;
      for (const auto& text : cell.text) {
        Text(text.c_str(), static_cast<int>(x + (box.w - TextWidth(text.c_str(), size)) / 2), static_cast<int>(textY), size,
             ToRay(button.state == ButtonState::Cooldown ? p.terracotta : p.ink));
        textY += line;
      }
      actionBandHits_.push_back({box, button.taskId, ""});
      x += box.w + gap;
    }
    y += rowH + gap;
  }
  for (const auto& text : notes) {
    Text(text.c_str(), static_cast<int>(left + (width - TextWidth(text.c_str(), size)) / 2), static_cast<int>(y), size, ToRay(p.muted));
    y += line;
  }
  for (const auto& text : warnings) {
    Text(text.c_str(), static_cast<int>(left + (width - TextWidth(text.c_str(), size)) / 2), static_cast<int>(y), size, ToRay(p.terracotta));
    y += line;
  }
  paintingContent_ = tracked;
  return h;
}

void ShellView::PaintTaskScreen(Simulation& sim) {
  taskButtons_.clear();
  const Palette& p = theme_.palette;
  const TaskScreenModel& screen = taskScreen_;
  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;
  const float column = 250.0f;

  const float headerH = PaintTaskHeader(screen.head, left, layout_.content.y);

  const float bodyTop = layout_.content.y + headerH + 8;
  const float stageX = left + (right - left - screen.stage.width) / 2;
  const bool beside = !screen.hasStage ||
      (stageX >= left + column + 16 && stageX + screen.stage.width <= right - column - 16);
  const float panelTop = screen.hasStage && !beside ? bodyTop + screen.stage.height + 12 : bodyTop;
  float y = panelTop;
  for (const NavPanel& panel : screen.left) {
    const float panelW = panel.placement == "np-full" ? right - left : column;
    const float panelH = std::max(118.0f, MeasurePanelHeight(panel, panelW));
    PaintPanel(panel, Area{left, y, panelW, panelH});
    y += panelH + 6;
  }
  float ry = panelTop;
  for (const NavPanel& panel : screen.right) {
    const float panelH = std::max(150.0f, MeasurePanelHeight(panel, column));
    PaintPanel(panel, Area{right - column, ry, column, panelH});
    ry += panelH + 6;
  }
  float stageBottom = bodyTop;
  if (screen.hasStage) {
    // Fixed-size instrument, centered independently of viewport width. When
    // the side panels cannot fit beside it, they flow beneath, never over it.
    const Area origin{stageX, bodyTop, screen.stage.width, screen.stage.height};
    PaintStage(screen.stage, origin);
    const float uy = beside ? origin.Bottom() + 4 : std::max({origin.Bottom(), y, ry}) + 12;
    const float gap = 6;
    const float panelW = (origin.w - gap * std::max(0, static_cast<int>(screen.under.size()) - 1)) /
                         std::max(1, static_cast<int>(screen.under.size()));
    float underH = 0;
    for (const auto& panel : screen.under) underH = std::max(underH, MeasurePanelHeight(panel, panelW));
    stageBottom = uy + underH;
    for (std::size_t i = 0; i < screen.under.size(); ++i)
      PaintPanel(screen.under[i], {origin.x + i * (panelW + gap), uy, panelW, underH});
  }

  contentPaintBottom_ = std::max({contentPaintBottom_, y, ry, stageBottom});
  if (!screen.progress.empty()) { PaintActionBand("Actions", screen.bandNote, screen.progress); }
  (void)sim;
}

// The chart's celestial bodies: stars, worlds, moons, stations and dust.
namespace {
#include "ChartArt.inc"
}  // namespace

void ShellView::EnsureNavScreen(const Simulation& sim) {
  NavCacheKey key;
  key.valid = true;
  key.actions = sim.ActionCount();
  key.layer = sim.Map().layer;
  key.sector = sim.Map().sector;
  key.here = sim.Map().here;
  key.zoom = sim.Map().zoom;
  auto viewIt = sim.Views().find(key.layer);
  if (viewIt != sim.Views().end()) {
    key.panX = viewIt->second.px;
    key.panY = viewIt->second.py;
  }
  key.target = sim.State().navTarget.value_or(std::string());
  key.docked = sim.State().docked;
  key.reducedMotion = sim.State().reducedMotion;
  key.scannedCount = sim.State().scanned.size();
  key.fuel = sim.StatValue("fuel");
  key.energy = sim.StatValue("energy");
  key.drive = sim.State().engineCondition;
  key.sensor = sim.State().sim.sensor ? 1 : 0;
  key.sensorTier = sim.SensorTier();
  key.scanCost = sim.ScanCost();
  key.observedCount = sim.State().sim.observed.size();
  key.cargoUsed = sim.CargoUsed();
  key.cargoCap = sim.CargoStore().capacity;

  std::size_t taskSig = sim.State().ongoing.size();
  for (const Task& task : sim.State().ongoing) {
    taskSig ^= std::hash<std::string>{}(task.id) + 0x9e3779b9 + (taskSig << 6) + (taskSig >> 2);
    taskSig ^= std::hash<double>{}(task.left) + 0x9e3779b9 + (taskSig << 6) + (taskSig >> 2);
    taskSig ^= std::hash<std::string>{}(task.state) + 0x9e3779b9 + (taskSig << 6) + (taskSig >> 2);
  }
  key.ongoingSig = taskSig;

  std::size_t crewSig = sim.Crew().size();
  for (const CrewMember& member : sim.Crew()) {
    crewSig ^= std::hash<std::string>{}(member.job) + 0x9e3779b9 + (crewSig << 6) + (crewSig >> 2);
    crewSig ^= std::hash<double>{}(member.jobAt) + 0x9e3779b9 + (crewSig << 6) + (crewSig >> 2);
  }
  key.crewSig = crewSig;

  if (key == navCacheKey_) { return; }
  navCacheKey_ = std::move(key);
  navScreen_ = BuildNavScreen(sim);
  navRaster_ = BuildNavRaster(sim);
}

void ShellView::PaintNavScreen(Simulation& sim) {
  navHits_.clear();
  const Palette& p = theme_.palette;
  const TaskScreenModel& screen = navScreen_.screen;
  const NavChartView& chart = navScreen_.chart;

  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;
  const float leftColumn = 214.0f;
  const float rightColumn = 252.0f;
  const float middleLeft = left + leftColumn + 12.0f;
  const float middleRight = right - rightColumn - 12.0f;

  const float headerH = PaintTaskHeader(screen.head, left, layout_.content.y);

  // HTA #map fills the middle column up to 1100px. Its height is limited by
  // the remaining content height, 500px and half its width, with a 240px floor.
  const float spareW = std::max(1.0f, middleRight - middleLeft);
  const float chartW = std::min(1100.0f, spareW);
  const float chartTop = layout_.content.y + headerH + 8.0f;
  // Park at 240 and measure every other drawn item, as HTA layoutNav does.
  const float halfW = (chartW - 6) / 2;
  float underH = 112;
  for (const auto& panel : screen.under) underH = std::max(underH, MeasurePanelHeight(panel, halfW));
  float leftH = 0;
  for (const auto& panel : screen.left) leftH += std::max(96.0f, MeasurePanelHeight(panel, leftColumn)) + 4;
  const float rightH = screen.right.empty() ? 0 : std::max(190.0f, MeasurePanelHeight(screen.right[0], rightColumn));
  float tailH = screen.commands.size() * (theme_.LineHeight() + 12);
  for (const auto& note : navScreen_.notes)
    tailH += WrapText(note, right - left - 16, TypePx(9), SelectFont(TypePx(9), false)).size() * theme_.LineHeight() + 4;
  const float footerH = PaintActionBand("Target Actions", navScreen_.courseNote, screen.progress, "", true);
  const float parkedBody = std::max({240 + 22 + underH, leftH, rightH}) + 12 + tailH;
  const float otherH = headerH + 8 + parkedBody - 240 + footerH + 8;
  const float availableH = layout_.content.h - otherH - 4;
  // Preserve the existing logical viewport and cap/floor ordering.
  const float chartH = std::min({500.0f, chartW * 0.5f, std::max(240.0f, availableH)});
  const Area box{middleLeft + (spareW - chartW) / 2, chartTop, chartW, chartH};
  navChartBox_ = box;
  const ship::NavRaster& raster = navRaster_;
  const float sx = box.w / static_cast<float>(raster.width == 0 ? 1.0 : raster.width);
  const float sy = box.h / static_cast<float>(raster.height == 0 ? 1.0 : raster.height);
  navChartScaleX_ = sx;
  navChartScaleY_ = sy;
  const float bodyScale = std::min(sx, sy);
  const float clipMinX = box.x - 18.0f;
  const float clipMaxX = box.Right() + 18.0f;
  const float clipMinY = box.y - 18.0f;
  const float clipMaxY = box.Bottom() + 18.0f;

  DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.chart));
  DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                     static_cast<int>(box.w), static_cast<int>(box.h),
                     ToRay(p.borderDark));

  BeginScissorMode(static_cast<int>(box.x) + 1, static_cast<int>(box.y - contentScroll_) + 1,
                   static_cast<int>(box.w) - 2, static_cast<int>(box.h) - 2);

  // The rungs: depth lines across the chart, faint far, solid near.
  for (const NavRasterRung& rung : raster.rungs) {
    const int y = static_cast<int>(box.y + static_cast<float>(rung.y) * sy);
    DrawRectangle(static_cast<int>(box.x), y, static_cast<int>(box.w), 1,
                  Fade(ToRay(p.ink), static_cast<float>(rung.alpha)));
  }
  // The fan: hairlines converging on a point held above the top edge.
  for (const NavRasterLine& line : raster.lines) {
    const double pi = 3.14159265358979323846;
    const double rad = line.angle * pi / 180.0;
    const double dx = std::cos(rad);
    const double dy = std::sin(rad);
    const float x1 = box.x + static_cast<float>(raster.vx - dx * 4000) * sx;
    const float y1 = box.y + static_cast<float>(raster.vy - dy * 4000) * sy;
    const float x2 = box.x + static_cast<float>(raster.vx + dx * 4000) * sx;
    const float y2 = box.y + static_cast<float>(raster.vy + dy * 4000) * sy;
    DrawLineEx(Vector2{x1, y1}, Vector2{x2, y2}, 1.0f, Fade(ToRay(p.ink), 0.13f));
  }

  // Deep field stars, frustum-culled to the chart viewport.
  const float starPx = std::clamp(sx, 1.0f, 2.0f);
  for (const NavDot& dot : chart.clusters) {
    const float px = box.x + static_cast<float>(dot.x) * sx;
    const float py = box.y + static_cast<float>(dot.y) * sy;
    if (px < clipMinX || px > clipMaxX || py < clipMinY || py > clipMaxY) { continue; }
    if (dot.cls.find("bright") != std::string::npos) {
      DrawCircleV(Vector2{px, py}, starPx + 1.7f, Fade(ToRay(p.amber), 0.22f));
      DrawCircleV(Vector2{px, py}, std::max(1.0f, starPx * 0.8f), ToRay(p.coffee));
    } else {
      const int size = static_cast<int>(std::ceil(starPx * 0.75f));
      DrawRectangle(static_cast<int>(px), static_cast<int>(py), size, size, ToRay(p.muted));
    }
  }
  for (const NavDot& dot : chart.routes) {
    const float px = box.x + static_cast<float>(dot.x) * sx;
    const float py = box.y + static_cast<float>(dot.y) * sy;
    if (px < clipMinX || px > clipMaxX || py < clipMinY || py > clipMaxY) { continue; }
    Color ink = Fade(ToRay(p.coffee), 0.8f);
    int size = 2;
    if (dot.cls.find("faint") != std::string::npos) { ink = Fade(ToRay(p.coffeeLight), 0.55f); }
    if (dot.cls.find("sel") != std::string::npos) { ink = ToRay(p.sage); }
    // A trade trail is the one route the chart is asked about, so it is drawn in
    // the game's own green (#7ec63f) and a pixel larger than the rest.
    if (dot.cls.find("trade") != std::string::npos) {
      ink = ::Color{126, 198, 63, 255};
      size = 3;
    }
    DrawRectangle(static_cast<int>(px), static_cast<int>(py), size, size, ink);
  }

  // Sector territory contours & region map on Layer 2 ("sector").
  struct SectorTerritory {
    std::string id;
    std::string name;
    float cx = 0.0f;
    float cy = 0.0f;
    float rx = 0.0f;
    float ry = 0.0f;
    ::Color tint{};
    bool active = false;
  };
  std::vector<SectorTerritory> sectorTerritories;
  std::unordered_map<std::string, ::Color> systemSectorTint;
  if (chart.layer == "sector") {
    static constexpr ::Color kSectorPalette[6] = {
        ::Color{146, 98, 42, 255},   // ochre-amber
        ::Color{62, 108, 142, 255},  // slate-cobalt
        ::Color{74, 122, 84, 255},   // sage-jade
        ::Color{154, 78, 68, 255},   // terracotta-rose
        ::Color{110, 84, 138, 255},  // plum-indigo
        ::Color{56, 124, 118, 255},  // deep-teal
    };
    struct BoundsAcc {
      float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
      int count = 0;
    };
    std::unordered_map<std::string, BoundsAcc> boundsByRegion;
    boundsByRegion.reserve(sim.World().regions.size() * 2);
    for (const NavBodyView& body : chart.bodies) {
      if (body.region.empty()) { continue; }
      const float bx = box.x + static_cast<float>(body.at.x) * sx;
      const float by = box.y + static_cast<float>(body.at.y) * sy;
      BoundsAcc& acc = boundsByRegion[body.region];
      acc.minX = std::min(acc.minX, bx);
      acc.maxX = std::max(acc.maxX, bx);
      acc.minY = std::min(acc.minY, by);
      acc.maxY = std::max(acc.maxY, by);
      ++acc.count;
    }
    const Body* curSys = sim.CurrentSystem();
    const std::string shipRegion = curSys == nullptr ? std::string() : curSys->region;
    sectorTerritories.reserve(sim.World().regions.size());
    for (std::size_t ri = 0; ri < sim.World().regions.size(); ++ri) {
      const Body& reg = sim.World().regions[ri];
      auto it = boundsByRegion.find(reg.id);
      if (it == boundsByRegion.end() || it->second.count == 0) { continue; }
      const ::Color tint = kSectorPalette[ri % 6];
      const BoundsAcc& acc = it->second;
      const float pad = std::clamp(18.0f * static_cast<float>(sim.Map().zoom * 0.55), 14.0f, 44.0f);
      SectorTerritory terr;
      terr.id = reg.id;
      terr.name = reg.name;
      terr.cx = 0.5f * (acc.minX + acc.maxX);
      terr.cy = 0.5f * (acc.minY + acc.maxY);
      terr.rx = std::max(26.0f, 0.5f * (acc.maxX - acc.minX) + pad);
      terr.ry = std::max(20.0f, 0.5f * (acc.maxY - acc.minY) + pad * 0.85f);
      terr.tint = tint;
      terr.active = (reg.id == sim.Map().sector || reg.id == shipRegion);
      if (terr.cx + terr.rx >= box.x && terr.cx - terr.rx <= box.Right() &&
          terr.cy + terr.ry >= box.y && terr.cy - terr.ry <= box.Bottom()) {
        DrawEllipse(static_cast<int>(terr.cx), static_cast<int>(terr.cy),
                    terr.rx, terr.ry, Fade(tint, terr.active ? 0.12f : 0.06f));
        DrawEllipseLines(static_cast<int>(terr.cx), static_cast<int>(terr.cy),
                         terr.rx, terr.ry, Fade(tint, terr.active ? 0.56f : 0.28f));
      }
      sectorTerritories.push_back(std::move(terr));
      for (std::size_t sysIdx : sim.World().SystemsInRegion(reg.id)) {
        if (sysIdx < sim.World().systems.size()) {
          systemSectorTint.emplace(sim.World().systems[sysIdx].id, tint);
        }
      }
    }
  }

  // Lane web on sector and galaxy layers with hash lookup and frustum culling.
  if (chart.layer == "sector" || chart.layer == "galaxy") {
    std::unordered_map<std::string, NavPoint> screenAt;
    screenAt.reserve(chart.bodies.size() * 2);
    for (const NavBodyView& body : chart.bodies) { screenAt.emplace(body.id, body.at); }
    const std::vector<Lane>& lanes =
        chart.layer == "sector" ? sim.World().sectorLanes : sim.World().galaxyLanes;
    const std::string homeId = sim.HomeBody() == nullptr ? std::string() : sim.HomeBody()->id;
    const std::string targetId = sim.State().navTarget.value_or(std::string());
    for (const Lane& lane : lanes) {
      const auto a = screenAt.find(lane.from);
      const auto b = screenAt.find(lane.to);
      if (a == screenAt.end() || b == screenAt.end()) { continue; }
      const Vector2 from{box.x + static_cast<float>(a->second.x) * sx,
                         box.y + static_cast<float>(a->second.y) * sy};
      const Vector2 to{box.x + static_cast<float>(b->second.x) * sx,
                       box.y + static_cast<float>(b->second.y) * sy};
      if ((from.x < clipMinX && to.x < clipMinX) || (from.x > clipMaxX && to.x > clipMaxX) ||
          (from.y < clipMinY && to.y < clipMinY) || (from.y > clipMaxY && to.y > clipMaxY)) {
        continue;
      }
      const bool lit = lane.from == homeId || lane.to == homeId || lane.from == targetId ||
                       lane.to == targetId;
      ::Color laneColor = lit ? Fade(ToRay(p.coffee), 0.68f) : Fade(ToRay(p.coffee), 0.26f);
      float laneWidth = lit ? 1.15f : 0.45f;
      if (chart.layer == "sector" && !lit) {
        const auto ta = systemSectorTint.find(lane.from);
        const auto tb = systemSectorTint.find(lane.to);
        if (ta != systemSectorTint.end() && tb != systemSectorTint.end()) {
          const bool sameSector =
              ta->second.r == tb->second.r && ta->second.g == tb->second.g && ta->second.b == tb->second.b;
          laneColor = sameSector ? Fade(ta->second, 0.36f) : Fade(ToRay(p.amber), 0.34f);
          laneWidth = sameSector ? 0.65f : 0.85f;
        }
      }
      DrawLineEx(from, to, laneWidth, laneColor);
    }
  }

  const ::Color kPlayerBeaconRed{220, 38, 38, 255};
  if (chart.hasMarker) {
    const float px = box.x + static_cast<float>(chart.marker.x) * sx;
    const float py = box.y + static_cast<float>(chart.marker.y) * sy;
    if (px >= clipMinX && px <= clipMaxX && py >= clipMinY && py <= clipMaxY) {
      const float markerR = std::max(10.0f, 13.5f * bodyScale);
      DrawCircleV(Vector2{px, py}, markerR + 4.5f, Fade(kPlayerBeaconRed, 0.18f));
    }
  }

  // --- visible bodies only ------------------------------------------------
  const float bodyPad = 30.0f;
  std::vector<const NavBodyView*> ordered;
  ordered.reserve(chart.bodies.size());
  for (const NavBodyView& body : chart.bodies) {
    const float cx = box.x + static_cast<float>(body.at.x) * sx;
    const float cy = box.y + static_cast<float>(body.at.y) * sy;
    if (cx < box.x - bodyPad || cx > box.Right() + bodyPad ||
        cy < box.y - bodyPad || cy > box.Bottom() + bodyPad) {
      continue;
    }
    ordered.push_back(&body);
  }
  // Paint back to front: dust and regions, then the stars that light the scene,
  // then the worlds, then the marks people built.
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const NavBodyView* a, const NavBodyView* b) {
                     return ChartPaintRank(ChartLookOf(*a)) < ChartPaintRank(ChartLookOf(*b));
                   });

  const bool compactGalaxyStars = (chart.layer == "galaxy" && bodyScale < 0.55f);
  for (const NavBodyView* body : ordered) {
    const float cx = box.x + static_cast<float>(body->at.x) * sx;
    const float cy = box.y + static_cast<float>(body->at.y) * sy;
    const float radius = static_cast<float>(body->size) * 0.42f * bodyScale;
    float alpha = body->inRange || body->here ? 1.0f : 0.78f;
    if (chart.layer == "sector" && !body->region.empty() && !sim.Map().sector.empty() &&
        body->region != sim.Map().sector) {
      alpha = std::min(alpha, 0.86f);
    }

    if (compactGalaxyStars && body->type == "system" && !body->selected && !body->here) {
      // Compact star LOD on zoomed-out galaxy view avoids thousands of tiny
      // sub-pixel corona rings while keeping every star visible and clickable.
      DrawCircleV(Vector2{cx, cy}, 1.7f, Fade(ToRay(p.amber), 0.72f * alpha));
    } else {
      PaintChartBody(*body, cx, cy, radius, bodyScale, p, alpha);
    }
  }

  // Hit-test front to back. The click loop walks navHits_ backwards and takes
  // the first box containing the point, so the winners must sit at the END:
  // register the lowest rank first. See ChartHitRank() for why a region has to
  // outrank the stars drawn inside it.
  std::vector<const NavBodyView*> hitOrder = ordered;
  std::stable_sort(hitOrder.begin(), hitOrder.end(),
                   [](const NavBodyView* a, const NavBodyView* b) {
                     return ChartHitRank(*a) < ChartHitRank(*b);
                   });
  for (const NavBodyView* body : hitOrder) {
    const float cx = box.x + static_cast<float>(body->at.x) * sx;
    const float cy = box.y + static_cast<float>(body->at.y) * sy;
    if (cx < box.x || cx > box.Right() || cy < box.y || cy > box.Bottom()) { continue; }
    const float radius = static_cast<float>(body->size) * 0.42f * bodyScale;
    const float reach = std::max(radius, ChartMinimumRadius(ChartLookOf(*body), bodyScale)) + 4.0f;
    navHits_.push_back(NavHit{Area{cx - reach, cy - reach, reach * 2.0f, reach * 2.0f},
                              "select", body->id});
  }

  for (const NavBodyView* body : ordered) {
    if (!body->selected) { continue; }
    const float cx = box.x + static_cast<float>(body->at.x) * sx;
    const float cy = box.y + static_cast<float>(body->at.y) * sy;
    const float radius = static_cast<float>(body->size) * 0.42f * bodyScale;
    const float drawn = std::max(radius, ChartMinimumRadius(ChartLookOf(*body), bodyScale));
    DrawCircleLinesV(Vector2{cx, cy}, drawn + 3.5f, ToRay(p.terracotta));
    DrawCircleLinesV(Vector2{cx, cy}, drawn + 5.5f, Fade(ToRay(p.terracotta), 0.45f));
  }

  // Top-pass vivid red player position reticle so dense star clusters never hide the ship.
  if (chart.hasMarker) {
    const float px = box.x + static_cast<float>(chart.marker.x) * sx;
    const float py = box.y + static_cast<float>(chart.marker.y) * sy;
    if (px >= clipMinX && px <= clipMaxX && py >= clipMinY && py <= clipMaxY) {
      const float markerR = std::max(10.0f, 13.5f * bodyScale);
      DrawCircleLinesV(Vector2{px, py}, markerR, kPlayerBeaconRed);
      DrawCircleLinesV(Vector2{px, py}, markerR + 1.6f, Fade(kPlayerBeaconRed, 0.82f));
      const float tickIn = markerR + 1.5f;
      const float tickOut = markerR + 5.5f;
      DrawLineEx(Vector2{px - tickOut, py}, Vector2{px - tickIn, py}, 1.8f, kPlayerBeaconRed);
      DrawLineEx(Vector2{px + tickIn, py}, Vector2{px + tickOut, py}, 1.8f, kPlayerBeaconRed);
      DrawLineEx(Vector2{px, py - tickOut}, Vector2{px, py - tickIn}, 1.8f, kPlayerBeaconRed);
      DrawLineEx(Vector2{px, py + tickIn}, Vector2{px, py + tickOut}, 1.8f, kPlayerBeaconRed);
      if (chart.layer != "system" || sim.FindOngoing("travel") != nullptr) {
        DrawCircleV(Vector2{px, py}, 2.6f, kPlayerBeaconRed);
      }
    }
  }

  // --- labels (sector headers first on Layer 2, then visible body labels) --
  std::vector<Area> takenLabels;
  if (chart.layer == "sector") {
    const int hdrSize = TypePx(9);
    const float hdrH = theme_.LineHeight() + 2.0f;
    for (const SectorTerritory& terr : sectorTerritories) {
      const float textW = TextWidth(terr.name.c_str(), hdrSize);
      const float pillW = textW + 10.0f;
      const float pillX = std::clamp(terr.cx - pillW * 0.5f, box.x + 4.0f, box.Right() - pillW - 36.0f);
      const float pillY = std::clamp(terr.cy - terr.ry + 4.0f, box.y + 4.0f, box.Bottom() - hdrH - 4.0f);
      if (terr.cx + terr.rx < box.x + 20.0f || terr.cx - terr.rx > box.Right() - 20.0f ||
          terr.cy + terr.ry < box.y + 20.0f || terr.cy - terr.ry > box.Bottom() - 20.0f) {
        continue;
      }
      const Area pill{pillX, pillY, pillW, hdrH};
      bool overlaps = false;
      for (const Area& taken : takenLabels) {
        if (ChartRectsOverlap(pill, taken)) { overlaps = true; break; }
      }
      if (overlaps) { continue; }
      DrawRectangle(static_cast<int>(pill.x), static_cast<int>(pill.y),
                    static_cast<int>(pill.w), static_cast<int>(pill.h),
                    Fade(ToRay(p.panel), terr.active ? 0.90f : 0.78f));
      DrawRectangleLines(static_cast<int>(pill.x), static_cast<int>(pill.y),
                         static_cast<int>(pill.w), static_cast<int>(pill.h),
                         Fade(terr.tint, terr.active ? 0.85f : 0.52f));
      Text(terr.name.c_str(), static_cast<int>(pill.x + 5.0f), static_cast<int>(pill.y + 1.0f),
           hdrSize, terr.active ? ToRay(p.ink) : terr.tint);
      takenLabels.push_back(pill);
    }
  }

  std::vector<const NavBodyView*> labelled;
  labelled.reserve(ordered.size());
  for (const NavBodyView* body : ordered) {
    if (chart.layer == "galaxy" && body->type == "system") { continue; }
    labelled.push_back(body);
  }
  const NavPoint labelAnchor = chart.hasMarker ? chart.marker : NavPoint{0.0, 0.0};
  const std::string browsed = sim.Map().sector;
  std::stable_sort(labelled.begin(), labelled.end(),
                   [&labelAnchor, &browsed](const NavBodyView* a, const NavBodyView* b) {
                     const int pa = ChartLabelPriority(*a);
                     const int pb = ChartLabelPriority(*b);
                     if (pa != pb) { return pa < pb; }
                     const bool ain = !browsed.empty() && a->region == browsed;
                     const bool bin = !browsed.empty() && b->region == browsed;
                     if (ain != bin) { return ain; }
                     const double da = (a->at.x - labelAnchor.x) * (a->at.x - labelAnchor.x) +
                                       (a->at.y - labelAnchor.y) * (a->at.y - labelAnchor.y);
                     const double db = (b->at.x - labelAnchor.x) * (b->at.x - labelAnchor.x) +
                                       (b->at.y - labelAnchor.y) * (b->at.y - labelAnchor.y);
                     if (da != db) { return da < db; }
                     return a->id < b->id;
                   });
  const std::size_t sectorZoomBudget = static_cast<std::size_t>(
      std::clamp(26.0 + (sim.Map().zoom - 1.0) * 8.0, 22.0, 96.0));
  const std::size_t labelBudget =
      chart.layer == "sector" ? sectorZoomBudget : (chart.layer == "galaxy" ? 24u : 48u);
  std::size_t labelsPainted = 0;
  takenLabels.reserve(takenLabels.size() + labelBudget);
  for (const NavBodyView* body : labelled) {
    if (labelsPainted >= labelBudget) { break; }
    const float labelX = box.x + static_cast<float>(body->labelX) * sx;
    const float labelY = box.y + static_cast<float>(body->labelY) * sy;
    if (labelX < box.x || labelY < box.y || labelX > box.Right() - 24.0f ||
        labelY > box.Bottom() - 16.0f) {
      continue;
    }
    float longestWord = 0.0f;
    {
      std::string word;
      for (const char c : body->name + " ") {
        if (c == ' ') {
          if (!word.empty()) {
            longestWord = std::max(longestWord, TextWidth(word.c_str(), TypePx(9)));
            word.clear();
          }
        } else {
          word.push_back(c);
        }
      }
    }
    const float labelW = std::max(92.0f * sx, longestWord + 6.0f);
    const auto labelLines =
        WrapText(body->name, labelW, TypePx(9), SelectFont(TypePx(9), false));
    const float textHeight =
        theme_.LineHeight() * static_cast<float>(labelLines.size());
    const Area rect{labelX, labelY, labelW, textHeight};
    if (rect.x < box.x || rect.y < box.y || rect.Right() > box.Right() ||
        rect.Bottom() > box.Bottom()) {
      continue;
    }
    bool clashes = false;
    for (const Area& taken : takenLabels) {
      if (ChartRectsOverlap(rect, taken)) { clashes = true; break; }
    }
    if (clashes) { continue; }
    takenLabels.push_back(rect);
    ++labelsPainted;

    float textY = labelY;
    const ::Color ink = body->selected ? ToRay(p.ink)
                                       : (body->scanned ? ToRay(p.ink) : ToRay(p.muted));
    for (const auto& line : labelLines) {
      const float textX =
          body->labelRight ? labelX + labelW - TextWidth(line.c_str(), TypePx(9)) : labelX;
      Text(line.c_str(), static_cast<int>(textX), static_cast<int>(textY), TypePx(9), ink);
      textY += theme_.LineHeight();
    }
    navHits_.push_back(NavHit{rect, "select", body->id});
  }
  if (chart.hasMarker && sim.HomeBody() != nullptr) {
    const float px = box.x + static_cast<float>(chart.marker.x) * sx;
    const float py = box.y + static_cast<float>(chart.marker.y) * sy;
    const float r = std::max(10.0f, 13.5f * bodyScale);
    navHits_.push_back(NavHit{Area{std::max(box.x, px - r), std::max(box.y, py - r),
        std::max(0.0f, std::min(box.Right(), px + r) - std::max(box.x, px - r)),
        std::max(0.0f, std::min(box.Bottom(), py + r) - std::max(box.y, py - r))}, "select", sim.HomeBody()->id});
  }
  EndScissorMode();

  // Match the HTA controls: a vertical zoom pair, then vertical layer buttons
  // pinned at the chart's upper-right edge.
  const float zoomSize = std::max(21.0f, theme_.LineHeight() + 2.0f);
  const float zoomX = box.Right() - zoomSize - 7.0f;
  const Area zoomIn{zoomX, box.y + 7.0f, zoomSize, zoomSize};
  const Area zoomOut{zoomX, zoomIn.Bottom() + 4.0f, zoomSize, zoomSize};
  for (const Area& z : {zoomIn, zoomOut}) {
    DrawRectangle(static_cast<int>(z.x), static_cast<int>(z.y),
                  static_cast<int>(z.w), static_cast<int>(z.h), ToRay(p.panel2));
    DrawRectangleLines(static_cast<int>(z.x), static_cast<int>(z.y),
                       static_cast<int>(z.w), static_cast<int>(z.h),
                       ToRay(p.borderDark));
  }
  const float plusW = TextWidth("+", TypePx(11));
  const float minusW = TextWidth("-", TypePx(11));
  Text("+", static_cast<int>(zoomIn.x + (zoomSize - plusW) * 0.5f),
       static_cast<int>(zoomIn.y + (zoomSize - TypePx(11)) * 0.5f),
       TypePx(11), ToRay(p.ink));
  Text("-", static_cast<int>(zoomOut.x + (zoomSize - minusW) * 0.5f),
       static_cast<int>(zoomOut.y + (zoomSize - TypePx(11)) * 0.5f),
       TypePx(11), ToRay(p.ink));
  navHits_.push_back(NavHit{zoomIn, "zoom-in", ""});
  navHits_.push_back(NavHit{zoomOut, "zoom-out", ""});

  float layerY = zoomOut.Bottom() + 6.0f;
  for (const std::string& key : NavLayerOrder()) {
    const bool on = key == chart.layer;
    const std::string label = NavLayerLabel(key);
    const float layerW = std::max(64.0f, TextWidth(label.c_str(), TypePx(9)) + 16.0f);
    const float layerH = std::max(19.0f, theme_.LineHeight() + 4.0f);
    const Area layerBox{box.Right() - layerW - 7.0f, layerY, layerW, layerH};
    DrawRectangle(static_cast<int>(layerBox.x), static_cast<int>(layerBox.y),
                  static_cast<int>(layerBox.w), static_cast<int>(layerBox.h),
                  ToRay(on ? p.panel3 : p.panel));
    DrawRectangleLines(static_cast<int>(layerBox.x), static_cast<int>(layerBox.y),
                       static_cast<int>(layerBox.w), static_cast<int>(layerBox.h),
                       ToRay(p.borderDark));
    Text(label.c_str(), static_cast<int>(layerBox.x + 6.0f),
         static_cast<int>(layerBox.y + (layerH - TypePx(9)) * 0.5f), TypePx(9),
         ToRay(on ? p.ink : p.muted));
    navHits_.push_back(NavHit{layerBox, "layer", key});
    layerY += layerH + 4.0f;
  }
  Text(chart.scaleText.c_str(), static_cast<int>(box.x + 8.0f),
       static_cast<int>(box.y + 7.0f), TypePx(9), ToRay(p.chartInk));

  // --- panels: identity left, sensors right, the leg underneath the chart
  float ly = layout_.content.y + headerH + 8.0f;
  for (const NavPanel& panel : screen.left) {
    const float panelH = std::max(96.0f, MeasurePanelHeight(panel, leftColumn));
    PaintPanel(panel, Area{left, ly, leftColumn, panelH});
    ly += panelH + 4.0f;
  }
  float rightBottom = chartTop;
  if (!screen.right.empty()) {
    const float panelH = std::max(190.0f,
                                  MeasurePanelHeight(screen.right[0], rightColumn));
    rightBottom = chartTop + panelH;
    PaintPanel(screen.right[0],
               Area{right - rightColumn, layout_.content.y + headerH + 8.0f,
                    rightColumn, panelH});
  }

  const float uy = box.Bottom() + 22.0f;
  for (std::size_t i = 0; i < screen.under.size(); ++i) {
    PaintPanel(screen.under[i],
               Area{box.x + static_cast<float>(i) * (halfW + 6.0f), uy, halfW,
                    underH});
  }

  float ty = std::max({ly, rightBottom, uy + underH}) + 12;
  for (const CommandRowView& row : screen.commands) {
    const float costW = TextWidth(row.displayCost.c_str(), TypePx(9)) + 12.0f;
    const Area rowBox{left + 8.0f, ty, right - left - 16.0f, theme_.LineHeight() + 8.0f};
    Text(row.label.c_str(), static_cast<int>(rowBox.x + costW),
             static_cast<int>(rowBox.y + 4.0f), TypePx(10) ,
             ToRay(row.enabled ? p.ink : p.muted));
    Text((row.displayCost + "  ").c_str(), static_cast<int>(rowBox.x),
             static_cast<int>(rowBox.y + 4.0f), TypePx(9) , ToRay(p.coffee));
    std::string action;
    if (row.label.find("Stand watch") == 0) { action = "stand-watch"; }
    if (row.label.find("Dock at ") == 0) { action = "dock"; }
    if (row.label.find("View ") == 0) { action = "view-sector"; }
    if (!action.empty()) {
      const std::string arg =
          action == "view-sector"
              ? sim.State().navTarget.value_or(std::string())
              : std::string();
      navHits_.push_back(NavHit{rowBox, action, arg});
    }
    ty += theme_.LineHeight() + 12.0f;
  }
  for (const std::string& note : navScreen_.notes) {
    for (const auto& line : WrapText(note, right - left - 16.0f, TypePx(9), SelectFont(TypePx(9), false))) {
      Text(line.c_str(), static_cast<int>(left + 8.0f), static_cast<int>(ty), TypePx(9), ToRay(p.muted));
      ty += theme_.LineHeight();
    }
    ty += 4.0f;
  }
  contentPaintBottom_ = std::max(contentPaintBottom_, ty);
  PaintActionBand("Target Actions", navScreen_.courseNote, screen.progress);

}

void ShellView::PaintCrewRecord(const Simulation& sim) {
  navHits_.clear();
  const Palette& p = theme_.palette;
  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;

  const float headerH = PaintTaskHeader(record_.head, left, layout_.content.y);

  // The record is free to open; talking is a separate, clearly priced action.
  float y = layout_.content.y + headerH + 8.0f;
  if (!record_.crewId.empty()) {
    const auto& row = record_.conversation;
    std::string label = row.label;
    if (row.coolingDown) { label += " - " + row.displayCost; }
    else if (!row.enabled) { label += " - unavailable"; }
    const int size = TypePx(10);
    const float line = theme_.LineHeight();
    const auto lines = WrapText(label, right - left - 16.0f, size, SelectFont(size, false));
    const Area box{left, y, right - left, lines.size() * line + 12.0f};
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w),
                  static_cast<int>(box.h), ToRay(row.enabled ? p.panel : p.panel3));
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w),
                       static_cast<int>(box.h), ToRay(p.borderDark));
    float textY = box.y + 6.0f;
    for (const auto& text : lines) {
      Text(text.c_str(), static_cast<int>(left + 8), static_cast<int>(textY), size,
           ToRay(row.enabled ? p.ink : p.muted));
      textY += line;
    }
    if (row.enabled) { navHits_.push_back({box, "talk", record_.crewId}); }
    y = box.Bottom() + 6.0f;
    if (!record_.said.empty()) {
      for (const auto& text : WrapText("\"" + record_.said + "\"", right - left - 16.0f,
                                       size, SelectFont(size, false))) {
        Text(text.c_str(), static_cast<int>(left + 8), static_cast<int>(y), size, ToRay(p.muted));
        y += line;
      }
      y += 6.0f;
    }
  }
  // Status across the top, then the three short shelves left to right.
  if (!record_.shelves.empty()) {
    const float panelH = std::max(96.0f,
                                  MeasurePanelHeight(record_.shelves[0], right - left));
    PaintPanel(record_.shelves[0], Area{left, y, right - left, panelH});
    y += panelH + 6.0f;
  }
  const float column = (right - left - 12.0f) / 3.0f;
  for (std::size_t i = 1; i < record_.shelves.size() && i <= 3; ++i) {
    const float panelH = std::max(180.0f,
                                  MeasurePanelHeight(record_.shelves[i], column));
    PaintPanel(record_.shelves[i],
               Area{left + static_cast<float>(i - 1) * (column + 6.0f), y, column,
                    panelH});
  }
  (void)sim;
}

void ShellView::PaintQuartersScreen(Simulation& sim) {
  navHits_.clear();
  const auto& p = theme_.palette;
  const auto& screen = quarters_.screen;
  const float left = layout_.content.x + 8;
  const float right = layout_.content.Right() - 8;
  const float column = 240;
  const float line = theme_.LineHeight();
  const float panelTop = layout_.content.y + PaintTaskHeader(screen.head, left, layout_.content.y) + 8;
  auto wrapped = [&](const std::string& value, float x, float& y, float width, Color color) {
    for (const auto& row : WrapText(value, width, TypePx(10), SelectFont(TypePx(10), false))) {
      Text(row.c_str(), static_cast<int>(x), static_cast<int>(y), TypePx(10), color);
      y += line;
    }
  };
  // Preserve the complete order sheet at readable type, but reserve the
  // Assignments band at the viewport bottom. Only the readings scroll: the
  // sole commit action must not require fullscreen to be reachable.
  const float bandWidth = right - left - 16;
  const auto countRows = [&](const std::string& text, float width) {
    return WrapText(text, width, TypePx(10), SelectFont(TypePx(10), false)).size();
  };
  const auto buttonWidth = [&](const ProgressButton& button) {
    // Leave two pixels beyond the measured text, avoiding float roundoff at
    // an exact wrap boundary. Measure footer height with this *same* width.
    return std::min(bandWidth, std::max(220.0f, TextWidth(button.text.c_str(), TypePx(10)) + 18));
  };
  float bandHeight = 12 + line + countRows(quarters_.bandNote, bandWidth) * line;
  for (const auto& button : screen.progress) {
    bandHeight += 6 + countRows(button.text, buttonWidth(button) - 16) * line + 12 + 8;
  }
  quartersBandTop_ = layout_.content.Bottom() - bandHeight;
  // Payroll and roster are separate in paintRoster(). Do not paint the full
  // roster as a table and then overlay clickable copies over its payroll rows.
  const Area hall{(left + right - 560) / 2, panelTop, 560, 300};
  const bool stacked = hall.x < left + column + 10 || hall.Right() > right - column - 10;
  float rowY = stacked ? hall.Bottom() + 10 : panelTop;
  if (!screen.left.empty()) {
    auto payroll = screen.left.front();
    if (payroll.lines.size() > 4) { payroll.lines.resize(4); }
    const float h = MeasurePanelHeight(payroll, column);
    PaintPanel(payroll, {left, rowY, column, h});
    rowY += h + 6;
  }
  for (const auto& row : quarters_.roster) {
    const float nameH = WrapText(row.name, column - 20, TypePx(10), SelectFont(TypePx(10), false)).size() * line;
    const float roleH = WrapText(row.role, column - 20, TypePx(10), SelectFont(TypePx(10), false)).size() * line;
    const float h = nameH + roleH + (row.flag.empty() ? 0 : line) + 12;
    const Area box{left, rowY, column, h};
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w), static_cast<int>(box.h), ToRay(row.selected ? p.panel3 : p.panel));
    float y = rowY + 6;
    const auto& color = row.colour;
    if (color.size() == 7 && color[0] == '#') {
      const unsigned rgb = static_cast<unsigned>(std::stoul(color.substr(1), nullptr, 16));
      DrawRectangle(static_cast<int>(left + 3), static_cast<int>(rowY + 12), 4, 4,
                    Color{static_cast<unsigned char>(rgb >> 16), static_cast<unsigned char>(rgb >> 8), static_cast<unsigned char>(rgb), 255});
    }
    wrapped(row.name, left + 10, y, column - 20, ToRay(p.ink));
    wrapped(row.role, left + 10, y, column - 20, ToRay(p.muted));
    if (!row.flag.empty()) { wrapped(row.flag, left + 10, y, column - 20, ToRay(p.terracotta)); }
    navHits_.push_back({box, "select", row.id});
    rowY += h + 3;
  }
  DrawRectangle(static_cast<int>(hall.x), static_cast<int>(hall.y),
                static_cast<int>(hall.w), static_cast<int>(hall.h),
                theme_.id == "classic" ? HexColor("#fdfbf7", ToRay(p.panel)) : ToRay(p.chrome));
  DrawRectangleLines(static_cast<int>(hall.x), static_cast<int>(hall.y),
                     static_cast<int>(hall.w), static_cast<int>(hall.h),
                     ToRay(p.borderDark));
  BeginScissorMode(static_cast<int>(hall.x) + 1, static_cast<int>(hall.y - contentScroll_) + 1,
                   static_cast<int>(hall.w) - 2, static_cast<int>(hall.h) - 2);
  for (const MachinePart& part : quarters_.hall) {
    if (PaintScenePart(part, hall)) continue;
    const int x = static_cast<int>(hall.x + part.box.x);
    const int y = static_cast<int>(hall.y + part.box.y);
    const int w = std::max(1, static_cast<int>(part.box.w));
    const int h = std::max(1, static_cast<int>(part.box.h));
    Color ink = ToRay(p.panel3);
    if (part.cls.find("cq-wall") != std::string::npos) { ink = ToRay(p.panel); }
    if (part.cls.find("cq-floor") != std::string::npos) { ink = ToRay(p.coffeeLight); }
    if (part.cls.find("cq-light") != std::string::npos) { ink = ToRay(p.white); }
    if (part.cls.find("cq-door") != std::string::npos) { ink = ToRay(p.coffeeDark); }
    if (part.cls.find("cq-lock") != std::string::npos) { ink = ToRay(p.coffee); }
    DrawRectangle(x, y, w, h, ink);
    if (!part.text.empty()) {
      Text(part.text.c_str(), x + 2, y + 1, TypePx(8) , ToRay(p.muted));
    }
  }
  for (const SpritePixel& pixel : quarters_.scene.pixels) {
    DrawRectangle(static_cast<int>(hall.x + pixel.box.x),
                  static_cast<int>(hall.y + pixel.box.y),
                  std::max(1, static_cast<int>(pixel.box.w)),
                  std::max(1, static_cast<int>(pixel.box.h)),
                  Color{static_cast<unsigned char>(std::stoi(pixel.colour.substr(1, 2), nullptr, 16)),
                        static_cast<unsigned char>(std::stoi(pixel.colour.substr(3, 2), nullptr, 16)),
                        static_cast<unsigned char>(std::stoi(pixel.colour.substr(5, 2), nullptr, 16)),
                        255});
  }
  EndScissorMode();


  // Orders retain the HTA dropdown and every note/cost. At narrow widths the
  // whole column moves below the hall instead of painting through its figure.
  const float orderX = right - column;
  float oy = stacked ? hall.Bottom() + 10 : panelTop;
  for (const auto& panel : screen.right) {
    const float h = MeasurePanelHeight(panel, column);
    PaintPanel(panel, {orderX, oy, column, h});
    oy += h + 6;
    if (panel.id != "cq-orders" || quarters_.closed) { continue; }
    // A real select, not an invisible/unwired label. Only this small overlay
    // owns ImGui input; the rest of the screen uses the measured Raylib hits.
    const float comboH = line + 12;
    const float screenY = oy - contentScroll_;
    if (screenY >= layout_.content.y && screenY + comboH < quartersBandTop_) {
      ImGui::SetNextWindowPos(ImVec2(orderX, screenY), ImGuiCond_Always);
      ImGui::SetNextWindowSize(ImVec2(column, comboH), ImGuiCond_Always);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
      ImGui::Begin("##quarters-job", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground |
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
      std::string preview;
      for (const auto& option : quarters_.jobOptions) {
        const auto split = option.find('\t');
        if (option.substr(0, split) == quarters_.selectedJob) { preview = option.substr(split + 1); }
      }
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##standing-job", preview.c_str())) {
        for (const auto& option : quarters_.jobOptions) {
          const auto split = option.find('\t');
          const auto id = option.substr(0, split);
          const auto label = option.substr(split + 1);
          if (ImGui::Selectable(label.c_str(), id == quarters_.selectedJob)) { PressQuartersAction(sim, "job", id); }
        }
        ImGui::EndCombo();
      }
      ImGui::End(); ImGui::PopStyleVar();
    }
    oy += comboH + 6;
    for (const auto& order : quarters_.orders) {
      const auto label = std::string(order.on ? "[x] " : "[ ] ") + order.label;
      const float h = (WrapText(label, column - 16, TypePx(10), SelectFont(TypePx(10), false)).size() +
          WrapText(order.note, column - 16, TypePx(10), SelectFont(TypePx(10), false)).size() +
          WrapText(order.cost, column - 16, TypePx(10), SelectFont(TypePx(10), false)).size()) * line + 12;
      const Area box{orderX, oy, column, h};
      DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w), static_cast<int>(box.h), ToRay(order.on ? p.panel3 : p.panel));
      DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.border));
      float y = oy + 6;
      wrapped(label, orderX + 8, y, column - 16, ToRay(order.locked ? p.muted : p.ink));
      wrapped(order.note, orderX + 8, y, column - 16, ToRay(p.muted));
      wrapped(order.cost, orderX + 8, y, column - 16, ToRay(p.coffee));
      if (!order.locked) { navHits_.push_back({box, "order", order.id}); }
      oy += h + 4;
    }
  }
  float uy = stacked ? std::max(rowY, oy) + 10 : hall.Bottom() + 10;
  for (const auto& panel : screen.under) {
    const float h = MeasurePanelHeight(panel, hall.w);
    PaintPanel(panel, {hall.x, uy, hall.w, h}); uy += h + 6;
  }
  if (quarters_.hasCrew) {
    float y = uy + 6;
    wrapped("The Figure", hall.x + 8, y, hall.w - 16, ToRay(p.coffee));
    const auto& row = quarters_.conversation;
    std::string label = row.label;
    if (row.coolingDown) { label += " - " + row.displayCost; }
    else if (!row.enabled) { label += " - unavailable"; }
    const auto lines = WrapText(label, hall.w - 28, TypePx(10), SelectFont(TypePx(10), false));
    const Area talk{hall.x + 8, y, hall.w - 16, lines.size() * line + 12};
    DrawRectangle(static_cast<int>(talk.x), static_cast<int>(talk.y), static_cast<int>(talk.w),
                  static_cast<int>(talk.h), ToRay(row.enabled ? p.panel : p.panel3));
    DrawRectangleLines(static_cast<int>(talk.x), static_cast<int>(talk.y), static_cast<int>(talk.w), static_cast<int>(talk.h), ToRay(p.borderDark));
    float textY = talk.y + 6;
    for (const auto& text : lines) {
      Text(text.c_str(), static_cast<int>(talk.x + 6), static_cast<int>(textY), TypePx(10),
           ToRay(row.enabled ? p.ink : p.muted));
      textY += line;
    }
    if (row.enabled) { navHits_.push_back({talk, "talk", ""}); }
    y = talk.Bottom() + 6;
    wrapped(quarters_.said.empty() ? "You have not asked them anything yet." : "\"" + quarters_.said + "\"", hall.x + 8, y, hall.w - 16, ToRay(p.muted));
    uy = y + 8;
  }
  contentPaintBottom_ = std::max(contentPaintBottom_, std::max({rowY, oy, uy}) + 10);
  // Footer coordinates are in the camera's content space, while its screen
  // position is fixed. Exclude its text from the scrolling body's extent.
  paintingContent_ = false;
  const float bandY = quartersBandTop_ + contentScroll_;
  DrawRectangle(static_cast<int>(left - 8), static_cast<int>(bandY),
                static_cast<int>(right - left + 16), static_cast<int>(bandHeight), ToRay(p.panel));
  DrawLine(static_cast<int>(left), static_cast<int>(bandY), static_cast<int>(right),
           static_cast<int>(bandY), ToRay(p.border));
  float y = bandY + 6;
  wrapped("Assignments", left + 8, y, bandWidth, ToRay(p.coffee));
  wrapped(quarters_.bandNote, left + 8, y, bandWidth, ToRay(p.muted));
  for (const auto& button : screen.progress) {
    const float width = buttonWidth(button);
    const float buttonH = countRows(button.text, width - 16) * line + 12;
    const Area box{(left + right - width) / 2, y + 6, width, buttonH};
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w), static_cast<int>(box.h), ToRay(button.enabled ? p.panel2 : p.panel3));
    PaintProgressFill(box, button.fillPercent);
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.borderDark));
    float textY = box.y + 6;
    for (const auto& text : WrapText(button.text, width - 16, TypePx(10), SelectFont(TypePx(10), false))) {
      Text(text.c_str(), static_cast<int>(box.x + (width - TextWidth(text.c_str(), TypePx(10))) / 2),
           static_cast<int>(textY), TypePx(10), ToRay(p.ink));
      textY += line;
    }
    navHits_.push_back({box, "commit", ""});
    y = box.Bottom() + 8;
  }
  paintingContent_ = true;
}

bool ShellView::PressPanelRow(Simulation& sim, ScreenKind kind, const std::string& id) {
  switch (kind) {
    case ScreenKind::System: return PressSystemAction(sim, id);
    case ScreenKind::Info: return PressInfoAction(sim, id);
    case ScreenKind::Station: {
      // The first two columns only choose; the third column commits. Choosing
      // costs nothing, so it neither spends an action nor locks a control.
      if (id.compare(0, 12, "station-cat:") == 0) {
        const std::string card = id.substr(12);
        if (stationSelection_.category != card) {
          stationSelection_.category = card;
          stationSelection_.item.clear();
        }
        return true;
      }
      if (id.compare(0, 13, "station-item:") == 0) {
        stationSelection_.item = id.substr(13);
        return true;
      }
      return PressStationAction(sim, id, "");
    }
    case ScreenKind::Cargo: return PressCargoRoom(sim, id, "");
    case ScreenKind::Science: return PressStoresAction(sim, id, "");
    case ScreenKind::Room: return PressRoomAction(sim, id, "");
    case ScreenKind::SimCore: return PressSimCoreAction(sim, id, "");
    case ScreenKind::Awards: return PressAwardsAction(sim, id, "");
    case ScreenKind::Event: return PressEventAction(sim, id, "");
    case ScreenKind::Wiki: {
      const std::size_t cut = id.find(':');
      const std::string verb = cut == std::string::npos ? id : id.substr(0, cut);
      const std::string arg = cut == std::string::npos ? std::string() : id.substr(cut + 1);
      return PressWikiAction(sim, verb, arg, &wiki_);
    }
    default: return false;
  }
}

// ============================================================================
//  The room hero: the authored vignette from ROOM_SCENES — a scope, two orbits,
//  a core, two beacons, the code and label, and an eight-bar gauge. Every
//  rectangle is read from the model; none of it is measured here.
// ============================================================================
void ShellView::PaintHero(const Vignette& vignette, const Area& box) {
  const Palette& p = theme_.palette;
  const Rgb accent = p.coffee;
  DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.panel));
  DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                     static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.border));

  const auto cx = [&box](const Area& a) {
    return box.x + a.x + a.w / 2.0f;
  };
  const auto cy = [&box](const Area& a) {
    return box.y + a.y + a.h / 2.0f;
  };
  DrawCircleLines(static_cast<int>(cx(vignette.scope)), static_cast<int>(cy(vignette.scope)),
                  vignette.scope.w / 2.0f, ToRay(p.border));
  DrawCircleLines(static_cast<int>(cx(vignette.orbitOne)),
                  static_cast<int>(cy(vignette.orbitOne)), vignette.orbitOne.w / 2.0f,
                  ToRay(p.borderDark));
  DrawCircleLines(static_cast<int>(cx(vignette.orbitTwo)),
                  static_cast<int>(cy(vignette.orbitTwo)), vignette.orbitTwo.w / 2.0f,
                  ToRay(p.borderDark));
  DrawRectangle(static_cast<int>(box.x + vignette.core.x),
                static_cast<int>(box.y + vignette.core.y),
                static_cast<int>(vignette.core.w), static_cast<int>(vignette.core.h),
                ToRay(accent));
  DrawRectangle(static_cast<int>(box.x + vignette.beaconOne.x),
                static_cast<int>(box.y + vignette.beaconOne.y),
                static_cast<int>(vignette.beaconOne.w),
                static_cast<int>(vignette.beaconOne.h), ToRay(accent));
  DrawRectangle(static_cast<int>(box.x + vignette.beaconTwo.x),
                static_cast<int>(box.y + vignette.beaconTwo.y),
                static_cast<int>(vignette.beaconTwo.w),
                static_cast<int>(vignette.beaconTwo.h), ToRay(p.muted));

  Text(vignette.code.c_str(),
           static_cast<int>(box.x + vignette.readout.x),
           static_cast<int>(box.y + vignette.readout.y), TypePx(9) , ToRay(p.coffee));
  Text(vignette.label.c_str(),
           static_cast<int>(box.x + vignette.readout.x),
           static_cast<int>(box.y + vignette.readout.y + 14.0f), TypePx(13) , ToRay(p.ink));
  Text(vignette.description.c_str(),
           static_cast<int>(box.x + vignette.readout.x),
           static_cast<int>(box.y + vignette.readout.y + 32.0f), TypePx(9) , ToRay(p.muted));
  Text(vignette.statusTop.c_str(),
           static_cast<int>(box.x + vignette.readout.x),
           static_cast<int>(box.y + box.h - 34.0f), TypePx(8) , ToRay(p.muted));
  Text(vignette.statusValue.c_str(),
           static_cast<int>(box.x + vignette.readout.x),
           static_cast<int>(box.y + box.h - 22.0f), TypePx(9) , ToRay(p.coffee));

  for (std::size_t i = 0; i < vignette.meter.size(); ++i) {
    const Area& bar = vignette.meter[i];
    DrawRectangle(static_cast<int>(box.x + bar.x), static_cast<int>(box.y + bar.y),
                  static_cast<int>(bar.w), static_cast<int>(bar.h),
                  ToRay(i < 5 ? p.coffee : p.panel3));
  }
}


// ============================================================================
//  The main menu (Section N). The card is the reference's: a 3:2 crop of the
//  1672x941 art, five free commands down the left, and one dialog beside them.
//  Nothing here can advance the clock: the menu is not a place in the voyage.
// ============================================================================
void ShellView::OpenPopup(const std::string& key) {
  if (std::find(PopupKeys().begin(), PopupKeys().end(), key) == PopupKeys().end()) { return; }
  auto& panel = floating_.Open(key);
  focusedPanel_ = panel.serial;
  popupKey_ = key;
  popupCount_ = floating_.OpenedCount();
}

void ShellView::OpenBriefing(const Simulation& sim, const std::string& kind, const std::string& id) {
  selection_ = DescribeTarget(sim, kind, id);
  auto& panel = floating_.Open("", BuildBriefPopup(sim, kind, id));
  focusedPanel_ = panel.serial;
  popupCount_ = floating_.OpenedCount();
}

void ShellView::ClosePopup() {
  if (std::none_of(floating_.Panels().begin(), floating_.Panels().end(),
                   [&](const auto& panel) { return panel.serial == focusedPanel_; }) &&
      !floating_.Panels().empty()) { focusedPanel_ = floating_.Panels().back().serial; }
  floating_.Close(focusedPanel_);
  focusedPanel_ = floating_.Panels().empty() ? 0 : floating_.Panels().back().serial;
  popupKey_ = floating_.Panels().empty() ? "" : floating_.Panels().back().key;
}

void ShellView::HideTitle() {
  titleActive_ = false;
  titleConfirm_ = false;
  titlePanel_ = TitlePanel::None;
  titleHits_.clear();
  titleMessage_.clear();
}

void ShellView::ShowTitle(bool fromVoyage) {
  titleActive_ = true;
  fromVoyage_ = fromVoyage;
  titlePanel_ = TitlePanel::None;
  titleCard_ = 0;
  titleMessage_.clear();
  titleConfirm_ = false;
  titleHits_.clear();
  popupKey_.clear();
  floating_.Clear();
  focusedPanel_ = 0;
}

void ShellView::PaintSettingsControls(Simulation& sim, const PopupModel& model) {
  auto text = [](const std::string& value) { ImGui::TextWrapped("%s", value.c_str()); };
  auto heading = [&](const std::string& value) { ImGui::Spacing(); text(value); };
  auto action = [&](const std::string& cmd, const std::string& arg = "") {
    std::string message;
    PressPopupAction(sim, "settings", cmd, arg, saveDir_, &message);
  };
  auto button = [&](const std::string& label, const std::string& id) {
    ImGui::PushID(id.c_str());
    const float width = ImGui::GetContentRegionAvail().x;
    const auto size = ImGui::CalcTextSize(label.c_str(), nullptr, false, width - 16);
    const auto at = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("button", ImVec2(width, size.y + 12));
    const auto& p = theme_.palette;
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + width, at.y + size.y + 12), ToIm(ImGui::IsItemHovered() ? p.panel3 : p.panel2));
    draw->AddRect(at, ImVec2(at.x + width, at.y + size.y + 12), ToIm(p.borderDark));
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + 8, at.y + 6), ToIm(p.ink), label.c_str(), nullptr, width - 16);
    ImGui::PopID(); return clicked;
  };
      if (!model.themes.empty()) {
        heading("Colour palette"); text("Change the bridge and instrument colours.");
        const auto it = std::find(model.themes.begin(), model.themes.end(), model.theme);
        const int selected = it == model.themes.end() ? 0 : static_cast<int>(it - model.themes.begin());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##palette", model.themeLabels[selected].c_str())) {
          for (std::size_t i = 0; i < model.themes.size(); ++i) {
            if (ImGui::Selectable(model.themeLabels[i].c_str(), static_cast<int>(i) == selected)) { action("theme", model.themes[i]); }
          }
          ImGui::EndCombo();
        }
      }
      if (!audioStatus_.empty()) { heading("Soundtrack"); text(audioStatus_); }
      heading("Display adjustment");
      text("Higher gamma darkens midtones; brightness adjusts the white level. Default is 1.00.");
      if (!displayAdjustmentAvailable_) { text("Display adjustment is unavailable; using the unadjusted display."); }
      ImGui::BeginDisabled(!displayAdjustmentAvailable_);
      float gamma = model.displayGamma;
      float brightness = model.displayBrightness;
      ImGui::SetNextItemWidth(-1);
      if (ImGui::SliderFloat("##display-gamma", &gamma, static_cast<float>(kDisplayGammaMin),
                             static_cast<float>(kDisplayGammaMax), "Gamma: %.2f")) {
        action("display-gamma", std::to_string(gamma));
      }
      ImGui::SetNextItemWidth(-1);
      if (ImGui::SliderFloat("##display-brightness", &brightness, 0.75f, 1.15f, "Brightness: %.2f")) {
        action("display-brightness", std::to_string(brightness));
      }
      ImGui::EndDisabled();
      if (displayAdjustmentAvailable_ && button("Reset display adjustment", "display-reset")) {
        action("display-gamma", "1"); action("display-brightness", "1");
      }
      for (const auto& toggle : model.toggles) {
        heading(toggle.heading); text(toggle.help);
        if (button(toggle.label, toggle.id)) { action("toggle", toggle.id); }
        // HTA places volume after sound effects, before decorative motion.
        if (toggle.id == "soundEffects") {
          heading("Audio level"); text("Overall music and effects volume.");
          std::string current = model.audioLevel;
          for (const auto& choice : model.audioChoices) {
            if (choice.value == model.audioLevel) { current = choice.label; }
          }
          ImGui::SetNextItemWidth(-1);
          if (ImGui::BeginCombo("##audio", current.c_str())) {
            for (const auto& choice : model.audioChoices) {
              if (ImGui::Selectable(choice.label.c_str(), model.audioLevel == choice.value)) { action("audio", choice.value); }
            }
            ImGui::EndCombo();
          }
        }
      }
      if (!model.fontStepLabels.empty()) {
        heading(model.fontStepLine);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##reading-size", model.fontStepLabels[model.fontStep].c_str())) {
          for (std::size_t i = 0; i < model.fontStepLabels.size(); ++i) {
            if (ImGui::Selectable(model.fontStepLabels[i].c_str(), static_cast<int>(i) == model.fontStep)) { action("font", std::to_string(i)); }
          }
          ImGui::EndCombo();
        }
        if (button(model.fullscreenLabel, "fullscreen")) { action("fullscreen"); }
      }
      if (!model.dialNote.empty()) {
        heading("The chart");
        text(model.dialNote);
        // A dial reports when the hand lets go of it: every step of a drag would
        // otherwise draw a new sky, and the last one is the only one that lasts. The
        // report is a plain deactivation rather than an edit, because SetChartDials()
        // compares the four numbers itself and does nothing when they did not move.
        int galaxies = model.outerGalaxies;
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##outer-galaxies", &galaxies, 0, 64, "Outer galaxies: %d");
        if (ImGui::IsItemDeactivated()) { action("galaxies", std::to_string(galaxies)); }
        int systems = model.systemsPerRegion;
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##systems-per-region", &systems, 8, 96, "Systems per region: %d");
        if (ImGui::IsItemDeactivated()) {
          action("systems-per-region", std::to_string(systems));
        }
        int chaos = model.chaosPercent;
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##chaos", &chaos, 0, 100, "Chaos: %d%%");
        if (ImGui::IsItemDeactivated()) { action("chaos", std::to_string(chaos)); }
        int stations = model.stations;
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderInt("##stations", &stations, 0, 100, "Stations: %d%%");
        if (ImGui::IsItemDeactivated()) { action("stations", std::to_string(stations)); }
      }
}

void ShellView::PageTutorial(int direction, double nowMs) {
  if (!titleActive_ || (titlePanel_ != TitlePanel::Guide && titlePanel_ != TitlePanel::Tutorial) ||
      direction == 0) return;
  const int next = std::clamp(titleCard_ + (direction > 0 ? 1 : -1),
                              0, static_cast<int>(TutorialCards().size()) - 1);
  if (next != titleCard_) {
    titleCard_ = next;
    tutorialDirection_ = direction > 0 ? 1 : -1;
    tutorialTransitionMs_ = nowMs;
  }
}

void ShellView::PaintTutorial(const Simulation& sim, double nowMs) {
  const Palette& p = theme_.palette;
  DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_), ToRay(p.titleBackground));
  const float innerW = std::min(width_ - 80.0f, 1200.0f);
  const float left = (width_ - innerW) * 0.5f;
  const int textPx = TypePx(11);
  const float lineH = static_cast<float>(textPx) + 5.0f;
  const std::string heading = "HOW TO PLAY   " + std::to_string(title_.tutorialCard + 1) +
                              " / " + std::to_string(title_.tutorialCards);
  Text(heading.c_str(), static_cast<int>(left), 18, textPx, ToRay(p.titleInk));
  const char* keyboardHint = "LEFT / RIGHT KEYS";
  Text(keyboardHint, static_cast<int>(left + innerW - TextWidth(keyboardHint, textPx, false)),
       18, textPx, ToRay(p.muted));
  const auto lines = WrapText(title_.cardBody, innerW, textPx, SelectFont(textPx, false));
  const auto habits = WrapText(title_.cardHabit, innerW, textPx, SelectFont(textPx, false));
  const float controlsY = height_ - 58.0f;
  const float notesY = controlsY - 12.0f - (lines.size() + habits.size()) * lineH;
  const float artTop = 54.0f;
  const float artH = std::max(40.0f, notesY - artTop - 12.0f);
  const Texture2D* art = static_cast<const Texture2D*>(tutorialArt_[static_cast<std::size_t>(title_.tutorialCard)]);
  if (art != nullptr && art->id != 0 && art->width > 0 && art->height > 0) {
    const float scale = std::min(innerW / art->width, artH / art->height);
    const float w = art->width * scale, h = art->height * scale;
    const float progress = sim.State().reducedMotion || theme_.reducedMotion ? 1.0f :
        std::clamp(static_cast<float>((nowMs - tutorialTransitionMs_) / 320.0), 0.0f, 1.0f);
    const float eased = 1.0f - std::pow(1.0f - progress, 3.0f);
    const float x = (width_ - w) * 0.5f + tutorialDirection_ * (1.0f - eased) * 48.0f;
    const float y = artTop + (artH - h) * 0.5f;
    DrawTexturePro(*art, Rectangle{0, 0, static_cast<float>(art->width), static_cast<float>(art->height)},
                   Rectangle{x, y, w, h}, Vector2{0, 0}, 0, ToRay(p.white, static_cast<int>(255 * eased)));
  } else {
    // Keep the guide usable even if a packaged artwork is missing.
    Text(title_.cardTitle.c_str(), static_cast<int>(left), static_cast<int>(artTop + artH * 0.4f),
         TypePx(18), ToRay(p.titleInk));
    Text("Illustration unavailable", static_cast<int>(left), static_cast<int>(artTop + artH * 0.4f + 40),
         textPx, ToRay(p.muted));
  }
  float y = notesY;
  for (const auto& line : lines) {
    Text(line.c_str(), static_cast<int>(left), static_cast<int>(y), textPx, ToRay(p.titleInk));
    y += lineH;
  }
  for (const auto& line : habits) {
    Text(line.c_str(), static_cast<int>(left), static_cast<int>(y), textPx, ToRay(p.coffee));
    y += lineH;
  }
  const float gap = 12.0f, buttonW = std::min(210.0f, (innerW - 2 * gap) / 3.0f);
  const float startX = (width_ - (buttonW * 3 + gap * 2)) * 0.5f;
  const char* labels[] = {"PREVIOUS", "BACK TO MENU", "NEXT"};
  const char* actions[] = {"card:prev", "back", "card:next"};
  for (int i = 0; i < 3; ++i) {
    const bool enabled = i == 1 || (i == 0 ? title_.tutorialCard > 0 : title_.tutorialCard + 1 < title_.tutorialCards);
    const Area box{startX + i * (buttonW + gap), controlsY, buttonW, 40.0f};
    const bool hovered = enabled && box.Contains(mouseX_, mouseY_);
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w),
                  static_cast<int>(box.h), ToRay(hovered ? p.titleMenuHover : p.titleMenu));
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w),
                       static_cast<int>(box.h), ToRay(p.titleMenuBorder));
    const float tw = TextWidth(labels[i], textPx, false);
    Text(labels[i], static_cast<int>(box.x + (box.w - tw) * 0.5f),
         static_cast<int>(box.y + (box.h - textPx) * 0.5f), textPx,
         ToRay(enabled ? p.titleMenuInk : p.muted));
    if (enabled) titleHits_.push_back({box, actions[i], ""});
  }
}

void ShellView::PaintTitle(Simulation& sim, double nowMs) {
  const Palette& p = theme_.palette;
  titleHits_.clear();
  title_ = BuildTitleScreen(sim, fromVoyage_, titlePanel_, saveDir_, titleSlot_,
                            titleMessage_, titleCard_);
  if (!titleConfirm_ && (titlePanel_ == TitlePanel::Guide || titlePanel_ == TitlePanel::Tutorial)) {
    PaintTutorial(sim, nowMs);
    return;
  }

  DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_),
                ToRay(p.panel));

  // title.jpg is the complete menu card, not a hero image to decorate with a
  // second, invented menu. Fit the entire 1672x941 artwork without cropping.
  const float ratio = 1672.0f / 941.0f;
  float cardW = std::min(width_ - 24.0f, 1672.0f);
  float cardH = cardW / ratio;
  if (cardH > height_ - 24.0f) { cardH = height_ - 24.0f; cardW = cardH * ratio; }
  const Area card{(width_ - cardW) * 0.5f, (height_ - cardH) * 0.5f, cardW, cardH};
  DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_), ToRay(p.titleBackground));
  const Texture2D* texture = static_cast<const Texture2D*>(titleArt_);
  const bool hasTitleArt = texture != nullptr && texture->id != 0 &&
                           texture->width > 0 && texture->height > 0;
  if (hasTitleArt) {
    DrawTexturePro(*texture,
                   Rectangle{0.0f, 0.0f, static_cast<float>(texture->width),
                             static_cast<float>(texture->height)},
                   Rectangle{card.x, card.y, card.w, card.h}, Vector2{0.0f, 0.0f}, 0.0f,
                   ToRay(p.white));
  }

  // A missing optional title image must never leave a blank window. Keep the
  // title and all menu actions usable, with explicit native controls as a
  // fallback for packaged builds where an asset might be absent or unreadable.
  if (!hasTitleArt && titlePanel_ == TitlePanel::None && !titleConfirm_) {
    Text("SPACESHIP", static_cast<int>(card.x + 42.0f),
         static_cast<int>(card.y + 48.0f), TypePx(40), ToRay(p.ink));
    Text("A SIMULATION GAME OF DEPTH, PATIENCE AND EVERY DECISION MATTERS",
         static_cast<int>(card.x + 46.0f), static_cast<int>(card.y + 106.0f),
         TypePx(12), ToRay(p.muted));
    float y = card.y + 190.0f;
    for (const ActionRowView& action : title_.actions) {
      const Area box{card.x + 46.0f, y, std::min(360.0f, card.w - 92.0f), 42.0f};
      DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                    static_cast<int>(box.w), static_cast<int>(box.h),
                    ToRay(action.enabled ? p.white : p.panel3));
      DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                         static_cast<int>(box.w), static_cast<int>(box.h),
                         ToRay(action.enabled ? p.borderDark : p.border));
      Text(action.label.c_str(), static_cast<int>(box.x + 14.0f),
           static_cast<int>(box.y + 12.0f), TypePx(12),
           ToRay(action.enabled ? p.ink : p.muted));
      if (action.enabled) { titleHits_.push_back(NavHit{box, action.id, ""}); }
      y += 52.0f;
    }
    return;
  }

  // The art says DESIGN / MANAGE / ...; these are decorative, not menu
  // controls. HTA #title-actions paints its own five commands over that strip.
  const Area menu{card.x + card.w * 0.02f, card.Bottom() - card.h * (0.0405f + 0.0565f),
                  card.w * 0.818f, std::max(card.h * 0.0565f, theme_.LineHeight() + 12)};
  const Color menuBg = ToRay(p.titleMenu), menuText = ToRay(p.titleMenuInk);
  const float cell = menu.w / static_cast<float>(title_.actions.size());
  for (std::size_t i = 0; i < title_.actions.size(); ++i) {
    const Area box{menu.x + cell * i, menu.y, cell, menu.h};
    const bool hover = box.Contains(mouseX_, mouseY_);
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w + 1), static_cast<int>(box.h),
                  hover ? ToRay(p.titleMenuHover) : menuBg);
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.titleMenuBorder));
    const auto& action = title_.actions[i];
    const float tw = TextWidth(action.label.c_str(), TypePx(12), true);
    Text(action.label.c_str(), static_cast<int>(box.x + (box.w - tw) / 2),
         static_cast<int>(box.y + (box.h - TypePx(12)) / 2), TypePx(12), menuText, true);
    if (titlePanel_ == TitlePanel::None && !titleConfirm_ && action.enabled) { titleHits_.push_back({box, action.id, ""}); }
  }
  if (titlePanel_ == TitlePanel::None && !titleConfirm_) { return; }

  // HTA #title-dialog is centred, parchment, vertically scrollable. There is
  // no second title or left-hand command column on top of the supplied art.
  const float w = std::min(560.0f, width_ * 0.9f);
  ImGui::SetNextWindowPos(ImVec2(width_ / 2, height_ / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, height_ * 0.86f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ToIm(p.titleDialog));
  ImGui::PushStyleColor(ImGuiCol_Text, ToIm(p.titleInk));
  ImGui::PushStyleColor(ImGuiCol_Border, ToIm(p.titleBorder));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(26, 24));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  if (ImGui::Begin("##title-sheet", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    ImGui::TextWrapped("%s", titleConfirm_ ? "New voyage" : title_.panelTitle.c_str());
    ImGui::Spacing();
    if (titleConfirm_) {
      ImGui::TextWrapped("%s", titleMessage_.c_str());
      if (ImGui::Button("OK")) {
        PressTitleAction(sim, "new-voyage", "", &fromVoyage_, &titlePanel_, &titleSlot_, saveDir_, &titleMessage_, &titleActive_);
        HideTitle();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) { titleConfirm_ = false; titleMessage_.clear(); }
    } else if (titlePanel_ == TitlePanel::Settings) {
      ImGui::TextWrapped("%s", title_.settings.notice.c_str());
      PaintSettingsControls(sim, title_.settings);
      if (ImGui::Button("BACK")) { titlePanel_ = TitlePanel::None; }
    } else {
      ImGui::TextWrapped("%s", title_.panelNote.c_str());
      for (const auto& row : title_.panelRows) {
        ImGui::BeginDisabled(!row.enabled);
        if (ImGui::Button(row.label.c_str(), ImVec2(-1, 0))) {
          PressTitleAction(sim, row.id, "", &fromVoyage_, &titlePanel_, &titleSlot_, saveDir_, &titleMessage_, &titleActive_);
        }
        ImGui::EndDisabled();
      }
      if (!titleMessage_.empty()) { ImGui::TextWrapped("%s", titleMessage_.c_str()); }
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(3);
}

// ============================================================================
//  The loss overlay: the voyage ended, and the run's own record with it.
// ============================================================================
void ShellView::PaintLoss(Simulation& sim) {
  const Palette& p = theme_.palette;
  lossHits_.clear();
  loss_ = BuildLossScreen(sim, lossSlot_, lossMessage_);

  DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_),
                ToRay(p.panel, 226));
  const float cardW = std::min(420.0f, width_ - 60.0f);
  const Area card{(width_ - cardW) * 0.5f, 90.0f, cardW, std::min(560.0f, height_ - 120.0f)};
  DrawRectangle(static_cast<int>(card.x), static_cast<int>(card.y),
                static_cast<int>(card.w), static_cast<int>(card.h), ToRay(p.panel));
  DrawRectangleLines(static_cast<int>(card.x), static_cast<int>(card.y),
                     static_cast<int>(card.w), static_cast<int>(card.h), ToRay(p.borderDark));

  float y = card.y + 18.0f;
  Text(loss_.heading.c_str(), static_cast<int>(card.x + 20.0f),
           static_cast<int>(y), TypePx(24) , ToRay(p.terracotta));
  y += 34.0f;
  for (const std::string& line : WrapText(loss_.cause, card.w - 40.0f, TypePx(12), SelectFont(TypePx(12), false))) {
    Text(line.c_str(), static_cast<int>(card.x + 20.0f), static_cast<int>(y), TypePx(12) ,
             ToRay(p.ink));
    y += 16.0f;
  }
  y += 4.0f;
  for (const std::string& line : WrapText(loss_.note, card.w - 40.0f, TypePx(10), SelectFont(TypePx(10), false))) {
    Text(line.c_str(), static_cast<int>(card.x + 20.0f), static_cast<int>(y), TypePx(10) ,
             ToRay(p.muted));
    y += 13.0f;
  }
  y += 10.0f;
  for (const PopupRow& row : loss_.summary) {
    Text(row.key.c_str(), static_cast<int>(card.x + 20.0f), static_cast<int>(y), TypePx(10) ,
             ToRay(p.muted));
    const float valueW = static_cast<float>(TextWidth(row.value.c_str(), TypePx(10) ));
    Text(row.value.c_str(), static_cast<int>(card.Right() - 20.0f - valueW),
             static_cast<int>(y), TypePx(10) , ToRay(p.ink));
    y += 14.0f;
  }
  y += 10.0f;
  for (const ActionRowView& action : loss_.actions) {
    if (y + 24.0f > card.Bottom() - 12.0f) { break; }
    const Area box{card.x + 20.0f, y, card.w - 40.0f, 22.0f};
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                  static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.panel2));
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                       static_cast<int>(box.w), static_cast<int>(box.h),
                       ToRay(p.borderDark));
    Text(action.label.c_str(), static_cast<int>(box.x + 8.0f),
             static_cast<int>(box.y + 6.0f), TypePx(10) , ToRay(p.ink));
    lossHits_.push_back(NavHit{box, action.id, ""});
    y += 26.0f;
  }
  if (!lossMessage_.empty()) {
    Text(lossMessage_.c_str(), static_cast<int>(card.x + 20.0f),
             static_cast<int>(card.Bottom() - 18.0f), TypePx(9) , ToRay(p.coffee));
  }
}

// The handbook's search box, drawn as a real field over the index column.
void ShellView::PaintWikiSearch(const Area& viewport) {
  if (panelsKind_ != ScreenKind::Wiki) { return; }
  const Area box{layout_.content.x + 8.0f, viewport.y, 260.0f, 22.0f};
  ImGui::SetNextWindowPos(ImVec2(box.x - 4.0f, box.y - 4.0f));
  ImGui::SetNextWindowSize(ImVec2(box.w + 8.0f, 30.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
  const bool host = ImGui::Begin("##wiki-search-host", nullptr,
                                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoBackground);
  if (!host) { ImGui::End(); ImGui::PopStyleVar(); return; }
  ImGui::SetNextItemWidth(box.w);
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ToIm(theme_.palette.panel2));
  ImGui::PushStyleColor(ImGuiCol_Text, ToIm(theme_.palette.ink));
  if (ImGui::InputTextWithHint("##wiki-search", "Search the handbook", wikiQuery_,
                               sizeof(wikiQuery_))) {
    wiki_.query = wikiQuery_;
  }
  ImGui::PopStyleColor(2);
  ImGui::End();
  ImGui::PopStyleVar();
}

void ShellView::PaintPanelsScreen(Simulation& sim) {
  navHits_.clear();
  taskButtons_.clear();
  const Palette& p = theme_.palette;
  const TaskScreenModel& screen = taskScreen_;
  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;

  const float headerH = PaintTaskHeader(screen.head, left, layout_.content.y);

  const float searchTop = layout_.content.y + headerH + 8.0f;
  PaintWikiSearch(Area{layout_.content.x, searchTop, layout_.content.w, 0.0f});
  float top = searchTop + (panelsKind_ == ScreenKind::Wiki ? 38.0f : 0.0f);
  if (screen.hasHero) {
    PaintHero(screen.hero, Area{left, top, right - left, screen.hero.hero.h});
    top += screen.hero.hero.h + 8.0f;
  }
  float panelsBottom = top;
  const bool grouped = screen.panelColumns.size() == screen.panels.size() && !screen.panels.empty();
  std::size_t count = screen.panels.empty() ? 1 : screen.panels.size();
  if (grouped) {
    count = 1;
    for (int column : screen.panelColumns) {
      count = std::max(count, static_cast<std::size_t>(column + 1));
    }
  }
  const float gap = 8.0f;
  const float columnW = (right - left - gap * static_cast<float>(count - 1)) /
                        static_cast<float>(count);
  std::vector<float> columnY(count, top);
  struct RowMetrics {
    std::vector<std::string> lines;
    std::string cost;
    float badgeW = 0;
    float height = 0;
    bool inlineCost = false;
  };
  const auto measureRow = [&](const CommandRowView& row, float rowW) {
    RowMetrics m;
    m.cost = row.hideCost ? std::string()
                          : (row.displayCost.empty() ? "free" : row.displayCost);
    m.badgeW = m.cost.empty() ? 0.0f : TextWidth(m.cost.c_str(), TypePx(9)) + 12.0f;
    m.inlineCost = grouped && !row.reading && (m.cost.empty() || rowW >= m.badgeW + 112.0f);
    const float labelW = std::max(1.0f, rowW - 12.0f - (m.inlineCost ? m.badgeW + 8.0f : 0.0f));
    m.lines = WrapText(row.label, labelW, TypePx(10), SelectFont(TypePx(10), false));
    m.height = row.reading ? m.lines.size() * theme_.LineHeight() + 6.0f
        : (m.lines.size() + (m.inlineCost ? 0 : 1)) * theme_.LineHeight() + 12.0f;
    return m;
  };
  for (std::size_t i = 0; i < screen.panels.size(); ++i) {
    const std::size_t column = grouped ? static_cast<std::size_t>(screen.panelColumns[i]) : i;
    const float x = left + static_cast<float>(column) * (columnW + gap);
    const float originalTop = columnY[column];
    const NavPanel& panel = screen.panels[i];
    const float noteH = MeasurePanelHeight(panel, columnW);

    // The station's panels contain their rows, as in the HTA, and the sales /
    // board and shipyard / tavern panels stack in their shared columns.
    float rowsH = 0;
    if (i < screen.panelRows.size()) {
      for (const CommandRowView& row : screen.panelRows[i]) {
        const RowMetrics m = measureRow(row, columnW - (grouped ? 16.0f : 0.0f));
        rowsH += m.height + (row.reading ? 0.0f : 3.0f);
      }
    }
    // Keep the selected handbook article alongside the scrolling index. For
    // a tall article, let it scroll until its bottom is visible before pinning
    // it; never make the lower text unreachable. Its sticky offset must not
    // grow the document's scroll extent or its click coordinates will drift.
    const bool stickyWiki = panelsKind_ == ScreenKind::Wiki && panel.id == "wiki-article";
    const float panelHeight = noteH + 6.0f + rowsH;
    const float anchor = std::min(layout_.content.y + 8.0f,
                                  layout_.content.Bottom() - 8.0f - panelHeight);
    const float stickyShift = stickyWiki ? std::max(0.0f, contentScroll_ + anchor - originalTop) : 0.0f;
    const float panelTop = originalTop + stickyShift;
    const bool tracked = paintingContent_;
    if (stickyWiki) { paintingContent_ = false; }
    if (grouped && rowsH > 0) {
      DrawRectangle(static_cast<int>(x), static_cast<int>(panelTop), static_cast<int>(columnW),
                    static_cast<int>(noteH + rowsH + 12), ToRay(p.panel));
      DrawRectangleLines(static_cast<int>(x), static_cast<int>(panelTop), static_cast<int>(columnW),
                         static_cast<int>(noteH + rowsH + 12), ToRay(p.border));
    }
    PaintPanel(panel, Area{x, panelTop, columnW, noteH});

    float y = panelTop + noteH + 6.0f;
    if (i < screen.panelRows.size()) {
      for (std::size_t r = 0; r < screen.panelRows[i].size(); ++r) {
        const CommandRowView& row = screen.panelRows[i][r];
        const float inset = grouped ? 8.0f : 0.0f;
        const float rowW = columnW - 2 * inset;
        const RowMetrics m = measureRow(row, rowW);
        if (row.reading) {
          for (const auto& line : m.lines) {
            Text(line.c_str(), static_cast<int>(x + inset + 6.0f), static_cast<int>(y), TypePx(10), ToRay(p.muted));
            y += theme_.LineHeight();
          }
          y += 6.0f;
          continue;
        }
        const float rowH = m.height;
        const Area box{x + inset, y, rowW, rowH};
        if (row.plain) {
          // A list line, not a button: nothing behind it except the mark of the
          // open card or the picked item, so the walk stays visible.
          if (row.selected) {
            DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                          static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.panel3));
            DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y), 3,
                          static_cast<int>(box.h), ToRay(p.coffee));
          }
          float lineTop = box.y + 6.0f;
          for (const auto& line : m.lines) {
            Text(line.c_str(), static_cast<int>(box.x + 8.0f), static_cast<int>(lineTop),
                 TypePx(10), ToRay(row.live ? p.ink : p.muted));
            lineTop += theme_.LineHeight();
          }
          if (row.enabled && i < screen.panelRowIds.size() && r < screen.panelRowIds[i].size()) {
            navHits_.push_back(NavHit{box, "row", screen.panelRowIds[i][r]});
          }
          y += rowH + 3.0f;
          continue;
        }
        DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                      static_cast<int>(box.w), static_cast<int>(box.h),
                      ToRay(row.selected ? p.panel3
                                         : (row.enabled ? (grouped ? p.control : p.panel2)
                                                        : p.panel3)));
        DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                           static_cast<int>(box.w), static_cast<int>(box.h),
                           ToRay(row.selected ? p.coffeeLight
                                              : (row.enabled ? p.borderDark : p.border)));
        if (row.selected) {
          // The open card and the picked item are marked with a coffee bar down
          // their left edge, so the walk card -> item -> inspector is visible.
          DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y), 3,
                        static_cast<int>(box.h), ToRay(p.coffee));
        }
        float textTop = box.y + 6.0f;
        for (const auto& line : m.lines) {
          Text(line.c_str(), static_cast<int>(box.x + 6.0f), static_cast<int>(textTop), TypePx(10), ToRay(row.enabled ? p.ink : p.muted));
          textTop += theme_.LineHeight();
        }
        if (m.inlineCost) {
          if (!m.cost.empty()) {
            // A row that asked for no badge gets none: the button is just a button.
            const float badgeX = box.Right() - m.badgeW - 6.0f;
            DrawRectangle(static_cast<int>(badgeX), static_cast<int>(box.y + 4.0f),
                          static_cast<int>(m.badgeW), static_cast<int>(theme_.LineHeight()), ToRay(p.panel2));
            DrawRectangleLines(static_cast<int>(badgeX), static_cast<int>(box.y + 4.0f),
                               static_cast<int>(m.badgeW), static_cast<int>(theme_.LineHeight()), ToRay(p.border));
            Text(m.cost.c_str(), static_cast<int>(badgeX + 6.0f), static_cast<int>(box.y + 6.0f),
                 TypePx(9), ToRay(row.enabled ? p.coffee : p.muted));
          }
        } else if (!m.cost.empty()) {
          Text(m.cost.c_str(), static_cast<int>(box.x + 6.0f), static_cast<int>(textTop), TypePx(9), ToRay(row.enabled ? p.coffee : p.muted));
        }
        if (row.enabled && i < screen.panelRowIds.size() && r < screen.panelRowIds[i].size()) {
          navHits_.push_back(NavHit{box, "row", screen.panelRowIds[i][r]});
        }
        y += rowH + 3.0f;
      }
    }
    if (grouped && rowsH > 0) { y += 6.0f; }
    columnY[column] = y - stickyShift + gap;
    panelsBottom = std::max(panelsBottom, y - stickyShift);
    paintingContent_ = tracked;
  }

  contentPaintBottom_ = std::max(contentPaintBottom_, panelsBottom);
  auto buttons = screen.progress;
  std::string note = screen.bandNote;
  if (!pendingConfirm_.empty()) {
    ProgressButton yes; yes.taskId = "confirm-yes"; yes.text = "Yes, do it";
    ProgressButton no; no.taskId = "confirm-no"; no.text = "Keep as it is";
    buttons.push_back(yes); buttons.push_back(no);
  }
  if (!buttons.empty() || !note.empty()) PaintActionBand("Work in progress", note, buttons, pendingConfirm_);
  (void)sim;
}

// ============================================================================
//  The floating popups (Section K). They never replace the window: a veil and
//  a card float over whatever is behind, and every control inside is free.
//  Successive popups step down and right by (count % 5) * 22, as in the
//  reference, so the reader can see one behind another.
// ============================================================================
void ShellView::PaintPopup(Simulation& sim) {
  const auto& p = theme_.palette;
  // One standard movable chrome for every HTA showPopup window. Fixed width,
  // growing body up to viewport height, then native scrolling; no text cutoffs.
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ToIm(p.panel));
  ImGui::PushStyleColor(ImGuiCol_Text, ToIm(p.ink));
  ImGui::PushStyleColor(ImGuiCol_Border, ToIm(p.borderDark));
  ImGui::PushStyleColor(ImGuiCol_TitleBg, ToIm(p.panel2));
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ToIm(p.panel2));
  ImGui::PushStyleColor(ImGuiCol_Button, ToIm(p.panel2));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToIm(p.panel3));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToIm(p.coffeeLight));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ToIm(p.panel2));
  ImGui::PushStyleColor(ImGuiCol_Header, ToIm(p.panel3));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(13, 11));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  std::vector<int> closed;
  bool showMenu = false;
  bool restarted = false;
  for (auto& panel : floating_.Panels()) {
    // Briefings and manifest are opening-time snapshots. The log, settings
    // and file controls refresh in place, as their HTA handlers require.
    if (panel.key == "manifest" && panel.brief.title.empty()) {
      panel.brief = BuildPopup(sim, panel.key, saveDir_);
    }
    const auto model = panel.key.empty() || panel.key == "manifest"
        ? panel.brief : BuildPopup(sim, panel.key, saveDir_);
    const std::string name = model.title + "###ship-popup-" + std::to_string(panel.serial);
    const float w = std::min(520.0f, std::max(220.0f, width_ - 32.0f));
    const float maxH = std::max(100.0f, height_ - 48.0f);
    ImGui::SetNextWindowPos(ImVec2(std::max(0.0f, std::min(panel.x, width_ - w - 12.0f)),
                                std::max(0.0f, std::min(panel.y, height_ - 100.0f))), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, maxH));
    bool open = true;
    const bool visible = ImGui::Begin(name.c_str(), &open,
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
      focusedPanel_ = panel.serial;
    }
    if (visible) {
      ImGui::BeginDisabled(floating_.HasConfirmation() && panel.pendingAction.empty());
      auto text = [](const std::string& value) { ImGui::TextWrapped("%s", value.c_str()); };
      auto heading = [&](const std::string& value) { ImGui::Spacing(); text(value); };
      auto action = [&](const std::string& command, const std::string& arg = "") {
        PressPopupAction(sim, panel.key, command, arg, saveDir_, &panel.message);
      };
      // Wrap labels as well as paragraphs. InvisibleButton creates a complete,
      // measured hitbox for each multiline command instead of clipping Button.
      auto button = [&](const std::string& label, const std::string& id) {
        ImGui::PushID(id.c_str());
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const auto size = ImGui::CalcTextSize(label.c_str(), nullptr, false, width - 12.0f);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton("control", ImVec2(width, size.y + 12.0f));
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(at, ImVec2(at.x + width, at.y + size.y + 12.0f),
            ToIm(ImGui::IsItemHovered() ? p.panel3 : p.panel2));
        draw->AddRect(at, ImVec2(at.x + width, at.y + size.y + 12.0f), ToIm(p.border));
        draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(at.x + 6, at.y + 6),
                      ToIm(p.ink), label.c_str(), nullptr, width - 12.0f);
        ImGui::PopID();
        return pressed;
      };
      if (!model.notice.empty()) { text(model.notice); ImGui::Spacing(); }
      for (const auto& section : model.sections) {
        if (!section.title.empty()) { heading(section.title); ImGui::Separator(); }
        for (const auto& row : section.rows) {
          text(row.key);
          ImGui::Indent(12); text(row.value); ImGui::Unindent(12);
          ImGui::Spacing();
        }
      }
      for (const auto& line : model.lines) { text(line); ImGui::Spacing(); }
      if (!model.acts.empty()) {
        heading("Actions in this window"); ImGui::Separator();
        for (const auto& act : model.acts) {
          text(act.cost + "  " + act.label + (act.live ? " - wired" : ""));
        }
      }
      for (const auto& line : model.footer) { ImGui::Spacing(); text(line); }
      if (panel.key == "settings") { PaintSettingsControls(sim, model); }
      if (!model.slots.empty()) {
        ImGui::BeginDisabled(!panel.pendingAction.empty());
        heading("Save slot");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##slot", model.slots[panel.slot - 1].label.c_str())) {
          for (const auto& slot : model.slots) {
            if (ImGui::Selectable(slot.label.c_str(), slot.slot == panel.slot)) { panel.slot = slot.slot; }
          }
          ImGui::EndCombo();
        }
        const struct { const char* command; const char* label; } commands[] = {
            {"save", "Save voyage"}, {"load", "Load voyage"},
            {"new-voyage", "Start a new voyage"}, {"main-menu", "Return to main menu"}};
        for (const auto& command : commands) {
          if (!button(command.label, command.command)) { continue; }
          const std::string cmd = command.command;
          if (cmd == "main-menu") { showMenu = true; }
          else if (cmd == "new-voyage" || sim.State().options.confirmRisk) {
            panel.pendingAction = cmd;
            panel.pendingSlot = panel.slot;
            panel.question = cmd == "new-voyage"
                ? "Start a new voyage? Unsaved progress will be lost; saved slots are kept."
                : (cmd == "save" ? "Overwrite save slot " : "Replace the current voyage with slot ") + std::to_string(panel.slot) + "?";
          } else { action(cmd, std::to_string(panel.slot)); }
        }
        ImGui::EndDisabled();
        if (!panel.pendingAction.empty()) {
          ImGui::Separator(); text(panel.question);
          if (button("OK", "confirm")) {
            const auto cmd = panel.pendingAction;
            panel.pendingAction.clear(); panel.question.clear();
            action(cmd, std::to_string(panel.pendingSlot));
            if (cmd == "new-voyage") { restarted = true; }
          }
          if (button("Cancel", "cancel")) { panel.pendingAction.clear(); panel.question.clear(); }
        }
      }
      if (!panel.message.empty()) { ImGui::Spacing(); text(panel.message); }
      ImGui::EndDisabled();
    }
    ImGui::End();
    if (!open) { closed.push_back(panel.serial); }
    if (restarted || showMenu) { break; }
  }
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(10);
  for (int serial : closed) { floating_.Close(serial); }
  if (restarted) { floating_.Clear(); ResetContentScroll(); }
  if (showMenu) { ShowTitle(true); }
  popupKey_ = floating_.Panels().empty() ? "" : floating_.Panels().back().key;
}

void ShellView::PaintDiagram(const DiagramModel& diagram, const Area& stageBox, float scale) {
  (void)stageBox;
  (void)scale;
  const ::Color frame = ToRay(theme_.palette.coffee);
  const ::Color hull = ToRay(theme_.palette.white);
  const ::Color spine = ToRay(theme_.palette.panel);
  const float ox = diagram.stage.x;
  const float oy = diagram.stage.y;

  for (const HullPart& part : diagram.hull) {
    const int x = static_cast<int>(ox + part.box.x);
    const int y = static_cast<int>(oy + part.box.y);
    const int w = static_cast<int>(part.box.w);
    const int h = static_cast<int>(part.box.h);
    const float radius = HullRadius(part.cls);
    if (part.cls == "hp-lens") {
      DrawCircle(static_cast<int>(x + w / 2.0f), static_cast<int>(y + h / 2.0f),
                 static_cast<float>(w) / 2.0f, hull);
      DrawRing(Vector2{x + w / 2.0f, y + h / 2.0f}, static_cast<float>(w) / 2.0f - 2.0f,
               static_cast<float>(w) / 2.0f, 0.0f, 360.0f, 64, frame);
    } else if (radius > 0.0f) {
      DrawRectangleRounded(Rectangle{static_cast<float>(x), static_cast<float>(y),
                                     static_cast<float>(w), static_cast<float>(h)},
                           radius / static_cast<float>(std::max(w, h)),
                           part.cls == "hp-nacelle" ? 6 : 12,
                           part.cls == "hp-spine" ? spine : hull);
      DrawRectangleRoundedLinesEx(
          Rectangle{static_cast<float>(x), static_cast<float>(y),
                    static_cast<float>(w), static_cast<float>(h)},
          radius / static_cast<float>(std::max(w, h)),
          part.cls == "hp-nacelle" ? 6 : 12, 2.0f, frame);
    } else {
      DrawRectangle(x, y, w, h, hull);
      DrawRectangleLinesEx(Rectangle{static_cast<float>(x), static_cast<float>(y),
                                     static_cast<float>(w), static_cast<float>(h)},
                           2.0f, frame);
    }
  }

  for (const Area& bar : diagram.nacBars) {
    DrawRectangle(static_cast<int>(ox + bar.x), static_cast<int>(oy + bar.y),
                  static_cast<int>(bar.w), static_cast<int>(bar.h),
                  ToRay(theme_.palette.borderDark));
  }

  DrawRectangleLines(static_cast<int>(ox + diagram.corridor.x),
                     static_cast<int>(oy + diagram.corridor.y),
                     static_cast<int>(diagram.corridor.w),
                     static_cast<int>(diagram.corridor.h),
                     ToRay(theme_.palette.borderDark));
  Text(diagram.corridorLabel.c_str(),
           static_cast<int>(ox + diagram.corridorLabelBox.x),
           static_cast<int>(oy + diagram.corridorLabelBox.y), TypePx(9) ,
           ToRay(theme_.palette.muted));

  DrawRectangle(static_cast<int>(ox + diagram.conduit.x),
                static_cast<int>(oy + diagram.conduit.y),
                static_cast<int>(diagram.conduit.w),
                static_cast<int>(diagram.conduit.h), ToRay(theme_.palette.panel3));

  for (const Room& room : diagram.rooms) {
    const int x = static_cast<int>(ox + room.box.x);
    const int y = static_cast<int>(oy + room.box.y);
    const int w = static_cast<int>(room.box.w);
    const int h = static_cast<int>(room.box.h);
    DrawRectangle(x, y, w, h, theme_.id == "classic" ? HexColor(room.tone, ToRay(theme_.palette.panel2)) : ToRay(theme_.palette.panel2));
    DrawRectangleLines(x, y, w, h, ToRay(theme_.palette.borderDark));
    const auto accent = diagram.roomAccents.find(room.id);
    DrawRectangle(x, y, 3, h, accent == diagram.roomAccents.end() ? ToRay(theme_.palette.coffee) : HexColor(accent->second, ToRay(theme_.palette.coffee)));
    // Room names are always shown: in the reference the compact deck keeps
    // names at 10 px and hides only tags/sys codes. The 18 px floor applies,
    // so names may be large in compact rooms but are never hidden.
    Text(room.name.c_str(), x + 6, y + 4, TypePx(10), ToRay(theme_.palette.ink));
    if (!diagram.compact && !room.small) {
      Text(room.tag.c_str(), x + w - 30, y + h - 14, TypePx(8),
           ToRay(theme_.palette.muted));
    }
  }

  for (const Area& divider : diagram.dividers) {
    DrawLine(static_cast<int>(ox + divider.x),
             static_cast<int>(oy + divider.y),
             static_cast<int>(ox + divider.x),
             static_cast<int>(oy + divider.Bottom()),
             ToRay(theme_.palette.border));
  }

  for (const Area& nozzle : diagram.nozzles) {
    DrawRectangle(static_cast<int>(ox + nozzle.x), static_cast<int>(oy + nozzle.y),
                  static_cast<int>(nozzle.w), static_cast<int>(nozzle.h),
                  ToRay(theme_.palette.panel3));
    DrawRectangleLines(static_cast<int>(ox + nozzle.x), static_cast<int>(oy + nozzle.y),
                       static_cast<int>(nozzle.w), static_cast<int>(nozzle.h), frame);
  }

  for (const DiagramCrew& crew : diagram.crew) {
    DrawRectangle(static_cast<int>(ox + crew.dot.x), static_cast<int>(oy + crew.dot.y),
                  15, 15, HexColor(crew.colour, ToRay(theme_.palette.coffeeDark)));
    if (crew.labelled) {
      Text(crew.firstName.c_str(), static_cast<int>(ox + crew.label.x),
               static_cast<int>(oy + crew.label.y), TypePx(9) , ToRay(theme_.palette.muted));
    }
  }
}

void ShellView::Move(float x, float y) {
  mouseX_ = x;
  mouseY_ = y;
}

void ShellView::Drag(Simulation& sim, float x, float y, float dx, float dy, bool down, bool pressed) {
  Move(x, y);
  if (!down) { draggingChart_ = false; draggingFeed_ = false; return; }
  if (floating_.HasConfirmation()) { return; }
  if (titleActive_ || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) { return; }
  if (feedActive_ && down && (draggingFeed_ ||
      (pressed && layout_.content.Contains(x, y) && feedThumb_.Contains(x, y + contentScroll_)))) {
    if (!draggingFeed_) { draggingFeed_ = true; feedDragOffset_ = y + contentScroll_ - feedThumb_.y; }
    const float feedTrackTop = feedBox_.y + static_cast<float>(TypePx(11)) + 12.0f;
    const float travel = feedBox_.h - (static_cast<float>(TypePx(11)) + 16.0f) - feedThumb_.h;
    if (travel > 0) feedScroll_ = std::clamp((y + contentScroll_ - feedTrackTop - feedDragOffset_) / travel, 0.0f, 1.0f) * feedMaxScroll_;
    return;
  }
  const float localY = y + contentScroll_;
  if (!draggingChart_) {
    if (!pressed) { return; }
    if (!screenNav_ || !layout_.content.Contains(x, y) ||
        !navChartBox_.Contains(x, localY)) { return; }
    // Layer and zoom controls must not also pan the chart underneath them.
    for (const auto& hit : navHits_) {
      if (hit.action != "select" && hit.box.Contains(x, localY)) { return; }
    }
    chartDragAccumX_ = chartDragAccumY_ = 0.0;
    chartDragMoved_ = false;
    draggingChart_ = true;
    return;
  }
  chartDragAccumX_ += dx;
  chartDragAccumY_ += dy;
  chartDragMoved_ = std::abs(chartDragAccumX_) + std::abs(chartDragAccumY_) > 4.0;
  // PanMap accepts deltas, not absolute offsets. Convert once from live chart
  // pixels to the model viewport; adding the accumulated pan accelerates it.
  sim.PanMap(dx / std::max(0.01f, navChartScaleX_),
             dy / std::max(0.01f, navChartScaleY_));
}

void ShellView::Scroll(float x, float y, float wheelDelta) {
  Move(x, y);
  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) { return; }
  if (floating_.HasConfirmation()) { return; }
  const float step = wheelDelta * TypeStep(40.0f);
  if (titleActive_) { titleScroll_ = std::max(0.0f, std::min(titleMaxScroll_, titleScroll_ - step)); }
  else if (feedActive_ && layout_.content.Contains(x, y) && feedBox_.Contains(x, y + contentScroll_)) {
    feedScroll_ = std::clamp(feedScroll_ - step, 0.0f, feedMaxScroll_);
  }
  else if (wikiIndexBox_.Contains(x, y + contentScroll_)) { wikiIndexScroll_ = std::max(0.0f, std::min(wikiIndexMaxScroll_, wikiIndexScroll_ - step)); }
  else if (wikiBodyBox_.Contains(x, y + contentScroll_)) { wikiBodyScroll_ = std::max(0.0f, std::min(wikiBodyMaxScroll_, wikiBodyScroll_ - step)); }
  else if (layout_.sidepanel.Contains(x, y)) { sideScroll_ = std::max(0.0f, std::min(std::max(0.0f, sideContentH_ - layout_.sidepanel.h), sideScroll_ - step)); }
  else { contentScroll_ = std::max(0.0f, std::min(contentMaxScroll_, contentScroll_ - step)); }
}

void ShellView::Escape(Simulation& sim) {
  if (LossVisible(sim)) { lossDismissed_ = true; return; }
  if (titleActive_) {
    if (titleConfirm_) { titleConfirm_ = false; }
    else if (titlePanel_ != TitlePanel::None) { titlePanel_ = TitlePanel::None; }
    else { HideTitle(); }
    return;
  }
  if (floating_.HasConfirmation()) {
    for (auto& panel : floating_.Panels()) { panel.pendingAction.clear(); panel.question.clear(); }
    return;
  }
  if (!floating_.Panels().empty()) { ClosePopup(); return; }
  if (!pendingConfirm_.empty()) { pendingConfirm_.clear(); pendingAction_.clear(); return; }
  ShowTitle(true);
}

void ShellView::Click(Simulation& sim, float x, float y, HitButton button,
                      double nowMs) {
  if (button == HitButton::Left) {
    const bool moved = draggingChart_ && chartDragMoved_;
    draggingChart_ = false;
    chartDragMoved_ = false;
    if (moved) { return; }
  }
  if (floating_.HasConfirmation() || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) { return; }
  if (button == HitButton::None) { return; }

  // The menu covers everything below it, and the loss overlay covers the ship.
  if (titleActive_) {
    for (const NavHit& hit : titleHits_) {
      if (!hit.box.Contains(x, y)) { continue; }
      if (hit.action == "confirm-new-voyage") {
        titleConfirm_ = false;
        PressTitleAction(sim, "new-voyage", "", &fromVoyage_, &titlePanel_, &titleSlot_,
                         saveDir_, &titleMessage_, &titleActive_);
        HideTitle();
        return;
      }
      if (hit.action == "cancel-new-voyage") {
        titleConfirm_ = false;
        titleMessage_.clear();
        return;
      }
      if (hit.action == "new-voyage" || hit.action == "start-voyage") {
        if (TitleNeedsConfirmation(sim, fromVoyage_)) {
          titleConfirm_ = true;
          titleMessage_ =
              "Start a new voyage? Unsaved progress will be lost; saved slots are kept.";
          return;
        }
        titleConfirm_ = false;
        PressTitleAction(sim, hit.action, "", &fromVoyage_, &titlePanel_, &titleSlot_,
                         saveDir_, &titleMessage_, &titleActive_);
        HideTitle();
        return;
      }
      if (hit.action == "load-slot") {
        const bool loaded = PressTitleAction(sim, "load-slot", "", &fromVoyage_,
                                             &titlePanel_, &titleSlot_, saveDir_,
                                             &titleMessage_, &titleActive_);
        if (loaded) { HideTitle(); }
        return;
      }
      if (hit.action == "card:next" || hit.action == "card:prev") {
        PageTutorial(hit.action == "card:next" ? 1 : -1, nowMs);
        return;
      }
      if (hit.action == "resume") {
        if (!fromVoyage_) { quitRequested_ = true; return; }
        PressTitleAction(sim, "resume", "", &fromVoyage_, &titlePanel_, &titleSlot_,
                         saveDir_, &titleMessage_, &titleActive_);
        HideTitle();
        return;
      }
      if (hit.action == "guide" || hit.action == "tutorial") {
        titleCard_ = 0;
        tutorialDirection_ = 1;
        tutorialTransitionMs_ = nowMs;
      }
      PressTitleAction(sim, hit.action, "", &fromVoyage_, &titlePanel_, &titleSlot_,
                       saveDir_, &titleMessage_, &titleActive_);
      return;
    }
    return;
  }

  if (LossVisible(sim)) {
    for (const NavHit& hit : lossHits_) {
      if (!hit.box.Contains(x, y)) { continue; }
      bool toTitle = false;
      const bool changed = PressLossAction(sim, hit.action, "", &lossSlot_, &toTitle,
                                          saveDir_, &lossMessage_);
      if (hit.action == "main-menu" || toTitle) {
        lossDismissed_ = true;
        ShowTitle(true);
      } else if (changed && sim.State().gameOver.empty()) {
        lossDismissed_ = true;
      }
      return;
    }
    return;
  }

  // Floating windows are nonmodal. ImGui owns input only over their actual
  // bounds; exposed ship/chrome controls remain usable, as in the HTA.

  // Chrome hit-testing follows paint order: taskbar controls and tabs, then
  // sidebar and pinned foot, then the screen behind them.
  if (layout_.scrollLeft.Contains(x, y)) {
    tabScroll_ = std::max(0.0f, tabScroll_ - layout_.tabStrip.w * 0.65f);
    return;
  }
  if (layout_.scrollRight.Contains(x, y)) {
    tabScroll_ = std::min(maxTabScroll_, tabScroll_ + layout_.tabStrip.w * 0.65f);
    return;
  }
  if (layout_.home.Contains(x, y)) { SwitchTab(sim, "home"); return; }
  for (const TabHit& hit : tabHits_) {
    if (hit.closable && hit.closeBox.Contains(x, y)) { CloseTab(sim, hit.key); return; }
    if (hit.box.Contains(x, y)) { SwitchTab(sim, hit.key); return; }
  }
  for (const SideHit& hit : sideHits_) {
    if (!hit.box.Contains(x, y)) { continue; }
    switch (hit.action) {
      case SideAction::Toggle:
        if (hit.key != "indicators") { sim.MutableState().spOpen[hit.key] = !sim.MutableState().spOpen[hit.key]; }
        return;
      case SideAction::OpenEvent: OpenTask(sim, "event", hit.key, false); return;
      case SideAction::Wiki: OpenTask(sim, "wiki", "main", true); return;
      case SideAction::Rewards: OpenTask(sim, "awards", "main", true); return;
      case SideAction::Manifest: OpenPopup("manifest"); return;
      case SideAction::Log: OpenPopup("log"); return;
      case SideAction::Files: OpenPopup("files"); return;
      case SideAction::Settings: OpenPopup("settings"); return;
      case SideAction::None: break;
    }
  }

  // Screen content is painted shifted up by contentScroll_; shift the cursor
  // down by the same amount so hit testing matches what is painted, and
  // content scrolled out of view can never be clicked.
  if (!layout_.content.Contains(x, y)) { return; }
  const float scrollY = contentScroll_;

  if (y >= actionBandTop_) {
    for (const auto& hit : actionBandHits_) {
      if (!hit.box.Contains(x, y + scrollY)) continue;
      if (hit.action == "confirm-no") { pendingConfirm_.clear(); pendingAction_.clear(); return; }
      if (hit.action == "confirm-yes") {
        const std::string action = pendingAction_;
        pendingConfirm_.clear(); pendingAction_.clear();
        if (PressPanelRow(sim, panelsKind_, action)) SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
        return;
      }
      const bool positive = screenNav_ ? PressNavAction(sim, hit.action, hit.arg)
                                       : PressProgressButton(sim, hit.action);
      if (positive) SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
      return;
    }
    return; // Never activate a body control hidden behind the band.
  }

  // The quarters: a roster row, a job, an order, the sheet, a word.
  if (screenQuarters_) {
    for (const NavHit& hit : navHits_) {
      // The opaque pinned band occludes body controls, including blank parts
      // of the band. Never activate an order painted behind that footer.
      if (y >= quartersBandTop_ && hit.action != "commit") { continue; }
      if (!hit.box.Contains(x, y + scrollY)) { continue; }
      if (PressQuartersAction(sim, hit.action, hit.arg) &&
          (hit.action == "commit" || hit.action == "talk")) {
        SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
      }
      return;
    }
    return;
  }
  if (screenRecord_) {
    for (const NavHit& hit : navHits_) {
      if (!hit.box.Contains(x, y + scrollY)) { continue; }
      if (hit.action == "talk" && PressConversation(sim, hit.arg)) {
        SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
      }
      return;
    }
    return;
  }

  // Station and stores: each command row carries its own action id.
  if (screenPanels_) {
    for (const NavHit& hit : navHits_) {
      if (!hit.box.Contains(x, y + scrollY)) { continue; }
      // Read the row's own question before acting: a purchase that cannot be
      // undone asks first when the reader keeps confirmations on.
      std::string question;
      const TaskScreenModel& screen = taskScreen_;
      for (std::size_t c = 0; c < screen.panelRowIds.size(); ++c) {
        for (std::size_t r = 0; r < screen.panelRowIds[c].size(); ++r) {
          if (screen.panelRowIds[c][r] != hit.arg) { continue; }
          if (c < screen.panelRows.size() && r < screen.panelRows[c].size()) {
            question = screen.panelRows[c][r].confirm;
          }
        }
      }
      // The ship purchase always asks; other risk prompts follow the option.
      if (!question.empty() && (sim.State().options.confirmRisk ||
                                (panelsKind_ == ScreenKind::Station && hit.arg == "ship-buy"))) {
        pendingConfirm_ = question;
        pendingAction_ = hit.arg;
        return;
      }
      const double actionsBefore = sim.ActionCount();
      const bool acted = PressPanelRow(sim, panelsKind_, hit.arg);
      if (acted && (panelsKind_ != ScreenKind::Wiki || sim.ActionCount() > actionsBefore)) {
        SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
      }
      return;
    }
  }

  // The chart's own controls: bodies, the zoom pair, the layers, the actions.
  if (screenNav_) {
    for (auto it = navHits_.rbegin(); it != navHits_.rend(); ++it) {
      const NavHit& hit = *it;
      if (!hit.box.Contains(x, y + scrollY)) { continue; }
      if (PressNavAction(sim, hit.action, hit.arg) &&
          (hit.action == "travel" || hit.action == "scan")) {
        SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
      }
      return;
    }
    return;
  }

  // A task window's own controls come first.
  for (const auto& entry : taskButtons_) {
    if (!entry.first.Contains(x, y + scrollY)) { continue; }
    if (PressProgressButton(sim, entry.second)) {
      SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
    }
    return;
  }

  if (layout_.home.Contains(x, y)) {
    SwitchTab(sim, "home");
    return;
  }

  if (sim.State().activeTab == "home") {
    // The whole home column is painted shifted up by contentScroll_; shift the
    // cursor down by the same amount so a click lands on what is actually
    // painted, and never on content scrolled out of view.
    const float scrollY = contentScroll_;
    // The view switch and room callouts are presentation only. They route to
    // exactly the same room handlers as the original deck plan.
    if (HasWrenExterior(sim) && exteriorView_) {
      for (const ExteriorCallout& item : exterior_.callouts) {
        if (!item.box.Contains(x, y + scrollY)) continue;
        if (button == HitButton::Right) { OpenTask(sim, "room", item.id, false); }
        else {
          OpenBriefing(sim, "room", item.id);
          SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
        }
        return;
      }
      if (exterior_.image.Contains(x, y + scrollY)) {
        exteriorView_ = false;
        return;
      }
    } else if (idle_.stageArea.Contains(x, y + scrollY)) {
      const float localX = x - idle_.diagram.stage.x;
      const float localY = y + scrollY - idle_.diagram.stage.y;
      for (const DiagramCrew& crew : idle_.diagram.crew) {
        const Area hit{crew.dot.x - 6.0f, crew.dot.y - 6.0f, 27.0f, 27.0f};
        const Area labelHit{crew.label.x, crew.label.y,
                            crew.labelled ? TextWidth(crew.firstName.c_str(), TypePx(9)) : 0.0f,
                            crew.labelled ? TypeStep(12.0f) : 0.0f};
        if (!hit.Contains(localX, localY) && !labelHit.Contains(localX, localY)) { continue; }
        if (button == HitButton::Right) {
          OpenTask(sim, "crew", crew.id, false);
        } else {
          OpenBriefing(sim, "crew", crew.id);
        }
        return;
      }
      for (const Room& room : idle_.diagram.rooms) {
        if (localX < room.box.x || localY < room.box.y ||
            localX >= room.box.x + room.box.w || localY >= room.box.y + room.box.h) {
          continue;
        }
        if (button == HitButton::Right) {
          OpenTask(sim, "room", room.id, false);
        } else {
          OpenBriefing(sim, "room", room.id);
          SpawnMark(marks_, x, y, nowMs,
                    sim.State().reducedMotion);
        }
        return;
      }
      if (HasWrenExterior(sim)) {
        exteriorView_ = true;
        return;
      }
    }
    for (const RailCell& cell : idle_.stats) {
      if (!cell.box.Contains(x, y + scrollY)) { continue; }
      if (button == HitButton::Right) {
        OpenTask(sim, "system", cell.id, false);
      } else {
        OpenBriefing(sim, "system", cell.id);
      }
      return;
    }
    for (const RailCell& cell : idle_.infos) {
      if (!cell.box.Contains(x, y + scrollY)) { continue; }
      if (button == HitButton::Right) {
        OpenTask(sim, "info", cell.id, false);
      } else {
        OpenBriefing(sim, "info", cell.id);
      }
      return;
    }
    if (idle_.nav.box.Contains(x, y + scrollY)) {
      if (button == HitButton::Right) {
        OpenTask(sim, "nav", "main", false);
      } else {
        OpenBriefing(sim, "nav", "main");
      }
      return;
    }
    for (const HomeEventCard& event : idle_.events) {
      if (!event.box.Contains(x, y + scrollY)) { continue; }
      if (event.station) {
        if (button == HitButton::Right) { OpenTask(sim, "station", event.id, false); }
        else { OpenBriefing(sim, "station", event.id); }
      } else if (!event.id.empty()) {
        OpenTask(sim, "event", event.id, false);
      }
      return;
    }
  }
}

}  // namespace ship
