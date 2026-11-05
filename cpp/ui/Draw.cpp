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

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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

// WrapText(): greedy wrapping by measured width, so a long paragraph in a
// dialog or an article breaks where the reference's CSS would break it.
std::vector<std::string> WrapText(const std::string& text, float width, int size,
                                  const Font& font) {
  std::vector<std::string> lines;
  std::string line;
  std::size_t i = 0;
  while (i <= text.size()) {
    // A explicit newline is a break of its own.
    if (i == text.size() || text[i] == '\n') {
      lines.push_back(line);
      line.clear();
      if (i == text.size()) { break; }
      i += 1;
      continue;
    }
    if (text[i] == ' ') {
      const std::string withSpace = line.empty() ? line : line + " ";
      if (!line.empty() && MeasureTextEx(font, (withSpace + "x").c_str(),
                                         static_cast<float>(size), 1.0f).x > width) {
        lines.push_back(line);
        line.clear();
      } else {
        line = withSpace;
      }
      i += 1;
      continue;
    }
    line.push_back(text[i]);
    i += 1;
  }
  if (lines.empty()) { lines.push_back(std::string()); }
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

int ShellView::TypePx(int reference) const {
  return reference <= 0
             ? reference
             : static_cast<int>(std::lround(reference * (theme_.FontPx() / 13.0)));
}

void ShellView::Text(const char* text, int x, int y, int size, ::Color tint,
                     bool mono) const {
  const Font& font = mono ? monoFont_ : (size >= TypePx(16) ? boldFont_ : bodyFont_);
  DrawTextEx(font, text, Vector2{static_cast<float>(x), static_cast<float>(y)},
             static_cast<float>(size), 1.0f, tint);
}

float ShellView::TextWidth(const char* text, int size, bool mono) const {
  const Font& font = mono ? monoFont_ : bodyFont_;
  return MeasureTextEx(font, text, static_cast<float>(size), 1.0f).x;
}

ShellView::ShellView() { theme_ = Theme::ById("classic"); }

void ShellView::Configure(float width, float height, const Theme& theme) {
  width_ = width;
  height_ = height;
  theme_ = theme;
  layout_ = ComputeLayout(width_, height_, height_ - Metrics::kContentTop -
                                              Metrics::kStatusH,
                          others_, theme_.stageCompact);
}

void ShellView::Draw(Simulation& sim, double nowMs) {
  const float contentH = height_ - Metrics::kContentTop - Metrics::kStatusH;
  layout_ = ComputeLayout(width_, height_, contentH, others_, theme_.stageCompact);

  // Always build Home, including when reduced motion is enabled. Keep drift
  // state across frames and advance it at the reference cadence; rebuilding
  // random dots every frame caused teleporting and a blank reduced-motion view.
  if (sim.State().activeTab == "home") {
    FxRng seeded = rng_;
    idle_ = BuildIdleScreen(sim, layout_, theme_, theme_.hideCrewLabels, others_, seeded);
    rng_ = seeded;
    SyncIdleDots(sim, nowMs);
  }

  UpdateMarks(marks_, nowMs);

  // A lost voyage that has been dealt with (loaded or restarted) puts the
  // overlay away for good; a fresh loss brings it back.
  if (sim.State().gameOver.empty()) { lossDismissed_ = false; }

  ClearBackground(ToRay(theme_.palette.panel));

  if (titleActive_) {
    PaintTitle(sim);
    return;
  }

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
    const ScreenKind kind = tab == nullptr ? ScreenKind::Described : ScreenKindFor(*tab);
    screenNav_ = kind == ScreenKind::Nav;
    screenQuarters_ = kind == ScreenKind::Quarters;
    screenRecord_ = kind == ScreenKind::CrewRecord;
    screenPanels_ = kind == ScreenKind::Station || kind == ScreenKind::Cargo ||
                    kind == ScreenKind::Science || kind == ScreenKind::Room ||
                    kind == ScreenKind::SimCore || kind == ScreenKind::Awards ||
                    kind == ScreenKind::Event || kind == ScreenKind::Wiki;
    panelsKind_ = kind;
    if (taskTabKey_ != sim.State().activeTab) {
      pendingConfirm_.clear();
      pendingAction_.clear();
    }
    taskTabKey_ = sim.State().activeTab;
    taskScreen_ = BuildTabScreen(sim, *FindTab(sim.State(), taskTabKey_), wiki_);
    if (screenNav_) {
      navScreen_ = BuildNavScreen(sim);
      PaintNavScreen(sim);
    } else if (screenQuarters_) {
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
  PaintChrome(sim);
  if (!popupKey_.empty()) { PaintPopup(sim); }
  if (LossVisible(sim)) { PaintLoss(sim); }

  // Action marks last, over everything, because they are the player's hand.
  for (const ActionMark& mark : marks_) {
    const MarkStyle style = MarkAt(mark, nowMs);
    if (!style.alive) { continue; }
    const int alpha = static_cast<int>(style.opacity * 255.0f);
    const float x = mark.x + layout_.content.x;
    DrawLine(static_cast<int>(x), static_cast<int>(style.top),
             static_cast<int>(x + 11.0f), static_cast<int>(style.top),
             ToRay(theme_.palette.coffee, alpha));
    DrawLine(static_cast<int>(x + 5.0f), static_cast<int>(style.top - 5.0f),
             static_cast<int>(x + 5.0f), static_cast<int>(style.top + 6.0f),
             ToRay(theme_.palette.coffee, alpha));
  }

}

void ShellView::SyncIdleDots(const Simulation& sim, double nowMs) {
  const bool compact = sim.State().shipClass == "wren";
  if (dots_.size() != idle_.diagram.crew.size()) {
    dots_.clear();
    for (const DiagramCrew& crew : idle_.diagram.crew) {
      DriftDot dot;
      dot.id = crew.id;
      dot.roomId = crew.roomId;
      dot.x = dot.tx = crew.dot.x;
      dot.y = dot.ty = crew.dot.y;
      dots_.push_back(dot);
    }
    lastDriftMs_ = nowMs;
  }
  if (!sim.State().reducedMotion && lastDriftMs_ <= 0.0) { lastDriftMs_ = nowMs; }
  const int ticks = sim.State().reducedMotion || lastDriftMs_ <= 0.0
                        ? 0
                        : std::min(8, static_cast<int>((nowMs - lastDriftMs_) / FxTiming::kDriftMs));
  if (ticks > 0) {
    for (DriftDot& dot : dots_) {
      const auto room = std::find_if(idle_.diagram.rooms.begin(), idle_.diagram.rooms.end(),
                                     [&dot](const Room& r) { return r.id == dot.roomId; });
      if (room == idle_.diagram.rooms.end()) { continue; }
      const Area roomBox{static_cast<float>(room->box.x), static_cast<float>(room->box.y),
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

void ShellView::PaintChrome(Simulation& sim) {
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
                      ToIm(p.panel2));
  draw->AddLine(ImVec2(0.0f, Metrics::kTaskbarH - 1.0f),
                ImVec2(width_, Metrics::kTaskbarH - 1.0f), ToIm(p.borderDark));

  const std::vector<TabView> tabs = TabBar(sim);
  std::vector<float> tabWidths;
  float tabsWidth = 4.0f;
  for (const TabView& tab : tabs) {
    const float tabW = ImGui::CalcTextSize(tab.title.c_str()).x + (tab.closable ? 30.0f : 18.0f);
    tabWidths.push_back(tabW);
    tabsWidth += tabW + 4.0f;
  }
  maxTabScroll_ = std::max(0.0f, tabsWidth - layout_.tabStrip.w);
  tabScroll_ = std::max(0.0f, std::min(maxTabScroll_, tabScroll_));
  const auto active = std::find_if(tabs.begin(), tabs.end(), [](const TabView& t) { return t.active; });
  if (active != tabs.end() && active->key != lastRevealedTab_) {
    const std::size_t index = static_cast<std::size_t>(active - tabs.begin());
    float start = 4.0f;
    for (std::size_t i = 0; i < index; ++i) { start += tabWidths[i] + 4.0f; }
    const float end = start + tabWidths[index];
    if (end - tabScroll_ > layout_.tabStrip.w) { tabScroll_ = std::min(maxTabScroll_, end - layout_.tabStrip.w); }
    if (start - tabScroll_ < 0.0f) { tabScroll_ = start; }
    lastRevealedTab_ = active->key;
  }
  tabHits_.clear();
  draw->PushClipRect(ImVec2(0.0f, 0.0f), ImVec2(layout_.tabStrip.w, Metrics::kTaskbarH), true);
  float x = 4.0f - tabScroll_;
  for (std::size_t i = 0; i < tabs.size(); ++i) {
    const TabView& tab = tabs[i];
    const float w = tabWidths[i];
    const Area tabBox{x, 3.0f, w, Metrics::kTaskbarH - 5.0f};
    const Area closeBox{tabBox.Right() - 19.0f, tabBox.y, 18.0f, tabBox.h};
    tabHits_.push_back(TabHit{tab.key, tabBox, closeBox, tab.closable});
    const ImU32 fill = tab.active ? ToIm(p.white)
                       : tab.fresh ? ToIm(p.coffeeLight)
                                   : ToIm(p.panel3);
    draw->AddRectFilled(ImVec2(x, 3.0f), ImVec2(x + w, Metrics::kTaskbarH - 2.0f), fill, 3.0f);
    draw->AddRect(ImVec2(x, 3.0f), ImVec2(x + w, Metrics::kTaskbarH - 2.0f), ToIm(p.border), 3.0f);
    draw->AddText(ImVec2(x + 7.0f, 9.0f), ToIm(p.muted), tab.title.c_str());
    if (tab.closable) { draw->AddText(ImVec2(x + w - 13.0f, 9.0f), ToIm(p.muted), "x"); }
    x += w + 4.0f;
  }
  draw->PopClipRect();

  // Home and the two scroll arrows live in the reserved right-hand slot.
  draw->AddRectFilled(ImVec2(layout_.home.x, 0.0f),
                      ImVec2(layout_.home.Right(), Metrics::kTaskbarH), ToIm(p.panel3));
  draw->AddText(ImVec2(layout_.home.x + 10.0f, 9.0f), ToIm(p.coffee), "Home");
  draw->AddText(ImVec2(layout_.scrollLeft.x + 12.0f, 9.0f), ToIm(p.muted), "<");
  draw->AddText(ImVec2(layout_.scrollRight.x + 12.0f, 9.0f), ToIm(p.muted), ">");
  (void)tabScroll_;

  // ---- sidebar -----------------------------------------------------------
  draw->AddRectFilled(ImVec2(layout_.sidepanel.x, layout_.sidepanel.y),
                      ImVec2(layout_.sidepanel.Right(), layout_.sidepanel.Bottom()),
                      ToIm(p.panel));

  sideHits_ = SideHits(sim, layout_.sidepanel, layout_.sidefoot, theme_.LineHeight(),
                       sideScroll_, &sideContentH_);
  const std::vector<SideHit>& hits = sideHits_;
  const std::vector<SideBlock> blocks = SideBlocks(sim);
  for (const SideHit& hit : hits) {
    if (hit.action == SideAction::Toggle && hit.header) {
      // The caret: open blocks point down, folded ones point right.
      bool open = true;
      for (const SideBlock& block : blocks) {
        if (block.key == hit.key) { open = block.open; }
      }
      draw->AddText(ImVec2(hit.box.x, hit.box.y), ToIm(p.coffee), hit.text.c_str());
      draw->AddText(ImVec2(hit.box.Right() - 10.0f, hit.box.y), ToIm(p.muted),
                    open ? "v" : ">");
      if (hit.key == "indicators") {
        const std::vector<IndicatorRect> lines = PlaceIndicators(IndicatorLines(sim));
        const float base = hit.box.Bottom() + 4.0f;
        for (const IndicatorRect& line : lines) {
          draw->AddRectFilled(ImVec2(hit.box.x + line.ghost.x, base + line.ghost.y),
                              ImVec2(hit.box.x + line.ghost.x + 2.0f,
                                     base + line.ghost.Bottom()),
                              ToIm(p.panel3));
          draw->AddRectFilled(
              ImVec2(hit.box.x + line.line.x, base + line.line.y),
              ImVec2(hit.box.x + line.line.x + 2.0f, base + line.line.Bottom()),
              line.grow ? ToIm(p.terracotta) : ToIm(p.sage));
        }
      }
      continue;
    }
    if (hit.action == SideAction::OpenEvent) {
      draw->AddText(ImVec2(hit.box.x, hit.box.y), ToIm(p.ink), hit.text.c_str());
      draw->AddRect(ImVec2(hit.box.Right() - 34.0f, hit.box.y + 12.0f),
                    ImVec2(hit.box.Right(), hit.box.y + 26.0f), ToIm(p.borderDark), 3.0f);
      draw->AddText(ImVec2(hit.box.Right() - 28.0f, hit.box.y + 14.0f), ToIm(p.muted),
                    "Open");
      continue;
    }
    if (hit.box.y < layout_.sidepanel.y) { continue; }   // the foot is drawn below
    draw->AddText(ImVec2(hit.box.x, hit.box.y),
                  ToIm(hit.dim ? p.muted : p.ink), hit.text.c_str());
    if (!hit.detail.empty()) {
      draw->AddText(ImVec2(hit.box.x, hit.box.y + 12.0f), ToIm(p.muted),
                    hit.detail.c_str());
    }
  }

  // ---- pinned foot -------------------------------------------------------
  draw->AddLine(ImVec2(0.0f, layout_.sidefoot.y),
                ImVec2(layout_.sidefoot.Right(), layout_.sidefoot.y), ToIm(p.border));
  for (const SideHit& hit : hits) {
    if (hit.box.y < layout_.sidefoot.y) { continue; }
    if (hit.action == SideAction::Manifest || hit.action == SideAction::Log ||
        hit.action == SideAction::Files || hit.action == SideAction::Settings) {
      draw->AddRect(ImVec2(hit.box.x, hit.box.y),
                    ImVec2(hit.box.Right(), hit.box.Bottom()), ToIm(p.borderDark), 3.0f);
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

  // ---- status bar --------------------------------------------------------
  draw->AddRectFilled(ImVec2(0.0f, layout_.statusbar.y),
                      ImVec2(width_, layout_.statusbar.Bottom()), ToIm(p.panel2));
  draw->AddLine(ImVec2(0.0f, layout_.statusbar.y), ImVec2(width_, layout_.statusbar.y),
                ToIm(p.border));
  draw->AddText(ImVec2(8.0f, layout_.statusbar.y + 4.0f), ToIm(p.ink),
                StatusText(sim).c_str());

  // ---- the briefing popup a left-click opens -----------------------------
  if (hasSelection_) {
    ImGui::SetNextWindowPos(ImVec2(layout_.content.x + 40.0f, layout_.content.y + 60.0f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ToIm(p.panel));
    ImGui::PushStyleColor(ImGuiCol_Text, ToIm(p.ink));
    ImGui::PushStyleColor(ImGuiCol_Border, ToIm(p.borderDark));
    ImGui::Begin("Briefing", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted(selection_.kindLabel.c_str());
    ImGui::TextWrapped("%s", selection_.title.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("%s", selection_.sub.c_str());
    for (const DescribeRow& row : selection_.rows) {
      ImGui::Text("%s", row.key.c_str());
      ImGui::SameLine(140.0f);
      ImGui::TextWrapped("%s", row.value.c_str());
    }
    for (const DescribeAct& act : selection_.acts) {
      ImGui::TextDisabled("%s - %s", act.label.c_str(), act.cost.c_str());
    }
    if (ImGui::Button("Open Window")) {
      OpenTask(sim, briefingKind_, briefingId_, true);
      hasSelection_ = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) { hasSelection_ = false; }
    ImGui::End();
    ImGui::PopStyleColor(3);
  }

  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

void ShellView::PaintIdle(Simulation& sim) {
  const Theme& theme = theme_;

  // ---- stat and info rails ----------------------------------------------
  for (const RailCell& cell : idle_.stats) {
    DrawRectangleLines(static_cast<int>(cell.box.x), static_cast<int>(cell.box.y),
                       static_cast<int>(cell.box.w), static_cast<int>(cell.box.h),
                       ToRay(theme.palette.border));
    Text(cell.label.c_str(), static_cast<int>(cell.box.x + 6.0f),
             static_cast<int>(cell.box.y + 4.0f), TypePx(11) , ToRay(theme.palette.muted));
    Text(cell.value.c_str(), static_cast<int>(cell.box.x + 6.0f),
         static_cast<int>(cell.box.y + 20.0f), TypePx(20), ToRay(theme.palette.ink), true);
    const float bar = cell.box.w - 12.0f;
    DrawRectangle(static_cast<int>(cell.box.x + 6.0f),
                  static_cast<int>(cell.box.Bottom() - 12.0f), static_cast<int>(bar), 4,
                  ToRay(theme.palette.panel3));
    DrawRectangle(static_cast<int>(cell.box.x + 6.0f),
                  static_cast<int>(cell.box.Bottom() - 12.0f),
                  static_cast<int>(bar * std::max(0.0, std::min(100.0, cell.reading)) / 100.0),
                  4, ToRay(theme.palette.coffee));
  }

  PaintDiagram(idle_.diagram, idle_.stageArea, idleDiagramScale_);

  Text(idle_.stageNote.c_str(), static_cast<int>(idle_.content.x + 8.0f),
           static_cast<int>(idle_.stageArea.Bottom() + 8.0f), TypePx(12) ,
           ToRay(theme.palette.muted));

  // ---- nav strip, dock strip, info rail ---------------------------------
  auto paintStrip = [this, &theme](const Strip& strip) {
    DrawRectangleLines(static_cast<int>(strip.box.x), static_cast<int>(strip.box.y),
                       static_cast<int>(strip.box.w), static_cast<int>(strip.box.h),
                       ToRay(theme.palette.border));
    Text(strip.label.c_str(), static_cast<int>(strip.box.x + 8.0f),
             static_cast<int>(strip.box.y + 6.0f), TypePx(11) , ToRay(theme.palette.muted));
    Text(strip.main.c_str(), static_cast<int>(strip.box.x + 8.0f),
             static_cast<int>(strip.box.y + 20.0f), TypePx(13) , ToRay(theme.palette.ink));
    Text(strip.right.c_str(), static_cast<int>(strip.box.x + 8.0f),
         static_cast<int>(strip.box.y + 32.0f), TypePx(10), ToRay(theme.palette.muted), true);
  };
  paintStrip(idle_.nav);
  if (idle_.docked) { paintStrip(idle_.dock); }

  for (const RailCell& cell : idle_.infos) {
    Text(cell.label.c_str(), static_cast<int>(cell.box.x), static_cast<int>(cell.box.y), TypePx(10) , ToRay(theme.palette.muted));
    Text(cell.value.c_str(), static_cast<int>(cell.box.x),
         static_cast<int>(cell.box.y + 13.0f), TypePx(13), ToRay(theme.palette.ink), true);
  }

  // ---- feed --------------------------------------------------------------
  Text(idle_.feedHead.c_str(), static_cast<int>(idle_.feedBox.x),
           static_cast<int>(idle_.feedBox.y - 16.0f), TypePx(11) , ToRay(theme.palette.coffee));
  DrawRectangleLines(static_cast<int>(idle_.feedBox.x),
                     static_cast<int>(idle_.feedBox.y),
                     static_cast<int>(idle_.feedBox.w),
                     static_cast<int>(idle_.feedBox.h), ToRay(theme.palette.border));
  float y = idle_.feedBox.y + 4.0f;
  for (const std::string& line : idle_.feed) {
    if (y > idle_.feedBox.Bottom() - 12.0f) { break; }
    Text(line.c_str(), static_cast<int>(idle_.feedBox.x + 6.0f),
             static_cast<int>(y), TypePx(12) , ToRay(theme.palette.ink));
    y += theme.LineHeight();
  }
  (void)sim;
}

void ShellView::PaintPanel(const NavPanel& panel, const Area& box) {
  const Palette& p = theme_.palette;
  DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.panel));
  DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                     static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.border));
  BeginScissorMode(static_cast<int>(box.x + 1.0f), static_cast<int>(box.y + 1.0f),
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
    const std::vector<std::string> rows = WrapText(value, std::max(1.0f, width), small, bodyFont_);
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
        Text(item.value.c_str(), static_cast<int>(box.x + 8.0f), static_cast<int>(y),
             TypePx(18), ToRay(p.ink));
        y += TypeStep(23.0f);
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
          5, ToRay(p.coffee));
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

void ShellView::PaintStage(const TaskStage& stage, const Area& origin) {
  const Palette& p = theme_.palette;
  for (const MachinePart& part : stage.parts) {
    const int x = static_cast<int>(origin.x + part.box.x);
    const int y = static_cast<int>(origin.y + part.box.y);
    const int w = std::max(1, static_cast<int>(part.box.w));
    const int h = std::max(1, static_cast<int>(part.box.h));
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
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                       static_cast<int>(box.w), static_cast<int>(box.h),
                       ToRay(p.borderDark));
    Text(control.button.text.c_str(), static_cast<int>(box.x + 6.0f),
             static_cast<int>(box.y + 7.0f), TypePx(10) ,
             ToRay(locked ? p.terracotta : p.ink));
    taskButtons_.push_back({box, control.button.taskId});
  }
}

void ShellView::PaintTaskScreen(Simulation& sim) {
  taskButtons_.clear();
  const Palette& p = theme_.palette;
  const TaskScreenModel& screen = taskScreen_;
  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;
  const float column = 250.0f;

  Text(screen.head.kind.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y), TypePx(9) , ToRay(p.muted));
  Text(screen.head.title.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 12.0f), TypePx(20) , ToRay(p.ink));
  Text(screen.head.sub.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 36.0f), TypePx(11) , ToRay(p.muted));

  float y = layout_.content.y + 58.0f;
  for (const NavPanel& panel : screen.left) {
    PaintPanel(panel, Area{left, y, column, 118.0f});
    y += 124.0f;
  }

  float ry = layout_.content.y + 58.0f;
  for (const NavPanel& panel : screen.right) {
    PaintPanel(panel, Area{right - column, ry, column, 150.0f});
    ry += 156.0f;
  }

  if (screen.hasStage) {
    const Area origin{left + column + 16.0f, layout_.content.y + 58.0f,
                      screen.stage.width, screen.stage.height};
    PaintStage(screen.stage, origin);
    float uy = origin.y + screen.stage.height + 4.0f;
    const float halfW = (right - column - 16.0f - (left + column + 16.0f) - 4.0f) / 2.0f;
    for (std::size_t i = 0; i < screen.under.size(); ++i) {
      PaintPanel(screen.under[i],
                 Area{origin.x + static_cast<float>(i) * (halfW + 4.0f), uy, halfW,
                      110.0f});
    }
  }

  // The commitments band, along the bottom of the content column.
  const float bandY = layout_.content.Bottom() - 96.0f;
  DrawRectangle(static_cast<int>(left), static_cast<int>(bandY),
                static_cast<int>(right - left), 90, ToRay(p.panel));
  DrawRectangleLines(static_cast<int>(left), static_cast<int>(bandY),
                     static_cast<int>(right - left), 90, ToRay(p.border));
  Text("Actions", static_cast<int>(left + 8.0f),
           static_cast<int>(bandY + 5.0f), TypePx(11) , ToRay(p.coffee));
  Text(screen.bandNote.c_str(), static_cast<int>(left + 8.0f),
           static_cast<int>(bandY + 20.0f), TypePx(10) , ToRay(p.muted));

  float bx = left + 12.0f;
  for (const ProgressButton& button : screen.progress) {
    const Area box{bx, bandY + 50.0f, 190.0f, 28.0f};
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                  static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.panel2));
    if (button.fillPercent > 0.0) {
      DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                    static_cast<int>(box.w * button.fillPercent / 100.0),
                    static_cast<int>(box.h), ToRay(p.white));
    }
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                       static_cast<int>(box.w), static_cast<int>(box.h),
                       ToRay(p.borderDark));
    Text(button.text.c_str(), static_cast<int>(box.x + 8.0f),
             static_cast<int>(box.y + 8.0f), TypePx(10) ,
             ToRay(button.state == ButtonState::Cooldown ? p.terracotta : p.ink));
    taskButtons_.push_back({box, button.taskId});
    bx += 198.0f;
  }
  (void)sim;
}

void ShellView::PaintNavScreen(Simulation& sim) {
  navHits_.clear();
  const Palette& p = theme_.palette;
  const TaskScreenModel& screen = navScreen_.screen;
  const NavChartView& chart = navScreen_.chart;

  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;
  const float column = 214.0f;
  const float middleLeft = left + column + 8.0f;
  const float middleRight = right - column - 8.0f;

  Text(screen.head.kind.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y), TypePx(9) , ToRay(p.muted));
  Text(screen.head.title.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 12.0f), TypePx(20) , ToRay(p.ink));
  Text(screen.head.sub.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 36.0f), TypePx(11) , ToRay(p.muted));

  // --- the chart box: 638x278 at 1:1, shrunk only when the window is narrower
  const float spareW = middleRight - middleLeft;
  float scale = 1.0f;
  if (chart.zoom >= 0 && spareW < 638.0f) { scale = spareW / 638.0f; }
  const Area box{middleLeft, layout_.content.y + 58.0f, 638.0f * scale,
                 278.0f * scale};
  navChartBox_ = box;

  DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.panel));
  DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                     static_cast<int>(box.w), static_cast<int>(box.h),
                     ToRay(p.borderDark));

  BeginScissorMode(static_cast<int>(box.x) + 1, static_cast<int>(box.y) + 1,
                   static_cast<int>(box.w) - 2, static_cast<int>(box.h) - 2);
  const ship::NavRaster raster = BuildNavRaster(sim);
  const float zs = box.w / static_cast<float>(raster.width);

  // The rungs: depth lines across the chart, faint far, solid near.
  for (const NavRasterRung& rung : raster.rungs) {
    const int y = static_cast<int>(box.y + static_cast<float>(rung.y) * zs);
    DrawRectangle(static_cast<int>(box.x), y, static_cast<int>(box.w), 1,
                  Fade(ToRay(p.ink), static_cast<float>(rung.alpha)));
  }
  // The fan: hairlines converging on a point held above the top edge.
  for (const NavRasterLine& line : raster.lines) {
    const double pi = 3.14159265358979323846;
    const double rad = line.angle * pi / 180.0;
    const double dx = std::cos(rad);
    const double dy = std::sin(rad);
    const float x1 = box.x + static_cast<float>(raster.vx - dx * 4000) * zs;
    const float y1 = box.y + static_cast<float>(raster.vy - dy * 4000) * zs;
    const float x2 = box.x + static_cast<float>(raster.vx + dx * 4000) * zs;
    const float y2 = box.y + static_cast<float>(raster.vy + dy * 4000) * zs;
    DrawLineEx(Vector2{x1, y1}, Vector2{x2, y2}, 1.0f, Fade(ToRay(p.ink), 0.13f));
  }

  for (const NavDot& dot : chart.clusters) {
    const float px = box.x + static_cast<float>(dot.x) * zs;
    const float py = box.y + static_cast<float>(dot.y) * zs;
    DrawRectangle(static_cast<int>(px), static_cast<int>(py), 1, 1,
                  ToRay(dot.cls.find("bright") != std::string::npos ? p.coffee
                                                                  : p.muted));
  }
  for (const NavDot& dot : chart.routes) {
    const float px = box.x + static_cast<float>(dot.x) * zs;
    const float py = box.y + static_cast<float>(dot.y) * zs;
    const Color ink = dot.cls.find("sel") != std::string::npos
                          ? ToRay(p.sage)
                          : (dot.cls.find("faint") != std::string::npos
                                 ? Fade(ToRay(p.coffeeLight), 0.55f)
                                 : Fade(ToRay(p.coffee), 0.8f));
    DrawRectangle(static_cast<int>(px), static_cast<int>(py), 2, 2, ink);
  }

  // The ship's ring goes DOWN FIRST: painted last it swallows every click
  // aimed at the body the ship is parked at.
  if (chart.hasMarker) {
    const float px = box.x + static_cast<float>(chart.marker.x) * zs;
    const float py = box.y + static_cast<float>(chart.marker.y) * zs;
    DrawCircleLines(static_cast<int>(px), static_cast<int>(py), 13.0f,
                    ToRay(p.sage));
  }

  for (const NavBodyView& body : chart.bodies) {
    const float cx = box.x + static_cast<float>(body.at.x) * zs;
    const float cy = box.y + static_cast<float>(body.at.y) * zs;
    const float radius = static_cast<float>(body.size) * 0.42f * zs;
    Color fill = ToRay(p.coffee);
    if (body.type == "station") { fill = ToRay(p.coffeeLight); }
    if (body.type == "star") { fill = ToRay(p.ink); }
    if (body.type == "region") { fill = ToRay(p.panel3); }
    if (body.type == "origin") { fill = ToRay(p.sage); }
    DrawCircleV(Vector2{cx, cy}, radius, fill);
    if (body.selected) {
      DrawCircleLines(static_cast<int>(cx), static_cast<int>(cy), radius + 3.0f,
                      ToRay(p.terracotta));
    }
    const int labelX =
        static_cast<int>(box.x + static_cast<float>(body.labelX) * zs);
    Text(body.name.c_str(), labelX,
             static_cast<int>(box.y + static_cast<float>(body.labelY) * zs), TypePx(9) ,
             ToRay(body.scanned ? p.ink : p.muted));
    navHits_.push_back(NavHit{Area{cx - radius - 4.0f, cy - radius - 4.0f,
                                   radius * 2.0f + 8.0f, radius * 2.0f + 8.0f},
                              "select", body.id});
  }
  EndScissorMode();

  // --- the zoom pair, the layer stack and the scale caption
  float bx = box.x + 4.0f;
  for (const std::string& key : NavLayerOrder()) {
    const bool on = key == chart.layer;
    const Area boxArea{bx, box.Bottom() - 20.0f, 54.0f, 16.0f};
    DrawRectangle(static_cast<int>(boxArea.x), static_cast<int>(boxArea.y),
                  static_cast<int>(boxArea.w), static_cast<int>(boxArea.h),
                  ToRay(on ? p.panel3 : p.panel));
    DrawRectangleLines(static_cast<int>(boxArea.x), static_cast<int>(boxArea.y),
                       static_cast<int>(boxArea.w), static_cast<int>(boxArea.h),
                       ToRay(p.borderDark));
    Text(NavLayerLabel(key).c_str(), static_cast<int>(boxArea.x + 6.0f),
             static_cast<int>(boxArea.y + 4.0f), TypePx(9) ,
             ToRay(on ? p.ink : p.muted));
    navHits_.push_back(NavHit{boxArea, "layer", key});
    bx += 58.0f;
  }
  const Area zoomIn{box.Right() - 44.0f, box.y + 4.0f, 18.0f, 18.0f};
  const Area zoomOut{box.Right() - 22.0f, box.y + 4.0f, 18.0f, 18.0f};
  for (const Area& z : {zoomIn, zoomOut}) {
    DrawRectangle(static_cast<int>(z.x), static_cast<int>(z.y),
                  static_cast<int>(z.w), static_cast<int>(z.h), ToRay(p.panel2));
    DrawRectangleLines(static_cast<int>(z.x), static_cast<int>(z.y),
                       static_cast<int>(z.w), static_cast<int>(z.h),
                       ToRay(p.borderDark));
  }
  Text("+", static_cast<int>(zoomIn.x + 6.0f), static_cast<int>(zoomIn.y + 3.0f), TypePx(11) , ToRay(p.ink));
  Text("-", static_cast<int>(zoomOut.x + 6.0f),
           static_cast<int>(zoomOut.y + 3.0f), TypePx(11) , ToRay(p.ink));
  navHits_.push_back(NavHit{zoomIn, "zoom-in", ""});
  navHits_.push_back(NavHit{zoomOut, "zoom-out", ""});
  Text(chart.scaleText.c_str(), static_cast<int>(box.Right() - 260.0f),
           static_cast<int>(box.Bottom() - 18.0f), TypePx(9) , ToRay(p.muted));

  // --- panels: identity left, sensors right, the leg underneath the chart
  float ly = layout_.content.y + 58.0f;
  for (const NavPanel& panel : screen.left) {
    PaintPanel(panel, Area{left, ly, column, 96.0f});
    ly += 100.0f;
  }
  PaintPanel(screen.right[0],
             Area{right - column, layout_.content.y + 58.0f, column, 190.0f});

  const float halfW = (box.w - 6.0f) / 2.0f;
  float uy = box.Bottom() + 22.0f;
  for (std::size_t i = 0; i < screen.under.size(); ++i) {
    PaintPanel(screen.under[i],
               Area{box.x + static_cast<float>(i) * (halfW + 6.0f), uy, halfW,
                    112.0f});
  }

  // --- the commitment band, full width along the bottom
  const float bandY = layout_.content.Bottom() - 110.0f;
  DrawRectangle(static_cast<int>(left), static_cast<int>(bandY),
                static_cast<int>(right - left), 104, ToRay(p.panel));
  DrawRectangleLines(static_cast<int>(left), static_cast<int>(bandY),
                     static_cast<int>(right - left), 104, ToRay(p.border));
  Text("Target Actions", static_cast<int>(left + 8.0f),
           static_cast<int>(bandY + 4.0f), TypePx(10) , ToRay(p.coffee));
  float ty = bandY + 18.0f;
  Text(navScreen_.courseNote.c_str(), static_cast<int>(left + 8.0f),
           static_cast<int>(ty), TypePx(10) , ToRay(p.muted));
  ty += 14.0f;

  float px2 = left + 8.0f;
  for (const ProgressButton& button : screen.progress) {
    const Area buttonBox{px2, ty, 148.0f, 22.0f};
    DrawRectangle(static_cast<int>(buttonBox.x), static_cast<int>(buttonBox.y),
                  static_cast<int>(buttonBox.w), static_cast<int>(buttonBox.h),
                  ToRay(button.enabled ? p.panel2 : p.panel3));
    if (button.fillPercent > 0.0) {
      DrawRectangle(static_cast<int>(buttonBox.x), static_cast<int>(buttonBox.y),
                    static_cast<int>(buttonBox.w * button.fillPercent / 100.0),
                    static_cast<int>(buttonBox.h), ToRay(p.white));
    }
    DrawRectangleLines(static_cast<int>(buttonBox.x), static_cast<int>(buttonBox.y),
                       static_cast<int>(buttonBox.w),
                       static_cast<int>(buttonBox.h), ToRay(p.borderDark));
    Text(button.text.c_str(), static_cast<int>(buttonBox.x + 6.0f),
             static_cast<int>(buttonBox.y + 6.0f), TypePx(9) ,
             ToRay(button.state == ButtonState::Cooldown ? p.terracotta : p.ink));
    navHits_.push_back(NavHit{buttonBox, button.taskId, ""});
    px2 += 156.0f;
  }

  ty += 26.0f;
  for (const CommandRowView& row : screen.commands) {
    const Area rowBox{left + 8.0f, ty, 300.0f, 18.0f};
    Text(row.label.c_str(), static_cast<int>(rowBox.x + 34.0f),
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
    ty += 19.0f;
  }
  for (const std::string& note : navScreen_.notes) {
    if (ty > bandY + 96.0f) { break; }
    Text(note.c_str(), static_cast<int>(left + 8.0f), static_cast<int>(ty), TypePx(9) ,
             ToRay(p.muted));
    ty += 11.0f;
  }
}

void ShellView::PaintCrewRecord(const Simulation& sim) {
  navHits_.clear();
  const Palette& p = theme_.palette;
  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;

  Text(record_.head.kind.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y), TypePx(9) , ToRay(p.muted));
  Text(record_.head.title.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 12.0f), TypePx(20) , ToRay(p.ink));
  Text(record_.head.sub.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 36.0f), TypePx(11) , ToRay(p.muted));

  // Status across the top, then the three short shelves left to right.
  float y = layout_.content.y + 58.0f;
  if (!record_.shelves.empty()) {
    PaintPanel(record_.shelves[0], Area{left, y, right - left, 96.0f});
    y += 102.0f;
  }
  const float column = (right - left - 12.0f) / 3.0f;
  for (std::size_t i = 1; i < record_.shelves.size() && i <= 3; ++i) {
    PaintPanel(record_.shelves[i],
               Area{left + static_cast<float>(i - 1) * (column + 6.0f), y, column,
                    180.0f});
  }
  (void)sim;
}

void ShellView::PaintQuartersScreen(Simulation& sim) {
  navHits_.clear();
  const Palette& p = theme_.palette;
  const TaskScreenModel& screen = quarters_.screen;
  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;
  const float column = 210.0f;

  Text(screen.head.kind.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y), TypePx(9) , ToRay(p.muted));
  Text(screen.head.title.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 12.0f), TypePx(20) , ToRay(p.ink));
  Text(screen.head.sub.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 36.0f), TypePx(10) , ToRay(p.muted));

  // --- the roster, and the payroll above it
  const float panelTop = layout_.content.y + 58.0f;
  float ry = panelTop;
  if (!screen.left.empty()) {
    PaintPanel(screen.left[0], Area{left, ry, column, 170.0f});
    ry += 176.0f;
  }

  // --- the hallway: 560x300 of scenery, then the figure on top of it
  const Area hall{left + column + 10.0f, panelTop, 560.0f, 300.0f};
  DrawRectangle(static_cast<int>(hall.x), static_cast<int>(hall.y),
                static_cast<int>(hall.w), static_cast<int>(hall.h), ToRay(p.panel2));
  DrawRectangleLines(static_cast<int>(hall.x), static_cast<int>(hall.y),
                     static_cast<int>(hall.w), static_cast<int>(hall.h),
                     ToRay(p.borderDark));
  BeginScissorMode(static_cast<int>(hall.x) + 1, static_cast<int>(hall.y) + 1,
                   static_cast<int>(hall.w) - 2, static_cast<int>(hall.h) - 2);
  for (const MachinePart& part : quarters_.hall) {
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

  // --- the roster is clickable: looking at a hand is free
  float rowY = panelTop + 34.0f;
  for (const CrewRowView& row : quarters_.roster) {
    const Area box{left + 6.0f, rowY, column - 12.0f, 24.0f};
    if (row.selected) {
      DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                    static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.panel3));
    }
    Text(row.name.c_str(), static_cast<int>(box.x + 8.0f),
             static_cast<int>(box.y + 3.0f), TypePx(10) , ToRay(p.ink));
    Text(row.role.c_str(), static_cast<int>(box.x + 8.0f),
             static_cast<int>(box.y + 13.0f), TypePx(8) , ToRay(p.muted));
    if (!row.flag.empty()) {
      Text(row.flag.c_str(), static_cast<int>(box.Right() - 30.0f),
               static_cast<int>(box.y + 6.0f), TypePx(8) , ToRay(p.terracotta));
    }
    navHits_.push_back(NavHit{box, "select", row.id});
    rowY += 26.0f;
  }

  // --- condition and standing, under the hallway
  float uy = hall.Bottom() + 8.0f;
  for (const NavPanel& panel : screen.under) {
    PaintPanel(panel, Area{hall.x + (uy > hall.Bottom() + 8.0f ? 0.0f : 0.0f), uy,
                           270.0f, 120.0f});
    uy += 124.0f;
  }

  // --- orders and the sheet, down the right
  float oy = panelTop;
  for (const NavPanel& panel : screen.right) {
    const float height = panel.id == "cq-orders" ? 220.0f : 120.0f;
    PaintPanel(panel, Area{right - column, oy, column, height});
    if (panel.id == "cq-orders" && !quarters_.closed) {
      float lineY = oy + 96.0f;
      for (const OrderLineView& line : quarters_.orders) {
        const Area box{right - column + 6.0f, lineY, column - 12.0f, 18.0f};
        const std::string label = (line.on ? "[x] " : "[ ] ") + line.label;
        Text(label.c_str(), static_cast<int>(box.x),
                 static_cast<int>(box.y), TypePx(9) ,
                 ToRay(line.on ? p.ink : (line.locked ? p.muted : p.ink)));
        Text(line.cost.c_str(), static_cast<int>(box.Right() - 46.0f),
                 static_cast<int>(box.y), TypePx(8) , ToRay(p.coffee));
        navHits_.push_back(NavHit{box, "order", line.id});
        lineY += 19.0f;
    }
    }
    oy += height + 6.0f;
  }

  // --- the band: one button, one bar, one charge
  const float bandY = layout_.content.Bottom() - 78.0f;
  DrawRectangle(static_cast<int>(left), static_cast<int>(bandY),
                static_cast<int>(right - left), 72, ToRay(p.panel));
  DrawRectangleLines(static_cast<int>(left), static_cast<int>(bandY),
                     static_cast<int>(right - left), 72, ToRay(p.border));
  Text("Assignments", static_cast<int>(left + 8.0f),
           static_cast<int>(bandY + 4.0f), TypePx(9) , ToRay(p.coffee));
  Text(quarters_.bandNote.c_str(), static_cast<int>(left + 8.0f),
           static_cast<int>(bandY + 18.0f), TypePx(10) , ToRay(p.muted));
  float bx = left + 8.0f;
  for (const ProgressButton& button : screen.progress) {
    const Area box{bx, bandY + 36.0f, 220.0f, 26.0f};
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                  static_cast<int>(box.w), static_cast<int>(box.h),
                  ToRay(button.enabled ? p.panel2 : p.panel3));
    if (button.fillPercent > 0.0) {
      DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                    static_cast<int>(box.w * button.fillPercent / 100.0),
                    static_cast<int>(box.h), ToRay(p.white));
    }
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                       static_cast<int>(box.w), static_cast<int>(box.h),
                       ToRay(p.borderDark));
    Text(button.text.c_str(), static_cast<int>(box.x + 8.0f),
             static_cast<int>(box.y + 7.0f), TypePx(10) , ToRay(p.ink));
    navHits_.push_back(NavHit{box, "commit", ""});
    bx += 228.0f;
  }
  (void)sim;
}


// ============================================================================
//  Station and stores: free-standing columns of command rows. The layout is a
//  plain even split - the reference does the same - and every row is a hit box
//  whose id comes from the view-model, never from the label.
// ============================================================================
bool ShellView::PressPanelRow(Simulation& sim, ScreenKind kind, const std::string& id) {
  switch (kind) {
    case ScreenKind::Station: return PressStationAction(sim, id, "");
    case ScreenKind::Cargo:
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
void ShellView::ShowTitle(bool fromVoyage) {
  titleActive_ = true;
  fromVoyage_ = fromVoyage;
  titlePanel_ = TitlePanel::None;
  titleMessage_.clear();
  titleConfirm_ = false;
  titleHits_.clear();
  popupKey_.clear();
  ClosePopup();
}

void ShellView::PaintTitle(Simulation& sim) {
  const Palette& p = theme_.palette;
  titleHits_.clear();
  title_ = BuildTitleScreen(sim, fromVoyage_, titlePanel_, saveDir_, titleSlot_,
                            titleMessage_, titleCard_);

  DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_),
                ToRay(p.panel));

  // title.jpg is the complete menu card, not a hero image to decorate with a
  // second, invented menu. Fit the entire 1672x941 artwork without cropping.
  const float ratio = 1672.0f / 941.0f;
  float cardW = std::min(width_, 1672.0f);
  float cardH = cardW / ratio;
  if (cardH > height_) { cardH = height_; cardW = cardH * ratio; }
  const Area card{(width_ - cardW) * 0.5f, (height_ - cardH) * 0.5f, cardW, cardH};
  DrawRectangle(0, 0, static_cast<int>(width_), static_cast<int>(height_), ToRay(p.panel2));
  if (titleArt_ != nullptr) {
    const Texture2D* texture = static_cast<const Texture2D*>(titleArt_);
    DrawTexturePro(*texture,
                   Rectangle{0.0f, 0.0f, static_cast<float>(texture->width),
                             static_cast<float>(texture->height)},
                   Rectangle{card.x, card.y, card.w, card.h}, Vector2{0.0f, 0.0f}, 0.0f,
                   ToRay(p.white));
  }

  // The artwork itself contains the six menu labels and button treatments.
  // Their click regions are derived from the authored bottom menu strip.
  if (titlePanel_ == TitlePanel::None && !titleConfirm_) {
    const Area menu{card.x + card.w * 0.02f, card.y + card.h * 0.903f,
                    card.w * 0.818f, card.h * 0.0565f};
    const std::size_t count = title_.actions.size();
    if (count > 0) {
      const float cell = menu.w / static_cast<float>(count);
      for (std::size_t i = 0; i < count; ++i) {
        titleHits_.push_back(NavHit{Area{menu.x + cell * static_cast<float>(i), menu.y,
                                         cell, menu.h}, title_.actions[i].id, ""});
      }
    }
    if (!titleMessage_.empty()) {
      Text(titleMessage_.c_str(), static_cast<int>(card.x + 12.0f),
           static_cast<int>(card.Bottom() - TypeStep(24.0f)), TypePx(10), ToRay(p.coffee));
    }
    return;
  }

  // Secondary sheets are overlays on the real artwork; never redraw a mock
  // title, a substitute button column, or a cropped copy of the art.
  const float headY = card.y + 24.0f;
  Text(title_.heading.c_str(), static_cast<int>(card.x + 34.0f),
           static_cast<int>(headY), TypePx(40) , ToRay(p.ink));
  Text(title_.subtitle.c_str(), static_cast<int>(card.x + 36.0f),
           static_cast<int>(headY + 48.0f), TypePx(13) , ToRay(p.muted));

  // The five commands, in a column under the title.
  float y = headY + 84.0f;
  for (const ActionRowView& action : title_.actions) {
    const Area box{card.x + 36.0f, y, 260.0f, 30.0f};
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                  static_cast<int>(box.w), static_cast<int>(box.h),
                  ToRay(action.enabled ? p.white : p.panel3));
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                       static_cast<int>(box.w), static_cast<int>(box.h),
                       ToRay(action.enabled ? p.borderDark : p.border));
    Text(action.label.c_str(), static_cast<int>(box.x + 12.0f),
             static_cast<int>(box.y + 9.0f), TypePx(12) ,
             ToRay(action.enabled ? p.ink : p.muted));
    if (action.enabled) { titleHits_.push_back(NavHit{box, action.id, ""}); }
    y += 38.0f;
  }

  // The dialog: the three slots, the settings sheet or the guide.
  const Area dialog{card.x + 330.0f, card.y + card.h * 0.55f + 18.0f,
                    card.w - 330.0f - 36.0f, card.h * 0.45f - 54.0f};
  if (dialog.w > 40.0f && dialog.h > 40.0f) {
    DrawRectangle(static_cast<int>(dialog.x), static_cast<int>(dialog.y),
                  static_cast<int>(dialog.w), static_cast<int>(dialog.h), ToRay(p.panel));
    DrawRectangleLines(static_cast<int>(dialog.x), static_cast<int>(dialog.y),
                       static_cast<int>(dialog.w), static_cast<int>(dialog.h),
                       ToRay(p.border));
    float dy = dialog.y + 10.0f;
    if (!title_.panelTitle.empty()) {
      Text(title_.panelTitle.c_str(), static_cast<int>(dialog.x + 12.0f),
               static_cast<int>(dy), TypePx(16) , ToRay(p.ink));
      dy += 24.0f;
    }
    if (!title_.panelNote.empty()) {
      for (const std::string& line : WrapText(title_.panelNote, dialog.w - 24.0f, TypePx(10), bodyFont_)) {
        Text(line.c_str(), static_cast<int>(dialog.x + 12.0f),
                 static_cast<int>(dy), TypePx(10) , ToRay(p.muted));
        dy += 13.0f;
      }
      dy += 6.0f;
    }
    const auto paintRow = [&](const ActionRowView& row, const Area& box) {
      DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                    static_cast<int>(box.w), static_cast<int>(box.h),
                    ToRay(row.enabled ? p.panel2 : p.panel3));
      DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                         static_cast<int>(box.w), static_cast<int>(box.h),
                         ToRay(row.enabled ? p.borderDark : p.border));
      Text(row.label.c_str(), static_cast<int>(box.x + 8.0f),
           static_cast<int>(box.y + 6.0f), TypePx(10),
           ToRay(row.enabled ? p.ink : p.muted));
      if (row.enabled) { titleHits_.push_back(NavHit{box, row.id, ""}); }
    };
    if (titlePanel_ == TitlePanel::Settings) {
      const float gutter = 8.0f;
      const float columnW = (dialog.w - 36.0f - gutter) * 0.5f;
      for (std::size_t i = 0; i < title_.panelRows.size(); ++i) {
        const std::size_t column = i % 2;
        const std::size_t line = i / 2;
        const Area box{dialog.x + 12.0f + static_cast<float>(column) * (columnW + gutter),
                       dy + static_cast<float>(line) * 26.0f, columnW, 22.0f};
        if (box.Bottom() > dialog.Bottom() - 6.0f) { break; }
        paintRow(title_.panelRows[i], box);
      }
    } else {
      for (const ActionRowView& row : title_.panelRows) {
        if (dy + 24.0f > dialog.Bottom() - 8.0f) { break; }
        const Area box{dialog.x + 12.0f, dy, dialog.w - 24.0f, 22.0f};
        paintRow(row, box);
        dy += 26.0f;
      }
    }

    if (titleConfirm_) {
      const float buttonY = dialog.Bottom() - 30.0f;
      const Area confirm{dialog.x + 12.0f, buttonY, 190.0f, 22.0f};
      const Area cancel{confirm.Right() + 8.0f, buttonY, 170.0f, 22.0f};
      for (const auto& button : {std::make_pair(confirm, "CONFIRM NEW VOYAGE"),
                                 std::make_pair(cancel, "KEEP THIS VOYAGE")}) {
        DrawRectangle(static_cast<int>(button.first.x), static_cast<int>(button.first.y),
                      static_cast<int>(button.first.w), static_cast<int>(button.first.h),
                      ToRay(p.panel2));
        DrawRectangleLines(static_cast<int>(button.first.x), static_cast<int>(button.first.y),
                           static_cast<int>(button.first.w), static_cast<int>(button.first.h),
                           ToRay(p.borderDark));
        Text(button.second, static_cast<int>(button.first.x + 8.0f),
             static_cast<int>(button.first.y + 6.0f), TypePx(10), ToRay(p.ink));
      }
      titleHits_.push_back(NavHit{confirm, "confirm-new-voyage", ""});
      titleHits_.push_back(NavHit{cancel, "cancel-new-voyage", ""});
    }
  }

  if (!titleMessage_.empty()) {
    Text(titleMessage_.c_str(), static_cast<int>(card.x + 36.0f),
             static_cast<int>(card.Bottom() - 22.0f), TypePx(10) , ToRay(p.coffee));
  }
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
  for (const std::string& line : WrapText(loss_.cause, card.w - 40.0f, TypePx(12), bodyFont_)) {
    Text(line.c_str(), static_cast<int>(card.x + 20.0f), static_cast<int>(y), TypePx(12) ,
             ToRay(p.ink));
    y += 16.0f;
  }
  y += 4.0f;
  for (const std::string& line : WrapText(loss_.note, card.w - 40.0f, TypePx(10), bodyFont_)) {
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
  (void)viewport;
  if (panelsKind_ != ScreenKind::Wiki) { return; }
  const Area box{layout_.content.x + 8.0f, layout_.content.y + 50.0f, 260.0f, 22.0f};
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
  PaintWikiSearch(layout_.content);
  navHits_.clear();
  taskButtons_.clear();
  const Palette& p = theme_.palette;
  const TaskScreenModel& screen = taskScreen_;
  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;

  Text(screen.head.kind.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y), TypePx(9) , ToRay(p.muted));
  Text(screen.head.title.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 12.0f), TypePx(20) , ToRay(p.ink));
  Text(screen.head.sub.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 36.0f), TypePx(11) , ToRay(p.muted));

  float top = layout_.content.y + 56.0f;
  if (screen.hasHero) {
    PaintHero(screen.hero, Area{left, top, right - left, screen.hero.hero.h});
    top += screen.hero.hero.h + 8.0f;
  }
  const float bottom = layout_.content.Bottom() - 104.0f;
  const std::size_t count = screen.panels.empty() ? 1 : screen.panels.size();
  const float gap = 8.0f;
  const float columnW = (right - left - gap * static_cast<float>(count - 1)) /
                        static_cast<float>(count);

  for (std::size_t i = 0; i < screen.panels.size(); ++i) {
    const float x = left + static_cast<float>(i) * (columnW + gap);
    const NavPanel& panel = screen.panels[i];
    float noteH = 34.0f;
    for (const PanelLine& line : panel.lines) {
      noteH += 12.0f * static_cast<float>(1 + line.value.size() / 30);
    }
    PaintPanel(panel, Area{x, top, columnW, noteH});

    float y = top + noteH + 6.0f;
    if (i < screen.panelRows.size()) {
      for (std::size_t r = 0; r < screen.panelRows[i].size(); ++r) {
        if (y + 24.0f > bottom) { break; }
        const CommandRowView& row = screen.panelRows[i][r];
        const Area box{x, y, columnW, 22.0f};
        DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                      static_cast<int>(box.w), static_cast<int>(box.h),
                      ToRay(row.enabled ? p.panel2 : p.panel3));
        DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                           static_cast<int>(box.w), static_cast<int>(box.h),
                           ToRay(row.enabled ? p.borderDark : p.border));
        const std::string cost = row.displayCost.empty() ? std::string("free")
                                                         : row.displayCost;
        const float costW = static_cast<float>(TextWidth(cost.c_str(), TypePx(9) ));
        const std::string label = row.label;
        std::string shown = label;
        while (!shown.empty() &&
               static_cast<float>(TextWidth(shown.c_str(), TypePx(10) )) >
                   columnW - costW - 26.0f) {
          shown.pop_back();
        }
        Text(shown.c_str(), static_cast<int>(box.x + 6.0f),
                 static_cast<int>(box.y + 6.0f), TypePx(10) ,
                 ToRay(row.enabled ? p.ink : p.muted));
        Text(cost.c_str(), static_cast<int>(box.Right() - 6.0f - costW),
                 static_cast<int>(box.y + 6.0f), TypePx(9) ,
                 ToRay(row.enabled ? p.coffee : p.muted));
        if (row.enabled && i < screen.panelRowIds.size() &&
            r < screen.panelRowIds[i].size()) {
          navHits_.push_back(NavHit{box, "row", screen.panelRowIds[i][r]});
        }
        y += 25.0f;
      }
    }
  }

  // The action band: salvage and packaging buttons, and the clocks left.
  const float bandY = layout_.content.Bottom() - 96.0f;
  DrawRectangle(static_cast<int>(left), static_cast<int>(bandY),
                static_cast<int>(right - left), 90, ToRay(p.panel));
  DrawRectangleLines(static_cast<int>(left), static_cast<int>(bandY),
                     static_cast<int>(right - left), 90, ToRay(p.border));
  if (!pendingConfirm_.empty()) {
    Text(pendingConfirm_.c_str(), static_cast<int>(left + 8.0f),
             static_cast<int>(bandY + 24.0f), TypePx(10) , ToRay(p.terracotta));
    const struct {
      const char* action;
      const char* label;
    } answers[] = {{"confirm-yes", "Yes, do it"}, {"confirm-no", "Keep as it is"}};
    float ax = left + 12.0f;
    for (const auto& entry : answers) {
      const float aw = static_cast<float>(TextWidth(entry.label, TypePx(10) )) + 20.0f;
      const Area box{ax, bandY + 46.0f, aw, 24.0f};
      DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                         static_cast<int>(box.w), static_cast<int>(box.h),
                         ToRay(p.borderDark));
      Text(entry.label, static_cast<int>(box.x + 8.0f),
               static_cast<int>(box.y + 7.0f), TypePx(10) , ToRay(p.ink));
      navHits_.push_back(NavHit{box, entry.action, ""});
      ax += aw + 8.0f;
    }
  }

  Text("Work in progress", static_cast<int>(left + 8.0f),
           static_cast<int>(bandY + 5.0f), TypePx(11) , ToRay(p.coffee));
  Text(screen.bandNote.c_str(), static_cast<int>(left + 8.0f),
           static_cast<int>(bandY + 20.0f), TypePx(10) , ToRay(p.muted));
  float bx = left + 12.0f;
  for (const ProgressButton& button : screen.progress) {
    const Area box{bx, bandY + 50.0f, 190.0f, 28.0f};
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                  static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.panel2));
    if (button.fillPercent > 0.0) {
      DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                    static_cast<int>(box.w * button.fillPercent / 100.0),
                    static_cast<int>(box.h), ToRay(p.white));
    }
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                       static_cast<int>(box.w), static_cast<int>(box.h),
                       ToRay(p.borderDark));
    Text(button.text.c_str(), static_cast<int>(box.x + 8.0f),
             static_cast<int>(box.y + 8.0f), TypePx(10) ,
             ToRay(button.state == ButtonState::Cooldown ? p.terracotta : p.ink));
    taskButtons_.push_back({box, button.taskId});
    bx += 198.0f;
  }
  (void)sim;
}

// ============================================================================
//  The floating popups (Section K). They never replace the window: a veil and
//  a card float over whatever is behind, and every control inside is free.
//  Successive popups step down and right by (count % 5) * 22, as in the
//  reference, so the reader can see one behind another.
// ============================================================================
void ShellView::PaintPopup(Simulation& sim) {
  popupHits_.clear();
  const Palette& p = theme_.palette;
  popup_ = BuildPopup(sim, popupKey_, saveDir_);
  if (!popupMessage_.empty()) { popup_.message = popupMessage_; }

  const float off = static_cast<float>(popupCount_ % 5) * 22.0f;
  const float w = std::min(520.0f, width_ - 60.0f);
  const float h = std::min(430.0f, height_ - 100.0f);
  const float x = 300.0f + off;
  const float y = 90.0f + off;
  const Area card{x, y, w, h};

  DrawRectangle(static_cast<int>(card.x + 6.0f), static_cast<int>(card.y + 6.0f),
                static_cast<int>(card.w), static_cast<int>(card.h), ToRay(p.border));
  DrawRectangle(static_cast<int>(card.x), static_cast<int>(card.y),
                static_cast<int>(card.w), static_cast<int>(card.h), ToRay(p.panel));
  DrawRectangleLines(static_cast<int>(card.x), static_cast<int>(card.y),
                     static_cast<int>(card.w), static_cast<int>(card.h),
                     ToRay(p.borderDark));
  DrawRectangle(static_cast<int>(card.x), static_cast<int>(card.y),
                static_cast<int>(card.w), 24, ToRay(p.panel2));
  Text(popup_.title.c_str(), static_cast<int>(card.x + 8.0f),
           static_cast<int>(card.y + 6.0f), TypePx(12) , ToRay(p.ink));
  const Area closeBox{card.Right() - 26.0f, card.y + 3.0f, 20.0f, 18.0f};
  Text("x", static_cast<int>(closeBox.x + 7.0f),
           static_cast<int>(closeBox.y + 3.0f), TypePx(12) , ToRay(p.muted));
  popupHits_.push_back(NavHit{closeBox, "close", ""});

  float cy = card.y + 30.0f;
  const float pad = 10.0f;
  if (!popup_.notice.empty()) {
    Text(popup_.notice.c_str(), static_cast<int>(card.x + pad),
             static_cast<int>(cy), TypePx(9) , ToRay(p.muted));
    cy += 12.0f * static_cast<float>(1 + popup_.notice.size() / 74);
  }

  for (const PopupSection& section : popup_.sections) {
    Text(section.title.c_str(), static_cast<int>(card.x + pad),
             static_cast<int>(cy), TypePx(10) , ToRay(p.coffee));
    cy += 14.0f;
    for (const PopupRow& row : section.rows) {
      if (cy + 14.0f > card.Bottom() - 8.0f) { break; }
      Text(row.key.c_str(), static_cast<int>(card.x + pad),
               static_cast<int>(cy), TypePx(10) , ToRay(p.ink));
      const float valueW = static_cast<float>(TextWidth(row.value.c_str(), TypePx(9) ));
      Text(row.value.c_str(),
               static_cast<int>(card.Right() - pad - valueW),
               static_cast<int>(cy + 1.0f), TypePx(9) , ToRay(p.muted));
      cy += 13.0f;
    }
    cy += 6.0f;
  }

  int shownLines = 0;
  for (const std::string& line : popup_.lines) {
    if (cy + 13.0f > card.Bottom() - 8.0f) { break; }
    std::string text = line;
    if (text.size() > 84) { text = text.substr(0, 84) + "..."; }
    Text(text.c_str(), static_cast<int>(card.x + pad), static_cast<int>(cy), TypePx(9) ,
             ToRay(p.ink));
    cy += 12.0f;
    ++shownLines;
  }
  if (shownLines > 0 && shownLines < static_cast<int>(popup_.lines.size())) {
    const std::string more = "Older entries are folded away.";
    Text(more.c_str(), static_cast<int>(card.x + pad), static_cast<int>(cy + 2.0f), TypePx(9) , ToRay(p.muted));
  }

  for (const PopupToggle& toggle : popup_.toggles) {
    const Area box{card.x + pad, cy, card.w - pad * 2.0f, TypeStep(22.0f)};
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                  static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.panel2));
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                       static_cast<int>(box.w), static_cast<int>(box.h),
                       ToRay(p.borderDark));
    Text(toggle.label.c_str(), static_cast<int>(box.x + 6.0f),
             static_cast<int>(box.y + 6.0f), TypePx(10) , ToRay(p.ink));
    popupHits_.push_back(NavHit{box, "toggle", toggle.id});
    cy += TypeStep(24.0f);
  }

  if (!popup_.themes.empty()) {
    float tx = card.x + pad;
    for (std::size_t i = 0; i < popup_.themes.size(); ++i) {
      const std::string& key = popup_.themes[i];
      const bool active = key == popup_.theme;
      const float tw = static_cast<float>(
          TextWidth(popup_.themeLabels[i].c_str(), TypePx(9) )) + 16.0f;
      const Area box{tx, cy, tw, 20.0f};
      DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                    static_cast<int>(box.w), static_cast<int>(box.h),
                    ToRay(active ? p.white : p.panel3));
      DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                         static_cast<int>(box.w), static_cast<int>(box.h),
                         ToRay(active ? p.coffee : p.border));
      Text(popup_.themeLabels[i].c_str(), static_cast<int>(box.x + 8.0f),
               static_cast<int>(box.y + 5.0f), TypePx(9) , ToRay(p.ink));
      popupHits_.push_back(NavHit{box, "theme", key});
      tx += tw + 6.0f;
    }
    cy += TypeStep(26.0f);
  }

  // ---- the reading-size ladder and the window (Phase 9) ------------------
  if (!popup_.fontStepLabels.empty()) {
    Text(popup_.fontStepLine.c_str(), static_cast<int>(card.x + pad),
             static_cast<int>(cy + 2.0f), TypePx(10), ToRay(p.muted));
    float fx = card.x + pad;
    for (std::size_t i = 0; i < popup_.fontStepLabels.size(); ++i) {
      const bool active = static_cast<int>(i) == popup_.fontStep;
      const float fw = static_cast<float>(TextWidth(popup_.fontStepLabels[i].c_str(),
                                                      TypePx(10))) +
                       18.0f;
      const Area box{fx, cy + TypeStep(16.0f), fw, TypeStep(22.0f)};
      DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                    static_cast<int>(box.w), static_cast<int>(box.h),
                    ToRay(active ? p.white : p.panel3));
      DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                         static_cast<int>(box.w), static_cast<int>(box.h),
                         ToRay(active ? p.coffee : p.border));
      Text(popup_.fontStepLabels[i].c_str(), static_cast<int>(box.x + 9.0f),
               static_cast<int>(box.y + 5.0f), TypePx(10), ToRay(p.ink));
      popupHits_.push_back(NavHit{box, "font", std::to_string(i)});
      fx += fw + 6.0f;
    }
    cy += TypeStep(46.0f);
    const Area row{card.x + pad, cy, card.w - pad * 2.0f, TypeStep(22.0f)};
    DrawRectangle(static_cast<int>(row.x), static_cast<int>(row.y),
                  static_cast<int>(row.w), static_cast<int>(row.h),
                  ToRay(popup_.fullscreen ? p.panel3 : p.panel2));
    DrawRectangleLines(static_cast<int>(row.x), static_cast<int>(row.y),
                       static_cast<int>(row.w), static_cast<int>(row.h),
                       ToRay(p.borderDark));
    Text(popup_.fullscreenLabel.c_str(), static_cast<int>(row.x + 6.0f),
             static_cast<int>(row.y + 5.0f), TypePx(10), ToRay(p.ink));
    popupHits_.push_back(NavHit{row, "fullscreen", ""});
    cy += TypeStep(26.0f);
  }

  for (const PopupSlot& slot : popup_.slots) {
    const bool active = slot.slot == popupSlot_;
    const Area box{card.x + pad, cy, card.w - pad * 2.0f, 30.0f};
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                  static_cast<int>(box.w), static_cast<int>(box.h),
                  ToRay(active ? p.white : p.panel2));
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                       static_cast<int>(box.w), static_cast<int>(box.h),
                       ToRay(active ? p.coffee : p.border));
    Text(slot.label.c_str(), static_cast<int>(box.x + 6.0f),
             static_cast<int>(box.y + 4.0f), TypePx(10) , ToRay(p.ink));
    Text(slot.summary.c_str(), static_cast<int>(box.x + 6.0f),
             static_cast<int>(box.y + 17.0f), TypePx(9) , ToRay(p.muted));
    popupHits_.push_back(
        NavHit{box, "slot", std::to_string(slot.slot)});
    cy += TypeStep(33.0f);
  }

  if (!popup_.slots.empty()) {
    const struct {
      const char* action;
      const char* label;
    } buttons[] = {{"save", "Save voyage"},
                   {"load", "Load voyage"},
                   {"new-voyage", "Start a new voyage"},
                   {"main-menu", "Return to main menu"}};
    float bx = card.x + pad;
    for (const auto& entry : buttons) {
      const float bw = static_cast<float>(TextWidth(entry.label, TypePx(10) )) + 22.0f;
      const Area box{bx, cy, bw, 24.0f};
      DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                    static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.panel2));
      DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                         static_cast<int>(box.w), static_cast<int>(box.h),
                         ToRay(p.borderDark));
      Text(entry.label, static_cast<int>(box.x + 8.0f),
               static_cast<int>(box.y + 7.0f), TypePx(10) , ToRay(p.ink));
      popupHits_.push_back(NavHit{box, entry.action, std::to_string(popupSlot_)});
      bx += bw + 6.0f;
    }
    cy += 30.0f;
  }

  if (confirmNewVoyage_) {
    Text("Start a new voyage? Unsaved progress will be lost; saved slots are kept.",
             static_cast<int>(card.x + pad), static_cast<int>(cy), TypePx(9) ,
             ToRay(p.terracotta));
    cy += 14.0f;
    const struct {
      const char* action;
      const char* label;
    } answers[] = {{"new-voyage-yes", "Begin again"}, {"new-voyage-no", "Keep this voyage"}};
    float bx = card.x + pad;
    for (const auto& entry : answers) {
      const float bw = static_cast<float>(TextWidth(entry.label, TypePx(10) )) + 22.0f;
      const Area box{bx, cy, bw, 22.0f};
      DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                         static_cast<int>(box.w), static_cast<int>(box.h),
                         ToRay(p.borderDark));
      Text(entry.label, static_cast<int>(box.x + 8.0f),
               static_cast<int>(box.y + 6.0f), TypePx(10) , ToRay(p.ink));
      popupHits_.push_back(NavHit{box, entry.action, ""});
      bx += bw + 6.0f;
    }
    cy += 24.0f;
  }

  if (!popup_.message.empty()) {
    Text(popup_.message.c_str(), static_cast<int>(card.x + pad),
             static_cast<int>(card.Bottom() - 16.0f), TypePx(9) , ToRay(p.coffee));
  }
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
    DrawRectangle(x, y, w, h, ToRay(theme_.palette.panel2));
    DrawRectangleLines(x, y, w, h, ToRay(theme_.palette.borderDark));
    DrawRectangle(x, y, 3, h, ToRay(theme_.palette.coffee));
    if (!diagram.compact) {
      Text(room.name.c_str(), x + 6, y + 4, TypePx(10) , ToRay(theme_.palette.ink));
      if (!room.small) {
        Text(room.tag.c_str(), x + w - 30, y + h - 14, TypePx(8) ,
                 ToRay(theme_.palette.muted));
      }
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
                  15, 15, ToRay(theme_.palette.coffeeDark));
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

void ShellView::Drag(Simulation& sim, float x, float y, float dx, float dy, bool down) {
  Move(x, y);
  if (!screenNav_ || !navChartBox_.Contains(x, y)) { draggingChart_ = false; return; }
  if (!down) { draggingChart_ = false; return; }
  if (!draggingChart_) {
    chartDragAccumX_ = navScreen_.chart.panX;
    chartDragAccumY_ = navScreen_.chart.panY;
  } else {
    chartDragAccumX_ += dx;
    chartDragAccumY_ += dy;
    char pan[64];
    std::snprintf(pan, sizeof(pan), "%.2f,%.2f", chartDragAccumX_, chartDragAccumY_);
    PressNavAction(sim, "pan", pan);
  }
  draggingChart_ = true;
}

void ShellView::Scroll(float x, float y, float wheelDelta) {
  Move(x, y);
  const float step = wheelDelta * TypeStep(40.0f);
  if (!popupKey_.empty()) { popupScroll_ = std::max(0.0f, std::min(popupMaxScroll_, popupScroll_ - step)); }
  else if (titleActive_) { titleScroll_ = std::max(0.0f, std::min(titleMaxScroll_, titleScroll_ - step)); }
  else if (wikiIndexBox_.Contains(x, y)) { wikiIndexScroll_ = std::max(0.0f, std::min(wikiIndexMaxScroll_, wikiIndexScroll_ - step)); }
  else if (wikiBodyBox_.Contains(x, y)) { wikiBodyScroll_ = std::max(0.0f, std::min(wikiBodyMaxScroll_, wikiBodyScroll_ - step)); }
  else if (layout_.sidepanel.Contains(x, y)) { sideScroll_ = std::max(0.0f, std::min(std::max(0.0f, sideContentH_ - layout_.sidepanel.h), sideScroll_ - step)); }
  else { contentScroll_ = std::max(0.0f, std::min(contentMaxScroll_, contentScroll_ - step)); }
}

void ShellView::Escape(Simulation& sim) {
  if (LossVisible(sim)) { lossDismissed_ = true; return; }
  if (titleActive_) { if (titleConfirm_) { titleConfirm_ = false; } else { HideTitle(); } return; }
  if (!popupKey_.empty()) { ClosePopup(); return; }
  if (!pendingConfirm_.empty()) { pendingConfirm_.clear(); pendingAction_.clear(); return; }
  if (hasSelection_) { hasSelection_ = false; return; }
  ShowTitle(true);
}

void ShellView::Click(Simulation& sim, float x, float y, HitButton button,
                      double nowMs) {
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
        const int count = static_cast<int>(TutorialCards().size());
        const int step = hit.action == "card:next" ? 1 : -1;
        const int card = titleCard_ + step;
        if (card >= 0 && card < count) { titleCard_ = card; }
        return;
      }
      if (hit.action == "resume") {
        if (!fromVoyage_) { quitRequested_ = true; return; }
        PressTitleAction(sim, "resume", "", &fromVoyage_, &titlePanel_, &titleSlot_,
                         saveDir_, &titleMessage_, &titleActive_);
        HideTitle();
        return;
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

  // A floating popup covers the window: it is the topmost thing, and it is
  // free. Closing it puts the reader back exactly where they were.
  if (!popupKey_.empty()) {
    for (const NavHit& hit : popupHits_) {
      if (!hit.box.Contains(x, y)) { continue; }
      if (hit.action == "close") { ClosePopup(); return; }
      if (hit.action == "slot") { popupSlot_ = std::stoi(hit.arg); return; }
      if (hit.action == "new-voyage") { confirmNewVoyage_ = true; return; }
      if (hit.action == "main-menu") {
        ClosePopup();
        ShowTitle(true);
        return;
      }
      if (hit.action == "new-voyage-no") {
        confirmNewVoyage_ = false;
        popupMessage_.clear();
        return;
      }
      if (hit.action == "new-voyage-yes") {
        confirmNewVoyage_ = false;
        std::string message;
        PressPopupAction(sim, popupKey_, "new-voyage", hit.arg, saveDir_, &message);
        popupMessage_ = message;
        popupSlot_ = 1;
        return;
      }
      std::string message;
      const bool changed = PressPopupAction(sim, popupKey_, hit.action, hit.arg,
                                            saveDir_, &message);
      popupMessage_ = message;
      (void)changed;
      // A preference takes effect on the next paint: the app re-reads the
      // options and reconfigures the theme every frame.
      return;
    }
    return;  // clicks outside a popup card do not reach the ship
  }

  // A briefing is modal: ImGui owns its Open Window and Close buttons, and
  // clicks anywhere else must not leak through to the screen underneath.
  if (hasSelection_) { return; }

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

  // The quarters: a roster row, a job, an order, the sheet, a word.
  if (screenQuarters_) {
    for (const NavHit& hit : navHits_) {
      if (!hit.box.Contains(x, y)) { continue; }
      if (PressQuartersAction(sim, hit.action, hit.arg) && hit.action == "commit") {
        SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
      }
      return;
    }
    return;
  }
  if (screenRecord_) { return; }

  // Station and stores: each command row carries its own action id.
  if (screenPanels_) {
    for (const NavHit& hit : navHits_) {
      if (!hit.box.Contains(x, y)) { continue; }
      if (hit.action == "confirm-no") { pendingConfirm_.clear(); pendingAction_.clear(); return; }
      if (hit.action == "confirm-yes") {
        const std::string action = pendingAction_;
        pendingConfirm_.clear();
        pendingAction_.clear();
        const bool acted =
            panelsKind_ == ScreenKind::Station
                ? PressStationAction(sim, action, "")
                : PressStoresAction(sim, action, "");
        if (acted) { SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion); }
        return;
      }
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
      if (!question.empty() && sim.State().options.confirmRisk) {
        pendingConfirm_ = question;
        pendingAction_ = hit.arg;
        return;
      }
      const bool acted = PressPanelRow(sim, panelsKind_, hit.arg);
      if (acted) { SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion); }
      return;
    }
  }

  // The chart's own controls: bodies, the zoom pair, the layers, the actions.
  if (screenNav_) {
    for (const NavHit& hit : navHits_) {
      if (!hit.box.Contains(x, y)) { continue; }
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
    if (!entry.first.Contains(x, y)) { continue; }
    if (PressProgressButton(sim, entry.second)) {
      SpawnMark(marks_, x, y, nowMs, sim.State().reducedMotion);
      hasSelection_ = false;
    }
    return;
  }

  if (layout_.home.Contains(x, y)) {
    SwitchTab(sim, "home");
    return;
  }

  if (sim.State().activeTab == "home") {
    // The stage is the only clickable illustration on Home.
    if (idle_.stageArea.Contains(x, y)) {
      const float localX = x - idle_.stageArea.x;
      const float localY = y - idle_.stageArea.y;
      for (const DiagramCrew& crew : idle_.diagram.crew) {
        const Area hit{crew.dot.x - 6.0f, crew.dot.y - 6.0f, 27.0f, 27.0f};
        const Area labelHit{crew.label.x, crew.label.y,
                            crew.labelled ? TextWidth(crew.firstName.c_str(), TypePx(9)) : 0.0f,
                            crew.labelled ? TypeStep(12.0f) : 0.0f};
        if (!hit.Contains(localX, localY) && !labelHit.Contains(localX, localY)) { continue; }
        if (button == HitButton::Right) {
          OpenTask(sim, "crew", crew.id, false);
        } else {
          selection_ = DescribeTarget(sim, "crew", crew.id);
          briefingKind_ = "crew";
          briefingId_ = crew.id;
          hasSelection_ = true;
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
          selection_ = DescribeTarget(sim, "room", room.id);
          briefingKind_ = "room";
          briefingId_ = room.id;
          hasSelection_ = true;
          SpawnMark(marks_, x - layout_.content.x, y, nowMs,
                    sim.State().reducedMotion);
        }
        return;
      }
    }
    for (const RailCell& cell : idle_.stats) {
      if (!cell.box.Contains(x, y)) { continue; }
      if (button == HitButton::Right) {
        OpenTask(sim, "system", cell.id, false);
      } else {
        selection_ = DescribeTarget(sim, "system", cell.id);
        briefingKind_ = "system";
        briefingId_ = cell.id;
        hasSelection_ = true;
      }
      return;
    }
    for (const RailCell& cell : idle_.infos) {
      if (!cell.box.Contains(x, y)) { continue; }
      if (button == HitButton::Right) {
        if (cell.id == "life") {
          OpenTask(sim, "room", "o2", false);
        } else {
          OpenTask(sim, "info", cell.id, false);
        }
      } else {
        selection_ = DescribeTarget(sim, "info", cell.id);
        briefingKind_ = "info";
        briefingId_ = cell.id;
        hasSelection_ = true;
      }
      return;
    }
    if (idle_.nav.box.Contains(x, y)) {
      if (button == HitButton::Right) {
        OpenTask(sim, "nav", "main", false);
      } else {
        selection_ = DescribeTarget(sim, "nav", "main");
        briefingKind_ = "nav";
        briefingId_ = "main";
        hasSelection_ = true;
      }
      return;
    }
    if (idle_.docked && idle_.dock.box.Contains(x, y)) {
      if (button == HitButton::Right) {
        OpenTask(sim, "station", sim.Map().here, false);
      } else {
        selection_ = DescribeTarget(sim, "station", sim.Map().here);
        briefingKind_ = "station";
        briefingId_ = sim.Map().here;
        hasSelection_ = true;
      }
      return;
    }
  }
}

}  // namespace ship
