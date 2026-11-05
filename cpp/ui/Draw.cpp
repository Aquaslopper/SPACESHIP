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

namespace ship {

namespace {

::Color ToRay(const Rgb& rgb, int alpha = 255) {
  return ::Color{static_cast<unsigned char>(rgb.r), static_cast<unsigned char>(rgb.g),
                 static_cast<unsigned char>(rgb.b),
                 static_cast<unsigned char>(alpha)};
}

// "#rrggbb" -> a colour; anything else falls back rather than inventing one.
Rgb RgbFromHex(const std::string& hex, const Rgb& fallback) {
  if (hex.size() != 7 || hex[0] != '#') { return fallback; }
  const auto nibble = [&hex](std::size_t i) {
    const char c = hex[i];
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
  };
  int channels[3] = {0, 0, 0};
  for (int i = 0; i < 3; ++i) {
    const int hi = nibble(static_cast<std::size_t>(1 + i * 2));
    const int lo = nibble(static_cast<std::size_t>(2 + i * 2));
    if (hi < 0 || lo < 0) { return fallback; }
    channels[i] = hi * 16 + lo;
  }
  return Rgb{channels[0], channels[1], channels[2]};
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

ShellView::ShellView() { theme_ = Theme::ById("classic"); }

void ShellView::Configure(float width, float height, const Theme& theme) {
  width_ = width;
  height_ = height;
  theme_ = theme;
  theme_.stageCompact = false;  // set by the caller from STATE.shipClass
  layout_ = ComputeLayout(width_, height_, height_ - Metrics::kContentTop -
                                              Metrics::kStatusH,
                          others_, theme_.stageCompact);
}

void ShellView::Draw(Simulation& sim, double nowMs) {
  const float contentH = height_ - Metrics::kContentTop - Metrics::kStatusH;
  layout_ = ComputeLayout(width_, height_, contentH, others_, theme_.stageCompact);

  // The dot drift is cosmetic and confined to the idle screen; it is left in
  // step with the diagram that is about to be painted.
  if (!sim.State().reducedMotion && sim.State().activeTab == "home") {
    FxRng seeded = rng_;
    idle_ = BuildIdleScreen(sim, layout_, theme_, theme_.hideCrewLabels, others_,
                            seeded);
    dots_.clear();
    for (const DiagramCrew& crew : idle_.diagram.crew) {
      DriftDot dot;
      dot.id = crew.id;
      dot.roomId = crew.roomId;
      dot.x = crew.dot.x;
      dot.y = crew.dot.y;
      dot.tx = crew.dot.x;
      dot.ty = crew.dot.y;
      dots_.push_back(dot);
    }
    rng_ = seeded;
  }

  UpdateMarks(marks_, nowMs);

  BeginDrawing();
  ClearBackground(ToRay(theme_.palette.panel));

  if (sim.State().activeTab == "home") {
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
                    kind == ScreenKind::Event;
    panelsKind_ = kind;
    if (taskTabKey_ != sim.State().activeTab) {
      pendingConfirm_.clear();
      pendingAction_.clear();
    }
    taskTabKey_ = sim.State().activeTab;
    taskScreen_ = BuildTabScreen(sim, *FindTab(sim.State(), taskTabKey_));
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

  EndDrawing();
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
                   ImGuiWindowFlags_NoBackground);
  ImDrawList* draw = ImGui::GetWindowDrawList();

  // ---- taskbar -----------------------------------------------------------
  const ship::Palette& p = theme_.palette;
  draw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(width_, Metrics::kTaskbarH),
                      ToIm(p.panel2));
  draw->AddLine(ImVec2(0.0f, Metrics::kTaskbarH - 1.0f),
                ImVec2(width_, Metrics::kTaskbarH - 1.0f), ToIm(p.borderDark));

  float x = 4.0f;
  for (const TabView& tab : TabBar(sim)) {
    const std::string label = tab.title;
    const float w = ImGui::CalcTextSize(label.c_str()).x + 22.0f;
    if (x + w > layout_.tabStrip.w) { break; }
    const ImU32 fill = tab.active ? ToIm(p.white)
                       : tab.fresh ? ToIm(Rgb{233, 214, 184})
                                   : ToIm(p.panel3);
    draw->AddRectFilled(ImVec2(x, 3.0f), ImVec2(x + w, Metrics::kTaskbarH - 2.0f), fill,
                        3.0f);
    draw->AddRect(ImVec2(x, 3.0f), ImVec2(x + w, Metrics::kTaskbarH - 2.0f),
                  ToIm(p.border), 3.0f);
    draw->AddText(ImVec2(x + 7.0f, 9.0f), ToIm(p.muted), label.c_str());
    if (tab.closable) {
      draw->AddText(ImVec2(x + w - 13.0f, 9.0f), ToIm(p.muted), "x");
    }
    x += w + 4.0f;
  }

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

  const std::vector<SideHit> hits =
      SideHits(sim, layout_.sidepanel, layout_.sidefoot, theme_.LineHeight());
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
    DrawText(cell.label.c_str(), static_cast<int>(cell.box.x + 6.0f),
             static_cast<int>(cell.box.y + 4.0f), 11, ToRay(theme.palette.muted));
    DrawText(cell.value.c_str(), static_cast<int>(cell.box.x + 6.0f),
             static_cast<int>(cell.box.y + 20.0f), 20, ToRay(theme.palette.ink));
    const float bar = cell.box.w - 12.0f;
    DrawRectangle(static_cast<int>(cell.box.x + 6.0f),
                  static_cast<int>(cell.box.Bottom() - 12.0f), static_cast<int>(bar), 4,
                  ToRay(theme.palette.panel3));
    DrawRectangle(static_cast<int>(cell.box.x + 6.0f),
                  static_cast<int>(cell.box.Bottom() - 12.0f),
                  static_cast<int>(bar * std::max(0.0, std::min(100.0, cell.reading)) / 100.0),
                  4, ToRay(theme.palette.coffee));
  }

  PaintDiagram(idle_.diagram);

  DrawText(idle_.stageNote.c_str(), static_cast<int>(idle_.content.x + 8.0f),
           static_cast<int>(idle_.stageArea.Bottom() + 8.0f), 12,
           ToRay(theme.palette.muted));

  // ---- nav strip, dock strip, info rail ---------------------------------
  auto paintStrip = [&theme](const Strip& strip) {
    DrawRectangleLines(static_cast<int>(strip.box.x), static_cast<int>(strip.box.y),
                       static_cast<int>(strip.box.w), static_cast<int>(strip.box.h),
                       ToRay(theme.palette.border));
    DrawText(strip.label.c_str(), static_cast<int>(strip.box.x + 8.0f),
             static_cast<int>(strip.box.y + 6.0f), 11, ToRay(theme.palette.muted));
    DrawText(strip.main.c_str(), static_cast<int>(strip.box.x + 8.0f),
             static_cast<int>(strip.box.y + 20.0f), 13, ToRay(theme.palette.ink));
    DrawText(strip.right.c_str(), static_cast<int>(strip.box.x + 8.0f),
             static_cast<int>(strip.box.y + 32.0f), 10, ToRay(theme.palette.muted));
  };
  paintStrip(idle_.nav);
  if (idle_.docked) { paintStrip(idle_.dock); }

  for (const RailCell& cell : idle_.infos) {
    DrawText(cell.label.c_str(), static_cast<int>(cell.box.x), static_cast<int>(cell.box.y),
             10, ToRay(theme.palette.muted));
    DrawText(cell.value.c_str(), static_cast<int>(cell.box.x),
             static_cast<int>(cell.box.y + 13.0f), 13, ToRay(theme.palette.ink));
  }

  // ---- feed --------------------------------------------------------------
  DrawText(idle_.feedHead.c_str(), static_cast<int>(idle_.feedBox.x),
           static_cast<int>(idle_.feedBox.y - 16.0f), 11, ToRay(theme.palette.coffee));
  DrawRectangleLines(static_cast<int>(idle_.feedBox.x),
                     static_cast<int>(idle_.feedBox.y),
                     static_cast<int>(idle_.feedBox.w),
                     static_cast<int>(idle_.feedBox.h), ToRay(theme.palette.border));
  float y = idle_.feedBox.y + 4.0f;
  for (const std::string& line : idle_.feed) {
    if (y > idle_.feedBox.Bottom() - 12.0f) { break; }
    DrawText(line.c_str(), static_cast<int>(idle_.feedBox.x + 6.0f),
             static_cast<int>(y), 12, ToRay(theme.palette.ink));
    y += theme.LineHeight();
  }
  (void)sim;
}

void ShellView::PaintPanel(const NavPanel& panel, const Area& box) {
  const Palette& p = theme_.palette;
  DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.panel));
  DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                     static_cast<int>(box.w), static_cast<int>(box.h),
                     ToRay(p.border));
  DrawText(panel.title.c_str(), static_cast<int>(box.x + 8.0f),
           static_cast<int>(box.y + 5.0f), 11, ToRay(p.coffee));
  float y = box.y + 24.0f;
  for (const PanelLine& line : panel.lines) {
    switch (line.kind) {
      case PanelLine::Kind::Name:
        DrawText(line.value.c_str(), static_cast<int>(box.x + 8.0f),
                 static_cast<int>(y), 22, ToRay(p.ink));
        y += 22.0f;
        break;
      case PanelLine::Kind::Sub:
        DrawText(line.value.c_str(), static_cast<int>(box.x + 8.0f),
                 static_cast<int>(y), 10, ToRay(p.muted));
        y += 13.0f;
        break;
      case PanelLine::Kind::Bar: {
        const float w = box.w - 16.0f;
        DrawRectangle(static_cast<int>(box.x + 8.0f), static_cast<int>(y),
                      static_cast<int>(w), 5, ToRay(p.panel3));
        DrawRectangle(static_cast<int>(box.x + 8.0f), static_cast<int>(y),
                      static_cast<int>(w * std::max(0.0, std::min(100.0, line.pct)) / 100.0),
                      5, ToRay(p.coffee));
        y += 11.0f;
        break;
      }
      case PanelLine::Kind::Dim:
      case PanelLine::Kind::Note:
        DrawText(line.value.c_str(), static_cast<int>(box.x + 8.0f),
                 static_cast<int>(y), 10, ToRay(p.muted));
        y += 12.0f * static_cast<float>(1 + line.value.size() / 34);
        break;
      case PanelLine::Kind::Field:
        DrawText(line.key.c_str(), static_cast<int>(box.x + 8.0f),
                 static_cast<int>(y), 10, ToRay(p.muted));
        DrawText(line.value.c_str(), static_cast<int>(box.x + box.w - 8.0f -
                                                          static_cast<float>(MeasureText(line.value.c_str(), 10))),
                 static_cast<int>(y), 10, ToRay(p.ink));
        y += 14.0f;
        break;
    }
    if (y > box.Bottom() - 4.0f) { break; }
  }
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
      DrawText(part.text.c_str(), x + 2, y + 1, 8, ToRay(p.muted));
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
    DrawText(control.button.text.c_str(), static_cast<int>(box.x + 6.0f),
             static_cast<int>(box.y + 7.0f), 10,
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

  DrawText(screen.head.kind.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y), 9, ToRay(p.muted));
  DrawText(screen.head.title.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 12.0f), 20, ToRay(p.ink));
  DrawText(screen.head.sub.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 36.0f), 11, ToRay(p.muted));

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
  DrawText("Actions", static_cast<int>(left + 8.0f),
           static_cast<int>(bandY + 5.0f), 11, ToRay(p.coffee));
  DrawText(screen.bandNote.c_str(), static_cast<int>(left + 8.0f),
           static_cast<int>(bandY + 20.0f), 10, ToRay(p.muted));

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
    DrawText(button.text.c_str(), static_cast<int>(box.x + 8.0f),
             static_cast<int>(box.y + 8.0f), 10,
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

  DrawText(screen.head.kind.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y), 9, ToRay(p.muted));
  DrawText(screen.head.title.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 12.0f), 20, ToRay(p.ink));
  DrawText(screen.head.sub.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 36.0f), 11, ToRay(p.muted));

  // --- the chart box: 638x278 at 1:1, shrunk only when the window is narrower
  const float spareW = middleRight - middleLeft;
  float scale = 1.0f;
  if (chart.zoom >= 0 && spareW < 638.0f) { scale = spareW / 638.0f; }
  const Area box{middleLeft, layout_.content.y + 58.0f, 638.0f * scale,
                 278.0f * scale};

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
    DrawText(body.name.c_str(), labelX,
             static_cast<int>(box.y + static_cast<float>(body.labelY) * zs), 9,
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
    DrawText(NavLayerLabel(key).c_str(), static_cast<int>(boxArea.x + 6.0f),
             static_cast<int>(boxArea.y + 4.0f), 9,
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
  DrawText("+", static_cast<int>(zoomIn.x + 6.0f), static_cast<int>(zoomIn.y + 3.0f),
           11, ToRay(p.ink));
  DrawText("-", static_cast<int>(zoomOut.x + 6.0f),
           static_cast<int>(zoomOut.y + 3.0f), 11, ToRay(p.ink));
  navHits_.push_back(NavHit{zoomIn, "zoom-in", ""});
  navHits_.push_back(NavHit{zoomOut, "zoom-out", ""});
  DrawText(chart.scaleText.c_str(), static_cast<int>(box.Right() - 260.0f),
           static_cast<int>(box.Bottom() - 18.0f), 9, ToRay(p.muted));

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
  DrawText("Target Actions", static_cast<int>(left + 8.0f),
           static_cast<int>(bandY + 4.0f), 10, ToRay(p.coffee));
  float ty = bandY + 18.0f;
  DrawText(navScreen_.courseNote.c_str(), static_cast<int>(left + 8.0f),
           static_cast<int>(ty), 10, ToRay(p.muted));
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
    DrawText(button.text.c_str(), static_cast<int>(buttonBox.x + 6.0f),
             static_cast<int>(buttonBox.y + 6.0f), 9,
             ToRay(button.state == ButtonState::Cooldown ? p.terracotta : p.ink));
    navHits_.push_back(NavHit{buttonBox, button.taskId, ""});
    px2 += 156.0f;
  }

  ty += 26.0f;
  for (const CommandRowView& row : screen.commands) {
    const Area rowBox{left + 8.0f, ty, 300.0f, 18.0f};
    DrawText(row.label.c_str(), static_cast<int>(rowBox.x + 34.0f),
             static_cast<int>(rowBox.y + 4.0f), 10,
             ToRay(row.enabled ? p.ink : p.muted));
    DrawText((row.displayCost + "  ").c_str(), static_cast<int>(rowBox.x),
             static_cast<int>(rowBox.y + 4.0f), 9, ToRay(p.coffee));
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
    DrawText(note.c_str(), static_cast<int>(left + 8.0f), static_cast<int>(ty), 9,
             ToRay(p.muted));
    ty += 11.0f;
  }
}

void ShellView::PaintCrewRecord(const Simulation& sim) {
  navHits_.clear();
  const Palette& p = theme_.palette;
  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;

  DrawText(record_.head.kind.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y), 9, ToRay(p.muted));
  DrawText(record_.head.title.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 12.0f), 20, ToRay(p.ink));
  DrawText(record_.head.sub.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 36.0f), 11, ToRay(p.muted));

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

  DrawText(screen.head.kind.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y), 9, ToRay(p.muted));
  DrawText(screen.head.title.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 12.0f), 20, ToRay(p.ink));
  DrawText(screen.head.sub.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 36.0f), 10, ToRay(p.muted));

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
      DrawText(part.text.c_str(), x + 2, y + 1, 8, ToRay(p.muted));
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
    DrawText(row.name.c_str(), static_cast<int>(box.x + 8.0f),
             static_cast<int>(box.y + 3.0f), 10, ToRay(p.ink));
    DrawText(row.role.c_str(), static_cast<int>(box.x + 8.0f),
             static_cast<int>(box.y + 13.0f), 8, ToRay(p.muted));
    if (!row.flag.empty()) {
      DrawText(row.flag.c_str(), static_cast<int>(box.Right() - 30.0f),
               static_cast<int>(box.y + 6.0f), 8, ToRay(p.terracotta));
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
        DrawText(label.c_str(), static_cast<int>(box.x),
                 static_cast<int>(box.y), 9,
                 ToRay(line.on ? p.ink : (line.locked ? p.muted : p.ink)));
        DrawText(line.cost.c_str(), static_cast<int>(box.Right() - 46.0f),
                 static_cast<int>(box.y), 8, ToRay(p.coffee));
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
  DrawText("Assignments", static_cast<int>(left + 8.0f),
           static_cast<int>(bandY + 4.0f), 9, ToRay(p.coffee));
  DrawText(quarters_.bandNote.c_str(), static_cast<int>(left + 8.0f),
           static_cast<int>(bandY + 18.0f), 10, ToRay(p.muted));
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
    DrawText(button.text.c_str(), static_cast<int>(box.x + 8.0f),
             static_cast<int>(box.y + 7.0f), 10, ToRay(p.ink));
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
  const Rgb accent = RgbFromHex(vignette.accent, p.coffee);
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

  DrawText(vignette.code.c_str(),
           static_cast<int>(box.x + vignette.readout.x),
           static_cast<int>(box.y + vignette.readout.y), 9, ToRay(p.coffee));
  DrawText(vignette.label.c_str(),
           static_cast<int>(box.x + vignette.readout.x),
           static_cast<int>(box.y + vignette.readout.y + 14.0f), 13, ToRay(p.ink));
  DrawText(vignette.description.c_str(),
           static_cast<int>(box.x + vignette.readout.x),
           static_cast<int>(box.y + vignette.readout.y + 32.0f), 9, ToRay(p.muted));
  DrawText(vignette.statusTop.c_str(),
           static_cast<int>(box.x + vignette.readout.x),
           static_cast<int>(box.y + box.h - 34.0f), 8, ToRay(p.muted));
  DrawText(vignette.statusValue.c_str(),
           static_cast<int>(box.x + vignette.readout.x),
           static_cast<int>(box.y + box.h - 22.0f), 9, ToRay(p.coffee));

  for (std::size_t i = 0; i < vignette.meter.size(); ++i) {
    const Area& bar = vignette.meter[i];
    DrawRectangle(static_cast<int>(box.x + bar.x), static_cast<int>(box.y + bar.y),
                  static_cast<int>(bar.w), static_cast<int>(bar.h),
                  ToRay(i < 5 ? p.coffee : p.panel3));
  }
}

void ShellView::PaintPanelsScreen(Simulation& sim) {
  navHits_.clear();
  taskButtons_.clear();
  const Palette& p = theme_.palette;
  const TaskScreenModel& screen = taskScreen_;
  const float left = layout_.content.x + 8.0f;
  const float right = layout_.content.Right() - 8.0f;

  DrawText(screen.head.kind.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y), 9, ToRay(p.muted));
  DrawText(screen.head.title.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 12.0f), 20, ToRay(p.ink));
  DrawText(screen.head.sub.c_str(), static_cast<int>(left),
           static_cast<int>(layout_.content.y + 36.0f), 11, ToRay(p.muted));

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
        const float costW = static_cast<float>(MeasureText(cost.c_str(), 9));
        const std::string label = row.label;
        std::string shown = label;
        while (!shown.empty() &&
               static_cast<float>(MeasureText(shown.c_str(), 10)) >
                   columnW - costW - 26.0f) {
          shown.pop_back();
        }
        DrawText(shown.c_str(), static_cast<int>(box.x + 6.0f),
                 static_cast<int>(box.y + 6.0f), 10,
                 ToRay(row.enabled ? p.ink : p.muted));
        DrawText(cost.c_str(), static_cast<int>(box.Right() - 6.0f - costW),
                 static_cast<int>(box.y + 6.0f), 9,
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
    DrawText(pendingConfirm_.c_str(), static_cast<int>(left + 8.0f),
             static_cast<int>(bandY + 24.0f), 10, ToRay(p.terracotta));
    const struct {
      const char* action;
      const char* label;
    } answers[] = {{"confirm-yes", "Yes, do it"}, {"confirm-no", "Keep as it is"}};
    float ax = left + 12.0f;
    for (const auto& entry : answers) {
      const float aw = static_cast<float>(MeasureText(entry.label, 10)) + 20.0f;
      const Area box{ax, bandY + 46.0f, aw, 24.0f};
      DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                         static_cast<int>(box.w), static_cast<int>(box.h),
                         ToRay(p.borderDark));
      DrawText(entry.label, static_cast<int>(box.x + 8.0f),
               static_cast<int>(box.y + 7.0f), 10, ToRay(p.ink));
      navHits_.push_back(NavHit{box, entry.action, ""});
      ax += aw + 8.0f;
    }
  }

  DrawText("Work in progress", static_cast<int>(left + 8.0f),
           static_cast<int>(bandY + 5.0f), 11, ToRay(p.coffee));
  DrawText(screen.bandNote.c_str(), static_cast<int>(left + 8.0f),
           static_cast<int>(bandY + 20.0f), 10, ToRay(p.muted));
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
    DrawText(button.text.c_str(), static_cast<int>(box.x + 8.0f),
             static_cast<int>(box.y + 8.0f), 10,
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
  DrawText(popup_.title.c_str(), static_cast<int>(card.x + 8.0f),
           static_cast<int>(card.y + 6.0f), 12, ToRay(p.ink));
  const Area closeBox{card.Right() - 26.0f, card.y + 3.0f, 20.0f, 18.0f};
  DrawText("x", static_cast<int>(closeBox.x + 7.0f),
           static_cast<int>(closeBox.y + 3.0f), 12, ToRay(p.muted));
  popupHits_.push_back(NavHit{closeBox, "close", ""});

  float cy = card.y + 30.0f;
  const float pad = 10.0f;
  if (!popup_.notice.empty()) {
    DrawText(popup_.notice.c_str(), static_cast<int>(card.x + pad),
             static_cast<int>(cy), 9, ToRay(p.muted));
    cy += 12.0f * static_cast<float>(1 + popup_.notice.size() / 74);
  }

  for (const PopupSection& section : popup_.sections) {
    DrawText(section.title.c_str(), static_cast<int>(card.x + pad),
             static_cast<int>(cy), 10, ToRay(p.coffee));
    cy += 14.0f;
    for (const PopupRow& row : section.rows) {
      if (cy + 14.0f > card.Bottom() - 8.0f) { break; }
      DrawText(row.key.c_str(), static_cast<int>(card.x + pad),
               static_cast<int>(cy), 10, ToRay(p.ink));
      const float valueW = static_cast<float>(MeasureText(row.value.c_str(), 9));
      DrawText(row.value.c_str(),
               static_cast<int>(card.Right() - pad - valueW),
               static_cast<int>(cy + 1.0f), 9, ToRay(p.muted));
      cy += 13.0f;
    }
    cy += 6.0f;
  }

  int shownLines = 0;
  for (const std::string& line : popup_.lines) {
    if (cy + 13.0f > card.Bottom() - 8.0f) { break; }
    std::string text = line;
    if (text.size() > 84) { text = text.substr(0, 84) + "..."; }
    DrawText(text.c_str(), static_cast<int>(card.x + pad), static_cast<int>(cy), 9,
             ToRay(p.ink));
    cy += 12.0f;
    ++shownLines;
  }
  if (shownLines > 0 && shownLines < static_cast<int>(popup_.lines.size())) {
    const std::string more = "Older entries are folded away.";
    DrawText(more.c_str(), static_cast<int>(card.x + pad), static_cast<int>(cy + 2.0f),
             9, ToRay(p.muted));
  }

  for (const PopupToggle& toggle : popup_.toggles) {
    const Area box{card.x + pad, cy, card.w - pad * 2.0f, 22.0f};
    DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                  static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.panel2));
    DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                       static_cast<int>(box.w), static_cast<int>(box.h),
                       ToRay(p.borderDark));
    DrawText(toggle.label.c_str(), static_cast<int>(box.x + 6.0f),
             static_cast<int>(box.y + 6.0f), 10, ToRay(p.ink));
    popupHits_.push_back(NavHit{box, "toggle", toggle.id});
    cy += 24.0f;
  }

  if (!popup_.themes.empty()) {
    float tx = card.x + pad;
    for (std::size_t i = 0; i < popup_.themes.size(); ++i) {
      const std::string& key = popup_.themes[i];
      const bool active = key == popup_.theme;
      const float tw = static_cast<float>(
          MeasureText(popup_.themeLabels[i].c_str(), 9)) + 16.0f;
      const Area box{tx, cy, tw, 20.0f};
      DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                    static_cast<int>(box.w), static_cast<int>(box.h),
                    ToRay(active ? p.white : p.panel3));
      DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                         static_cast<int>(box.w), static_cast<int>(box.h),
                         ToRay(active ? p.coffee : p.border));
      DrawText(popup_.themeLabels[i].c_str(), static_cast<int>(box.x + 8.0f),
               static_cast<int>(box.y + 5.0f), 9, ToRay(p.ink));
      popupHits_.push_back(NavHit{box, "theme", key});
      tx += tw + 6.0f;
    }
    cy += 26.0f;
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
    DrawText(slot.label.c_str(), static_cast<int>(box.x + 6.0f),
             static_cast<int>(box.y + 4.0f), 10, ToRay(p.ink));
    DrawText(slot.summary.c_str(), static_cast<int>(box.x + 6.0f),
             static_cast<int>(box.y + 17.0f), 9, ToRay(p.muted));
    popupHits_.push_back(
        NavHit{box, "slot", std::to_string(slot.slot)});
    cy += 33.0f;
  }

  if (!popup_.slots.empty()) {
    const struct {
      const char* action;
      const char* label;
    } buttons[] = {{"save", "Save voyage"},
                   {"load", "Load voyage"},
                   {"new-voyage", "Start a new voyage"}};
    float bx = card.x + pad;
    for (const auto& entry : buttons) {
      const float bw = static_cast<float>(MeasureText(entry.label, 10)) + 22.0f;
      const Area box{bx, cy, bw, 24.0f};
      DrawRectangle(static_cast<int>(box.x), static_cast<int>(box.y),
                    static_cast<int>(box.w), static_cast<int>(box.h), ToRay(p.panel2));
      DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                         static_cast<int>(box.w), static_cast<int>(box.h),
                         ToRay(p.borderDark));
      DrawText(entry.label, static_cast<int>(box.x + 8.0f),
               static_cast<int>(box.y + 7.0f), 10, ToRay(p.ink));
      popupHits_.push_back(NavHit{box, entry.action, std::to_string(popupSlot_)});
      bx += bw + 6.0f;
    }
    cy += 30.0f;
  }

  if (confirmNewVoyage_) {
    DrawText("Start a new voyage? Unsaved progress will be lost; saved slots are kept.",
             static_cast<int>(card.x + pad), static_cast<int>(cy), 9,
             ToRay(p.terracotta));
    cy += 14.0f;
    const struct {
      const char* action;
      const char* label;
    } answers[] = {{"new-voyage-yes", "Begin again"}, {"new-voyage-no", "Keep this voyage"}};
    float bx = card.x + pad;
    for (const auto& entry : answers) {
      const float bw = static_cast<float>(MeasureText(entry.label, 10)) + 22.0f;
      const Area box{bx, cy, bw, 22.0f};
      DrawRectangleLines(static_cast<int>(box.x), static_cast<int>(box.y),
                         static_cast<int>(box.w), static_cast<int>(box.h),
                         ToRay(p.borderDark));
      DrawText(entry.label, static_cast<int>(box.x + 8.0f),
               static_cast<int>(box.y + 6.0f), 10, ToRay(p.ink));
      popupHits_.push_back(NavHit{box, entry.action, ""});
      bx += bw + 6.0f;
    }
    cy += 24.0f;
  }

  if (!popup_.message.empty()) {
    DrawText(popup_.message.c_str(), static_cast<int>(card.x + pad),
             static_cast<int>(card.Bottom() - 16.0f), 9, ToRay(p.coffee));
  }
}

void ShellView::PaintDiagram(const DiagramModel& diagram) {
  const ::Color frame = ToRay(theme_.palette.coffee);
  const ::Color hull = ToRay(Rgb{253, 251, 247});
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
  DrawText(diagram.corridorLabel.c_str(),
           static_cast<int>(ox + diagram.corridorLabelBox.x),
           static_cast<int>(oy + diagram.corridorLabelBox.y), 9,
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
      DrawText(room.name.c_str(), x + 6, y + 4, 10, ToRay(theme_.palette.ink));
      if (!room.small) {
        DrawText(room.tag.c_str(), x + w - 30, y + h - 14, 8,
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
      DrawText(crew.firstName.c_str(), static_cast<int>(ox + crew.label.x),
               static_cast<int>(oy + crew.label.y), 9, ToRay(theme_.palette.muted));
    }
  }
}

void ShellView::Click(Simulation& sim, float x, float y, HitButton button,
                      double nowMs) {
  if (button == HitButton::None) { return; }

  // A floating popup covers the window: it is the topmost thing, and it is
  // free. Closing it puts the reader back exactly where they were.
  if (!popupKey_.empty()) {
    for (const NavHit& hit : popupHits_) {
      if (!hit.box.Contains(x, y)) { continue; }
      if (hit.action == "close") { ClosePopup(); return; }
      if (hit.action == "slot") { popupSlot_ = std::stoi(hit.arg); return; }
      if (hit.action == "new-voyage") { confirmNewVoyage_ = true; return; }
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

  // Chrome first: the sidebar (blocks, events, foot), Home, then the ship.
  for (const SideHit& hit : SideHits(sim, layout_.sidepanel, layout_.sidefoot,
                                     theme_.LineHeight())) {
    if (!hit.box.Contains(x, y)) { continue; }
    switch (hit.action) {
      case SideAction::Toggle:
        if (hit.key == "indicators") { return; }  // the glance never folds
        sim.MutableState().spOpen[hit.key] = !sim.MutableState().spOpen[hit.key];
        return;
      case SideAction::OpenEvent:
        OpenTask(sim, "event", hit.key, false);   // tab only - no jump
        return;
      case SideAction::Wiki:
        OpenTask(sim, "wiki", "main", true);
        return;
      case SideAction::Rewards:
        OpenTask(sim, "awards", "main", true);
        return;
      case SideAction::Manifest:
        OpenPopup("manifest");
        return;
      case SideAction::Log:
        OpenPopup("log");
        return;
      case SideAction::Files:
        OpenPopup("files");
        return;
      case SideAction::Settings:
        OpenPopup("settings");
        return;
      case SideAction::None:
        break;
    }
    break;
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
        if (!hit.Contains(localX, localY)) { continue; }
        if (button == HitButton::Right) {
          OpenTask(sim, "crew", crew.id, true);
        } else {
          selection_ = DescribeTarget(sim, "crew", crew.id);
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
          OpenTask(sim, "room", room.id, true);
        } else {
          selection_ = DescribeTarget(sim, "room", room.id);
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
        OpenTask(sim, "system", cell.id, true);
      } else {
        selection_ = DescribeTarget(sim, "system", cell.id);
        hasSelection_ = true;
      }
      return;
    }
    for (const RailCell& cell : idle_.infos) {
      if (!cell.box.Contains(x, y)) { continue; }
      if (button == HitButton::Right) {
        if (cell.id == "life") {
          OpenTask(sim, "room", "o2", true);
        } else {
          OpenTask(sim, "info", cell.id, true);
        }
      } else {
        selection_ = DescribeTarget(sim, "info", cell.id);
        hasSelection_ = true;
      }
      return;
    }
    if (idle_.nav.box.Contains(x, y)) {
      if (button == HitButton::Right) {
        OpenTask(sim, "nav", "main", true);
      } else {
        selection_ = DescribeTarget(sim, "nav", "main");
        hasSelection_ = true;
      }
      return;
    }
  }
}

}  // namespace ship
