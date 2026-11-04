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
  }
  PaintChrome(sim);

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
  float y = layout_.sidepanel.y + 8.0f;
  for (const SideBlock& block : SideBlocks(sim)) {
    draw->AddText(ImVec2(10.0f, y), ToIm(p.coffee), block.title.c_str());
    y += theme_.LineHeight() + 3.0f;
    if (block.indicators) {
      const std::vector<IndicatorRect> lines = PlaceIndicators(IndicatorLines(sim));
      for (const IndicatorRect& line : lines) {
        if (line.grow) {
          draw->AddRectFilled(ImVec2(10.0f + line.ghost.x, y + line.ghost.y),
                              ImVec2(12.0f + line.ghost.x, y + line.ghost.Bottom()),
                              ToIm(p.panel3));
          draw->AddRectFilled(ImVec2(10.0f + line.line.x, y + line.line.y),
                              ImVec2(12.0f + line.line.x, y + line.line.Bottom()),
                              ToIm(p.terracotta));
        } else {
          draw->AddRectFilled(ImVec2(10.0f + line.line.x, y + line.line.y),
                              ImVec2(12.0f + line.line.x, y + line.line.Bottom()),
                              ToIm(p.sage));
        }
      }
      y += Metrics::kIndH + 6.0f;
      continue;
    }
    if (!block.open) { continue; }
    for (const SideRow& row : block.rows) {
      draw->AddText(ImVec2(12.0f, y), ToIm(p.ink), row.key.c_str());
      if (!row.value.empty()) {
        draw->AddText(ImVec2(12.0f, y + 12.0f), ToIm(p.muted), row.value.c_str());
        y += 12.0f;
      }
      y += theme_.LineHeight();
      if (y > layout_.sidepanel.Bottom() - 4.0f) { break; }
    }
  }

  // ---- pinned foot -------------------------------------------------------
  draw->AddLine(ImVec2(0.0f, layout_.sidefoot.y), ImVec2(layout_.sidefoot.Right(), layout_.sidefoot.y),
                ToIm(p.border));
  float fx = 8.0f;
  for (const FootButton& button : FootButtons()) {
    draw->AddRect(ImVec2(fx, layout_.sidefoot.y + 8.0f),
                  ImVec2(fx + 26.0f, layout_.sidefoot.y + 34.0f), ToIm(p.borderDark), 3.0f);
    if (ImGui::IsMouseHoveringRect(ImVec2(fx, layout_.sidefoot.y + 8.0f),
                                   ImVec2(fx + 26.0f, layout_.sidefoot.y + 34.0f))) {
      ImGui::SetTooltip("%s", button.title.c_str());
    }
    fx += 34.0f;
  }
  draw->AddText(ImVec2(8.0f, layout_.sidefoot.y + 44.0f), ToIm(p.ink), "Wiki");
  draw->AddText(ImVec2(48.0f, layout_.sidefoot.y + 44.0f), ToIm(p.ink),
                RewardsLabel(sim).c_str());
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

  // Chrome first: the sidebar's foot, Home, then the ship.
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
