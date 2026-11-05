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
#include <cstdlib>

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

::Color HexColor(const std::string& value, ::Color fallback) {
  if (value.size() != 7 || value[0] != '#') { return fallback; }
  char* end = nullptr;
  const unsigned long rgb = std::strtoul(value.c_str() + 1, &end, 16);
  if (end != value.c_str() + value.size()) { return fallback; }
  return {static_cast<unsigned char>(rgb >> 16), static_cast<unsigned char>(rgb >> 8),
          static_cast<unsigned char>(rgb), 255};
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

int ShellView::TypePx(int reference) const {
  return reference <= 0
             ? reference
             : std::max(18, static_cast<int>(
                                std::lround(reference * (theme_.FontPx() / 13.0))));
}

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
  const float contentH = height_ - Metrics::kContentTop - Metrics::kStatusH;
  layout_ = ComputeLayout(width_, height_, contentH, others_, theme_.stageCompact);

  if (scrollTabKey_ != sim.State().activeTab) {
    scrollTabKey_ = sim.State().activeTab;
    ResetContentScroll();
    draggingChart_ = false;
  }

  // Always build Home, including when reduced motion is enabled. Keep drift
  // state across frames and advance it at the reference cadence; rebuilding
  // random dots every frame caused teleporting and a blank reduced-motion view.
  if (sim.State().activeTab == "home") {
    FxRng seeded = rng_;
    idle_ = BuildIdleScreen(sim, layout_, theme_, theme_.hideCrewLabels, others_, seeded);
    rng_ = seeded;
    SyncIdleDots(sim, nowMs);
    // The whole home column scrolls when it is taller than the content area
    // (the HTA's #content { overflow:auto }). The ceiling is the feed's bottom
    // edge; anything past the content bottom is reachable only by scrolling.
    contentMaxScroll_ =
        std::max(0.0f, idle_.feedBox.Bottom() - layout_.content.Bottom());
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

  // Main-content scroll: the whole content column moves with contentScroll_,
  // matching the HTA's #content { overflow:auto }. Every screen paints inside
  // this transform, and Click() offsets the cursor by the same amount so
  // obscured content can never be clicked.
  Camera2D scrollCam{{0.0f, 0.0f}, {0.0f, -contentScroll_}, 0.0f, 1.0f};
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
    const ScreenKind kind = tab == nullptr ? ScreenKind::Described : ScreenKindFor(*tab);
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
  EndMode2D();
  paintingContent_ = false;
  contentMaxScroll_ = std::max(0.0f, contentPaintBottom_ - layout_.content.Bottom());
  contentScroll_ = std::min(contentScroll_, contentMaxScroll_);

  PaintChrome(sim);
  if (!LossVisible(sim)) { PaintPopup(sim); }
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
                       sideScroll_, &sideContentH_, [&](const std::string& text, float width) {
                         const float h = ImGui::CalcTextSize(text.c_str(), nullptr, false, width).y;
                         return std::max(1.0f, std::ceil(h / ImGui::GetFontSize())) * theme_.LineHeight();
                       });
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
        draw->AddText(ImVec2(box.Right() - 10, box.y), ToIm(p.muted), open ? "v" : ">");
      } else {
        for (const auto& line : PlaceIndicators(IndicatorLines(sim))) {
          const float base = box.Bottom() + 4;
          draw->AddRectFilled(ImVec2(box.x + line.ghost.x, base + line.ghost.y),
                              ImVec2(box.x + line.ghost.x + 2, base + line.ghost.Bottom()), ToIm(p.panel3));
          draw->AddRectFilled(ImVec2(box.x + line.line.x, base + line.line.y),
                              ImVec2(box.x + line.line.x + 2, base + line.line.Bottom()),
                              ToIm(line.grow ? p.terracotta : p.sage));
        }
      }
      continue;
    }
    if (hit.action == SideAction::OpenEvent) {
      wrapped(hit.text, box.y, box.w - 60, ToIm(p.ink));
      const Area button{box.Right() - 52, box.y, 52, theme_.LineHeight() + 4};
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
                      ImVec2(layout_.sidefoot.Right(), layout_.sidefoot.Bottom()), ToIm(p.panel));
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

  // ---- status bar --------------------------------------------------------
  draw->AddRectFilled(ImVec2(0.0f, layout_.statusbar.y),
                      ImVec2(width_, layout_.statusbar.Bottom()), ToIm(p.panel2));
  draw->AddLine(ImVec2(0.0f, layout_.statusbar.y), ImVec2(width_, layout_.statusbar.y),
                ToIm(p.border));
  draw->AddText(ImVec2(8.0f, layout_.statusbar.y + 4.0f), ToIm(p.ink),
                StatusText(sim).c_str());

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
    // The label sits at the top of the cell; the value starts one label height
    // plus a gap below it, so the two never overlap at any typography step.
    // The unit (e.g. "%", " air", " ration/action") is painted to the right of
    // the value when it fits, matching the reference's inline stat-unit.
    const float labelY = cell.box.y + 6.0f;
    const float valueY =
        cell.box.y + 6.0f + TypePx(11) + 6.0f;
    Text(cell.label.c_str(), static_cast<int>(cell.box.x + 6.0f),
             static_cast<int>(labelY), TypePx(11) , ToRay(theme.palette.muted));
    Text(cell.value.c_str(), static_cast<int>(cell.box.x + 6.0f),
         static_cast<int>(valueY), TypePx(20), ToRay(theme.palette.ink), true);
    if (!cell.unit.empty()) {
      const float valueW = TextWidth(cell.value.c_str(), TypePx(20), true);
      const float unitW = TextWidth(cell.unit.c_str(), TypePx(10), false);
      const float unitX = cell.box.x + 6.0f + valueW + 4.0f;
      if (unitX + unitW <= cell.box.Right() - 6.0f) {
        Text(cell.unit.c_str(), static_cast<int>(unitX), static_cast<int>(valueY),
             TypePx(10), ToRay(theme.palette.muted));
      } else {
        Text(cell.unit.c_str(), static_cast<int>(cell.box.x + 6.0f),
             static_cast<int>(valueY + TypePx(20) + 4.0f), TypePx(10), ToRay(theme.palette.muted));
      }
    }
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
    const float mainY =
        strip.box.y + 6.0f + TypePx(11) + 6.0f;
    const float rightY = mainY + TypePx(13) + 6.0f;
    Text(strip.label.c_str(), static_cast<int>(strip.box.x + 8.0f),
             static_cast<int>(strip.box.y + 6.0f), TypePx(11) , ToRay(theme.palette.muted));
    Text(strip.main.c_str(), static_cast<int>(strip.box.x + 8.0f),
         static_cast<int>(mainY), TypePx(13) , ToRay(theme.palette.ink));
    Text(strip.right.c_str(), static_cast<int>(strip.box.x + 8.0f),
         static_cast<int>(rightY), TypePx(10), ToRay(theme.palette.muted), true);
  };
  paintStrip(idle_.nav);
  if (idle_.docked) { paintStrip(idle_.dock); }

  for (const RailCell& cell : idle_.infos) {
    const float valueY = cell.box.y + 6.0f + TypePx(10) + 6.0f;
    Text(cell.label.c_str(), static_cast<int>(cell.box.x),
         static_cast<int>(cell.box.y + 6.0f), TypePx(10) , ToRay(theme.palette.muted));
    Text(cell.value.c_str(), static_cast<int>(cell.box.x),
         static_cast<int>(valueY), TypePx(13), ToRay(theme.palette.ink), true);
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

  const float headerH = PaintTaskHeader(screen.head, left, layout_.content.y);

  float y = layout_.content.y + headerH + 8.0f;
  for (const NavPanel& panel : screen.left) {
    const float panelH = std::max(118.0f, MeasurePanelHeight(panel, column));
    PaintPanel(panel, Area{left, y, column, panelH});
    y += panelH + 6.0f;
  }

  float ry = layout_.content.y + headerH + 8.0f;
  for (const NavPanel& panel : screen.right) {
    const float panelH = std::max(150.0f, MeasurePanelHeight(panel, column));
    PaintPanel(panel, Area{right - column, ry, column, panelH});
    ry += panelH + 6.0f;
  }

  float stageBottom = layout_.content.y + headerH;
  if (screen.hasStage) {
    const Area origin{left + column + 16.0f, layout_.content.y + headerH + 8.0f,
                      screen.stage.width, screen.stage.height};
    PaintStage(screen.stage, origin);
    const float uy = origin.y + screen.stage.height + 4.0f;
    const float halfW = (right - column - 16.0f - (left + column + 16.0f) - 4.0f) / 2.0f;
    float underH = 110.0f;
    for (const NavPanel& panel : screen.under) {
      underH = std::max(underH, MeasurePanelHeight(panel, halfW));
    }
    stageBottom = uy + underH;
    for (std::size_t i = 0; i < screen.under.size(); ++i) {
      PaintPanel(screen.under[i],
                 Area{origin.x + static_cast<float>(i) * (halfW + 4.0f), uy, halfW,
                      underH});
    }
  }

  // The commitments band, along the bottom of the content column.
  const float bandY = std::max({layout_.content.Bottom() - 108.0f, y + 8.0f, ry + 8.0f, stageBottom + 8.0f});
  DrawRectangle(static_cast<int>(left), static_cast<int>(bandY),
                static_cast<int>(right - left), 102, ToRay(p.panel));
  DrawRectangleLines(static_cast<int>(left), static_cast<int>(bandY),
                     static_cast<int>(right - left), 102, ToRay(p.border));
  Text("Actions", static_cast<int>(left + 8.0f),
           static_cast<int>(bandY + 5.0f), TypePx(11), ToRay(p.coffee));
  float noteY = bandY + TypeStep(32.0f);
  for (const auto& line : WrapText(screen.bandNote, right - left - 16.0f, TypePx(10), SelectFont(TypePx(10), false))) {
    Text(line.c_str(), static_cast<int>(left + 8.0f), static_cast<int>(noteY), TypePx(10), ToRay(p.muted));
    noteY += theme_.LineHeight();
  }

  float bx = left + 12.0f;
  const float progressH = theme_.LineHeight() + 6.0f;
  const float progressY = noteY + 8.0f;
  for (const ProgressButton& button : screen.progress) {
    const float buttonW = std::max(190.0f, TextWidth(button.text.c_str(), TypePx(10)) + 16.0f);
    const Area box{bx, progressY, buttonW, progressH};
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
             static_cast<int>(box.y + (box.h - TypePx(10)) * 0.5f), TypePx(10) ,
             ToRay(button.state == ButtonState::Cooldown ? p.terracotta : p.ink));
    taskButtons_.push_back({box, button.taskId});
    bx += buttonW + 8.0f;
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
  const float availableH = layout_.content.Bottom() - chartTop - 300.0f;
  const float chartH = std::min({500.0f, chartW * 0.5f,
                                 std::max(240.0f, availableH)});
  const Area box{middleLeft, chartTop, chartW, chartH};
  navChartBox_ = box;
  const ship::NavRaster raster = BuildNavRaster(sim);
  const float sx = box.w / static_cast<float>(raster.width);
  const float sy = box.h / static_cast<float>(raster.height);
  navChartScaleX_ = sx;
  navChartScaleY_ = sy;
  const float bodyScale = std::min(sx, sy);

  DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.panel));
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

  for (const NavDot& dot : chart.clusters) {
    const float px = box.x + static_cast<float>(dot.x) * sx;
    const float py = box.y + static_cast<float>(dot.y) * sy;
    DrawRectangle(static_cast<int>(px), static_cast<int>(py), 1, 1,
                  ToRay(dot.cls.find("bright") != std::string::npos ? p.coffee
                                                                  : p.muted));
  }
  for (const NavDot& dot : chart.routes) {
    const float px = box.x + static_cast<float>(dot.x) * sx;
    const float py = box.y + static_cast<float>(dot.y) * sy;
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
    const float px = box.x + static_cast<float>(chart.marker.x) * sx;
    const float py = box.y + static_cast<float>(chart.marker.y) * sy;
    DrawCircleLines(static_cast<int>(px), static_cast<int>(py), 13.0f * bodyScale,
                    ToRay(p.sage));
  }

  for (const NavBodyView& body : chart.bodies) {
    const float cx = box.x + static_cast<float>(body.at.x) * sx;
    const float cy = box.y + static_cast<float>(body.at.y) * sy;
    const float radius = static_cast<float>(body.size) * 0.42f * bodyScale;
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
        static_cast<int>(box.x + static_cast<float>(body.labelX) * sx);
    Text(body.name.c_str(), labelX,
             static_cast<int>(box.y + static_cast<float>(body.labelY) * sy), TypePx(9) ,
             ToRay(body.scanned ? p.ink : p.muted));
    navHits_.push_back(NavHit{Area{cx - radius - 4.0f, cy - radius - 4.0f,
                                   radius * 2.0f + 8.0f, radius * 2.0f + 8.0f},
                              "select", body.id});
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

  float layerY = box.y + 62.0f;
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
       static_cast<int>(box.y + 7.0f), TypePx(9), ToRay(p.muted));

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

  const float halfW = (box.w - 6.0f) / 2.0f;
  float underH = 112.0f;
  for (const NavPanel& panel : screen.under) {
    underH = std::max(underH, MeasurePanelHeight(panel, halfW));
  }
  const float uy = box.Bottom() + 22.0f;
  for (std::size_t i = 0; i < screen.under.size(); ++i) {
    PaintPanel(screen.under[i],
               Area{box.x + static_cast<float>(i) * (halfW + 6.0f), uy, halfW,
                    underH});
  }

  // --- the commitment band, full width along the bottom
  const float bandY = std::max({layout_.content.Bottom() - 110.0f, ly + 8.0f, rightBottom + 8.0f, uy + underH + 8.0f});
  DrawRectangle(static_cast<int>(left), static_cast<int>(bandY),
                static_cast<int>(right - left), 104, ToRay(p.panel));
  DrawRectangleLines(static_cast<int>(left), static_cast<int>(bandY),
                     static_cast<int>(right - left), 104, ToRay(p.border));
  Text("Target Actions", static_cast<int>(left + 8.0f),
           static_cast<int>(bandY + 4.0f), TypePx(10) , ToRay(p.coffee));
  float ty = bandY + theme_.LineHeight() + 12.0f;
  for (const auto& line : WrapText(navScreen_.courseNote, right - left - 16.0f, TypePx(10), SelectFont(TypePx(10), false))) {
    Text(line.c_str(), static_cast<int>(left + 8.0f), static_cast<int>(ty), TypePx(10), ToRay(p.muted));
    ty += theme_.LineHeight();
  }
  ty += 8.0f;

  float px2 = left + 8.0f;
  for (const ProgressButton& button : screen.progress) {
    const float buttonW = std::max(148.0f, TextWidth(button.text.c_str(), TypePx(9)) + 16.0f);
    const Area buttonBox{px2, ty, buttonW, theme_.LineHeight() + 12.0f};
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
    px2 += buttonW + 8.0f;
  }

  ty += theme_.LineHeight() + 20.0f;
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
}

void ShellView::PaintCrewRecord(const Simulation& sim) {
  navHits_.clear();
  const Palette& p = theme_.palette;
  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;

  const float headerH = PaintTaskHeader(record_.head, left, layout_.content.y);

  // Status across the top, then the three short shelves left to right.
  float y = layout_.content.y + headerH + 8.0f;
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
  // Payroll and roster are separate in paintRoster(). Do not paint the full
  // roster as a table and then overlay clickable copies over its payroll rows.
  float rowY = panelTop;
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
  const Area hall{left + column + 10, panelTop, 560, 300};
  DrawRectangle(static_cast<int>(hall.x), static_cast<int>(hall.y),
                static_cast<int>(hall.w), static_cast<int>(hall.h), ToRay(p.panel2));
  DrawRectangleLines(static_cast<int>(hall.x), static_cast<int>(hall.y),
                     static_cast<int>(hall.w), static_cast<int>(hall.h),
                     ToRay(p.borderDark));
  BeginScissorMode(static_cast<int>(hall.x) + 1, static_cast<int>(hall.y - contentScroll_) + 1,
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


  // Orders retain the HTA dropdown and every note/cost. At narrow widths the
  // whole column moves below the hall instead of painting through its figure.
  const bool stacked = right - column < hall.Right() + 10;
  const float orderX = stacked ? hall.x : right - column;
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
    if (screenY >= layout_.content.y && screenY + comboH < layout_.content.Bottom()) {
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
  float uy = stacked ? oy + 10 : hall.Bottom() + 10;
  for (const auto& panel : screen.under) {
    const float h = MeasurePanelHeight(panel, hall.w);
    PaintPanel(panel, {hall.x, uy, hall.w, h}); uy += h + 6;
  }
  if (quarters_.hasCrew) {
    float y = uy + 6;
    wrapped("The Figure", hall.x + 8, y, hall.w - 16, ToRay(p.coffee));
    const Area talk{hall.x + 8, y, hall.w - 16, line + 12};
    DrawRectangleLines(static_cast<int>(talk.x), static_cast<int>(talk.y), static_cast<int>(talk.w), static_cast<int>(talk.h), ToRay(p.borderDark));
    Text(quarters_.talkLabel.c_str(), static_cast<int>(talk.x + 6), static_cast<int>(talk.y + 6), TypePx(10), ToRay(p.ink));
    if (sim.FindOngoing("doc-" + quarters_.selectedId) == nullptr) { navHits_.push_back({talk, "talk", ""}); }
    y = talk.Bottom() + 6;
    wrapped(quarters_.said.empty() ? "You have not asked them anything yet." : "\"" + quarters_.said + "\"", hall.x + 8, y, hall.w - 16, ToRay(p.muted));
    uy = y + 8;
  }
  const float bandY = std::max({rowY, oy, uy}) + 10;
  float y = bandY + 6;
  wrapped("Assignments", left + 8, y, right - left - 16, ToRay(p.coffee));
  wrapped(quarters_.bandNote, left + 8, y, right - left - 16, ToRay(p.muted));
  for (const auto& button : screen.progress) {
    const float width = std::max(220.0f, TextWidth(button.text.c_str(), TypePx(10)) + 16);
    const Area box{left + 8, y + 6, width, line + 12};
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w), static_cast<int>(box.h), ToRay(button.enabled ? p.panel2 : p.panel3));
    if (button.fillPercent > 0) {
      DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w * button.fillPercent / 100), static_cast<int>(box.h), ToRay(p.white));
    }
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.borderDark));
    Text(button.text.c_str(), static_cast<int>(box.x + 8), static_cast<int>(box.y + 6), TypePx(10), ToRay(p.ink));
    navHits_.push_back({box, "commit", ""});
    y = box.Bottom() + 8;
  }
}

bool ShellView::PressPanelRow(Simulation& sim, ScreenKind kind, const std::string& id) {
  switch (kind) {
    case ScreenKind::System: return PressSystemAction(sim, id);
    case ScreenKind::Info: return PressInfoAction(sim, id);
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
  const Color menuBg{0x3f,0x34,0x2b,255}, menuText{0xf2,0xe5,0xcc,255};
  const float cell = menu.w / static_cast<float>(title_.actions.size());
  for (std::size_t i = 0; i < title_.actions.size(); ++i) {
    const Area box{menu.x + cell * i, menu.y, cell, menu.h};
    const bool hover = box.Contains(mouseX_, mouseY_);
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w + 1), static_cast<int>(box.h),
                  hover ? Color{0x6b,0x54,0x3f,255} : menuBg);
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y), static_cast<int>(box.w), static_cast<int>(box.h), Color{0xa4,0x8e,0x72,255});
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
  const std::size_t count = screen.panels.empty() ? 1 : screen.panels.size();
  const float gap = 8.0f;
  const float columnW = (right - left - gap * static_cast<float>(count - 1)) /
                        static_cast<float>(count);
  for (std::size_t i = 0; i < screen.panels.size(); ++i) {
    const float x = left + static_cast<float>(i) * (columnW + gap);
    const NavPanel& panel = screen.panels[i];
    const float noteH = MeasurePanelHeight(panel, columnW);
    PaintPanel(panel, Area{x, top, columnW, noteH});

    float y = top + noteH + 6.0f;
    if (i < screen.panelRows.size()) {
      for (std::size_t r = 0; r < screen.panelRows[i].size(); ++r) {
        const CommandRowView& row = screen.panelRows[i][r];
        const auto labelLines = WrapText(row.label, columnW - 12.0f, TypePx(10), SelectFont(TypePx(10), false));
        const float rowH = (labelLines.size() + 1) * theme_.LineHeight() + 12.0f;
        const Area box{x, y, columnW, rowH};
        DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                      static_cast<int>(box.w), static_cast<int>(box.h),
                      ToRay(row.enabled ? p.panel2 : p.panel3));
        DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                           static_cast<int>(box.w), static_cast<int>(box.h),
                           ToRay(row.enabled ? p.borderDark : p.border));
        const std::string cost = row.displayCost.empty() ? std::string("free")
                                                         : row.displayCost;
        float textTop = box.y + 6.0f;
        for (const auto& line : labelLines) {
          Text(line.c_str(), static_cast<int>(box.x + 6.0f), static_cast<int>(textTop), TypePx(10), ToRay(row.enabled ? p.ink : p.muted));
          textTop += theme_.LineHeight();
        }
        Text(cost.c_str(), static_cast<int>(box.x + 6.0f), static_cast<int>(textTop), TypePx(9), ToRay(row.enabled ? p.coffee : p.muted));
        if (row.enabled && i < screen.panelRowIds.size() &&
            r < screen.panelRowIds[i].size()) {
          navHits_.push_back(NavHit{box, "row", screen.panelRowIds[i][r]});
        }
        y += rowH + 3.0f;
      }
    }
    panelsBottom = std::max(panelsBottom, y);
  }

  // Only screens with commitments, progress, or a confirmation get a bottom band.
  if (!screen.progress.empty() || !pendingConfirm_.empty() || !screen.bandNote.empty()) {
  const float bandY = std::max(layout_.content.Bottom() - 96.0f, panelsBottom + 12.0f);
  DrawRectangle(static_cast<int>(left), static_cast<int>(bandY),
                static_cast<int>(right - left), 90, ToRay(p.panel));
  DrawRectangleLines(static_cast<int>(left), static_cast<int>(bandY),
                     static_cast<int>(right - left), 90, ToRay(p.border));
  Text("Work in progress", static_cast<int>(left + 8.0f),
       static_cast<int>(bandY + 5.0f), TypePx(11), ToRay(p.coffee));
  float bandCursor = bandY + theme_.LineHeight() + 12.0f;
  for (const auto& line : WrapText(screen.bandNote, right - left - 16.0f, TypePx(10), SelectFont(TypePx(10), false))) {
    Text(line.c_str(), static_cast<int>(left + 8.0f), static_cast<int>(bandCursor), TypePx(10), ToRay(p.muted));
    bandCursor += theme_.LineHeight();
  }
  if (!pendingConfirm_.empty()) {
    for (const auto& line : WrapText(pendingConfirm_, right - left - 16.0f, TypePx(10), SelectFont(TypePx(10), false))) {
      Text(line.c_str(), static_cast<int>(left + 8.0f), static_cast<int>(bandCursor), TypePx(10), ToRay(p.terracotta));
      bandCursor += theme_.LineHeight();
    }
    const struct {
      const char* action;
      const char* label;
    } answers[] = {{"confirm-yes", "Yes, do it"}, {"confirm-no", "Keep as it is"}};
    float ax = left + 12.0f;
    for (const auto& entry : answers) {
      const float aw = static_cast<float>(TextWidth(entry.label, TypePx(10) )) + 20.0f;
      const Area box{ax, bandCursor + 6.0f, aw, theme_.LineHeight() + 12.0f};
      DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                         static_cast<int>(box.w), static_cast<int>(box.h),
                         ToRay(p.borderDark));
      Text(entry.label, static_cast<int>(box.x + 8.0f),
               static_cast<int>(box.y + 7.0f), TypePx(10) , ToRay(p.ink));
      navHits_.push_back(NavHit{box, entry.action, ""});
      ax += aw + 8.0f;
    }
  }

  if (!pendingConfirm_.empty()) { bandCursor += theme_.LineHeight() + 24.0f; }
  const float progressTop = bandCursor + 8.0f;
  float bx = left + 12.0f;
  for (const ProgressButton& button : screen.progress) {
    const Area box{bx, progressTop, 190.0f, 28.0f};
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
  if (!down) { draggingChart_ = false; return; }
  if (floating_.HasConfirmation()) { return; }
  if (titleActive_ || ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) { return; }
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
  else if (wikiIndexBox_.Contains(x, y + contentScroll_)) { wikiIndexScroll_ = std::max(0.0f, std::min(wikiIndexMaxScroll_, wikiIndexScroll_ - step)); }
  else if (wikiBodyBox_.Contains(x, y + contentScroll_)) { wikiBodyScroll_ = std::max(0.0f, std::min(wikiBodyMaxScroll_, wikiBodyScroll_ - step)); }
  else if (layout_.sidepanel.Contains(x, y)) { sideScroll_ = std::max(0.0f, std::min(std::max(0.0f, sideContentH_ - layout_.sidepanel.h), sideScroll_ - step)); }
  else { contentScroll_ = std::max(0.0f, std::min(contentMaxScroll_, contentScroll_ - step)); }
}

void ShellView::Escape(Simulation& sim) {
  if (LossVisible(sim)) { lossDismissed_ = true; return; }
  if (titleActive_) { if (titleConfirm_) { titleConfirm_ = false; } else { HideTitle(); } return; }
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

  // The quarters: a roster row, a job, an order, the sheet, a word.
  if (screenQuarters_) {
    for (const NavHit& hit : navHits_) {
      if (!hit.box.Contains(x, y + scrollY)) { continue; }
      if (PressQuartersAction(sim, hit.action, hit.arg) && hit.action == "commit") {
        SpawnMark(marks_, x, y + scrollY, nowMs, sim.State().reducedMotion);
      }
      return;
    }
    return;
  }
  if (screenRecord_) { return; }

  // Station and stores: each command row carries its own action id.
  if (screenPanels_) {
    for (const NavHit& hit : navHits_) {
      if (!hit.box.Contains(x, y + scrollY)) { continue; }
      if (hit.action == "confirm-no") { pendingConfirm_.clear(); pendingAction_.clear(); return; }
      if (hit.action == "confirm-yes") {
        const std::string action = pendingAction_;
        pendingConfirm_.clear();
        pendingAction_.clear();
        const bool acted = PressPanelRow(sim, panelsKind_, action);
        if (acted) { SpawnMark(marks_, x, y + scrollY, nowMs, sim.State().reducedMotion); }
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
      if (acted) { SpawnMark(marks_, x, y + scrollY, nowMs, sim.State().reducedMotion); }
      return;
    }
  }

  // The chart's own controls: bodies, the zoom pair, the layers, the actions.
  if (screenNav_) {
    for (const NavHit& hit : navHits_) {
      if (!hit.box.Contains(x, y + scrollY)) { continue; }
      if (PressNavAction(sim, hit.action, hit.arg) &&
          (hit.action == "travel" || hit.action == "scan")) {
        SpawnMark(marks_, x, y + scrollY, nowMs, sim.State().reducedMotion);
      }
      return;
    }
    return;
  }

  // A task window's own controls come first.
  for (const auto& entry : taskButtons_) {
    if (!entry.first.Contains(x, y + scrollY)) { continue; }
    if (PressProgressButton(sim, entry.second)) {
      SpawnMark(marks_, x, y + scrollY, nowMs, sim.State().reducedMotion);
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
    // The stage is the only clickable illustration on Home.
    if (idle_.stageArea.Contains(x, y + scrollY)) {
      const float localX = x - idle_.stageArea.x;
      const float localY = y + scrollY - idle_.stageArea.y;
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
          SpawnMark(marks_, x - layout_.content.x, y, nowMs,
                    sim.State().reducedMotion);
        }
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
        if (cell.id == "life") {
          OpenTask(sim, "room", "o2", false);
        } else {
          OpenTask(sim, "info", cell.id, false);
        }
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
    if (idle_.docked && idle_.dock.box.Contains(x, y + scrollY)) {
      if (button == HitButton::Right) {
        OpenTask(sim, "station", sim.Map().here, false);
      } else {
        OpenBriefing(sim, "station", sim.Map().here);
      }
      return;
    }
  }
}

}  // namespace ship
