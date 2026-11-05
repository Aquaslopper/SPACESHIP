// CPU recording backend for the real Draw.cpp, not a replacement view model.
// This checks drawing coordinates/colours and input; it is NOT a GPU screenshot.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "imgui.h"
#include "raylib.h"
#include "ui/Draw.h"
#include "ui/Shell.h"

namespace {
struct TextCall { std::string text; float x, y, size; int order; };
struct RectCall { float x, y, w, h; Color color; int order; };
std::vector<TextCall> texts;
std::vector<RectCall> rects;
std::vector<ship::Area> scissors;
float cameraY = 0;
int drawOrder = 0;
int textureCalls = 0;
void Require(bool ok, const char* message) {
  if (!ok) { throw std::runtime_error(message); }
}
const TextCall& FindText(const std::string& value) {
  const auto it = std::find_if(texts.begin(), texts.end(), [&](const auto& t) { return t.text == value; });
  if (it == texts.end()) { throw std::runtime_error("Not painted: " + value); }
  return *it;
}
void Frame(ship::ShellView& view, ship::Simulation& sim, double nowMs = 0) {
  texts.clear(); rects.clear(); scissors.clear(); textureCalls = 0; drawOrder = 0;
  ImGui::NewFrame(); view.Draw(sim, nowMs); ImGui::Render();
  for (const auto& text : texts) { Require(text.size >= 18, "Native text fell below 18px"); }
}
void RequireMark(float x, float y) {
  Require(std::any_of(rects.begin(), rects.end(), [&](const auto& r) {
    return r.color.r == 126 && r.color.g == 198 && r.color.b == 63 && r.w == 11 && r.h == 2 &&
           std::abs(r.x - (x - 5)) < 1.1f && std::abs(r.y - (y - 1)) < 1.1f;
  }), "Positive-click mark did not land at the screen-space mouse position");
  Require(std::any_of(rects.begin(), rects.end(), [](const auto& r) {
    return r.color.r == 182 && r.color.g == 240 && r.color.b == 125 && r.color.a > 0 && r.color.a < 255;
  }), "Positive-click mark has no soft green halo");
}
RectCall FindBand(const ship::ShellView& view) {
  const auto c = view.LayoutNow().content;
  const auto it = std::find_if(rects.begin(), rects.end(), [&](const auto& r) {
    return std::abs(r.x - (c.x + 8)) < 1 && std::abs(r.w - (c.w - 16)) < 1 &&
           std::abs(r.y + r.h - (c.Bottom() - 8)) < 2 && r.h > 45;
  });
  Require(it != rects.end(), "Measured action band is not anchored at viewport bottom");
  Require(it->y >= c.y, "Action band grew above the viewport");
  return *it;
}
void RequireCenteredButtons(const ship::ShellView& view, const RectCall& band) {
  const auto p = view.CurrentTheme().palette;
  std::vector<RectCall> buttons;
  for (const auto& r : rects) {
    if (r.y < band.y || r.y + r.h > band.y + band.h || r.w < 150 || r.h < 30) continue;
    const bool enabled = r.color.r == p.control.r && r.color.g == p.control.g && r.color.b == p.control.b;
    const bool disabled = r.color.r == p.panel3.r && r.color.g == p.panel3.g && r.color.b == p.panel3.b;
    if (enabled || disabled) buttons.push_back(r);
  }
  Require(!buttons.empty(), "No action buttons inside the measured band");
  for (const auto& button : buttons) {
    float left = button.x, right = button.x + button.w;
    for (const auto& other : buttons) if (std::abs(other.y - button.y) < 1) {
      left = std::min(left, other.x); right = std::max(right, other.x + other.w);
    }
    Require(std::abs((left + right) / 2 - (band.x + band.w / 2)) < 2, "Action button row is not centered");
    for (const auto& text : texts) if (text.order > button.order && text.x >= button.x && text.x < button.x + button.w && text.y >= button.y && text.y < button.y + button.h) {
      const float width = MeasureTextEx({}, text.text.c_str(), text.size, 1).x;
      Require(std::abs(text.x + width / 2 - (button.x + button.w / 2)) < 2, "Button label is not centered");
      Require(text.y + text.size <= button.y + button.h, "Readable button label overflowed its box");
    }
  }
}

}

// Match raylib's camera transform: screen = (world - target) * zoom + offset.
void BeginMode2D(Camera2D c) { cameraY = c.offset.y - c.target.y * c.zoom; }
void EndMode2D() { cameraY = 0; }
Color Fade(Color color, float alpha) { color.a = static_cast<unsigned char>(255 * std::clamp(alpha, 0.0f, 1.0f)); return color; }
void ClearBackground(Color) {}
void BeginScissorMode(int x, int y, int w, int h) { scissors.push_back({float(x), float(y), float(w), float(h)}); }
void EndScissorMode() {}
Vector2 MeasureTextEx(Font, const char* text, float size, float spacing) {
  auto p = ImGui::GetFont()->CalcTextSizeA(size, 100000, 0, text);
  return {p.x + std::max(0, static_cast<int>(std::string(text).size()) - 1) * spacing, p.y};
}
void DrawTextEx(Font, const char* text, Vector2 p, float size, float, Color) {
  texts.push_back({text, p.x, p.y + cameraY, size, ++drawOrder});
}
void DrawRectangle(int x, int y, int w, int h, Color color) {
  rects.push_back({static_cast<float>(x), y + cameraY, static_cast<float>(w), static_cast<float>(h), color, ++drawOrder});
}
void DrawRectangleLines(int, int, int, int, Color) {}
void DrawRectangleLinesEx(Rectangle, float, Color) {}
void DrawRectangleRounded(Rectangle, float, int, Color) {}
void DrawRectangleRoundedLinesEx(Rectangle, float, int, float, Color) {}
void DrawEllipse(int, int, float, float, Color) {}
void DrawCircle(int, int, float, Color) {}
void DrawCircleV(Vector2, float, Color) {}
void DrawCircleLines(int, int, float, Color) {}
void DrawLine(int, int, int, int, Color) {}
void DrawLineEx(Vector2, Vector2, float, Color) {}
void DrawRing(Vector2, float, float, float, float, int, Color) {}
void DrawTexturePro(Texture2D, Rectangle, Rectangle, Vector2, float, Color) { ++textureCalls; }

int main() {
  try {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60;
    ImFontConfig config; config.SizePixels = 18;
    io.Fonts->AddFontDefault(&config);
    unsigned char* pixels; int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    auto sim = ship::Simulation::NewVoyage(ship::Catalog::LoadFromAssets("assets"));
    ship::ShellView view;
    view.Configure(1280, 720, ship::Theme::ById("classic"));
    Require(sim.StageJob("bo", "maintenance"), "Could not stage source crew job");
    Require(sim.ToggleOrder("rell", "doc"), "Could not stage source order");
    ship::OpenTask(sim, "room", "quarters", true);
    Frame(view, sim);
    const auto initialTitleY = FindText("Crew Quarters").y;
    const auto initialBandY = FindText("Assignments").y;
    const auto viewport = view.LayoutNow().content;
    Require(initialBandY > viewport.y && initialBandY < viewport.Bottom() - 20,
            "Assignments is outside a windowed viewport");
    view.Scroll(viewport.x + 20, viewport.y + 20, -3);
    Frame(view, sim);
    Require(FindText("Crew Quarters").y < initialTitleY, "Wheel down must move the body UP");
    Require(std::abs(FindText("Assignments").y - initialBandY) < 1,
            "Scrolling the order sheet moved its pinned commit band");

    const auto quarters = ship::BuildQuartersScreen(sim);
    const auto commitText = FindText(quarters.screen.progress.front().text);
    Require(commitText.y >= initialBandY && commitText.y + commitText.size < viewport.Bottom(),
            "Windowed commit action is not visible");
    Require(std::abs(commitText.x + MeasureTextEx({}, commitText.text.c_str(), commitText.size, 1).x / 2 -
                     (viewport.x + viewport.w / 2)) < 2, "Assignments commit button/label is not centered");
    const auto beforeCommit = sim.ActionCount();
    view.Click(sim, commitText.x + 2, commitText.y + 2, ship::HitButton::Left, 0);
    Require(sim.ActionCount() == beforeCommit + 1 && sim.FindOngoing("crew-assign") != nullptr,
            "Pinned commit click did not reach the source order dispatcher");
    Frame(view, sim);
    RequireMark(commitText.x + 2, commitText.y + 2);
    sim.PerformAction("advance assignments");
    Frame(view, sim);
    Require(std::any_of(rects.begin(), rects.end(), [](const auto& r) {
      return r.color.r == 223 && r.color.g == 240 && r.color.b == 204 && r.w > 0;
    }), "Pinned assignment task did not paint its source green fill");

    // Exercise the actual native sidebar click handler, not a direct spOpen edit.
    const auto side = view.LayoutNow().sidepanel;
    auto hits = ship::SideHits(sim, side, view.LayoutNow().sidefoot, view.CurrentTheme().LineHeight());
    auto vessel = std::find_if(hits.begin(), hits.end(), [](const auto& hit) { return hit.header && hit.key == "vessel"; });
    Require(vessel != hits.end(), "Vessel header missing");
    view.Click(sim, vessel->box.x + 5, vessel->box.y + 5, ship::HitButton::Left, 0);
    Require(sim.State().spOpen.at("vessel"), "Native Vessel click did not expand its information");
    Frame(view, sim);
    // Expand more blocks and scroll, then fold by replacing the sidebar with a
    // fresh voyage. A browser clamps the old scroll offset in this case.
    sim.MutableState().spOpen["resources"] = true;
    sim.MutableState().spOpen["crew"] = true;
    Frame(view, sim);
    view.Scroll(side.x + 30, side.y + 100, -100);
    Frame(view, sim);
    sim = ship::Simulation::NewVoyage(sim.World());
    Frame(view, sim);
    view.Click(sim, vessel->box.x + 5, vessel->box.y + 5, ship::HitButton::Left, 0);
    Require(sim.State().spOpen.at("vessel"), "Stale sidebar scroll hid Vessel after a new voyage");

    ship::OpenTask(sim, "room", "o2", true);
    sim.StartCanisterInstall(0); sim.PerformAction("probe");
    Frame(view, sim);
    const auto green = std::find_if(rects.begin(), rects.end(), [](const auto& r) {
      return r.color.r == 223 && r.color.g == 240 && r.color.b == 204 && r.w > 0;
    });
    Require(green != rects.end(), "Live filter-slot progress did not paint the HTA green fill");
    Require(sim.StartCanisterInstall(1) && sim.StartCanisterInstall(2), "Could not seat probe bank");
    sim.PerformAction("seating"); sim.PerformAction("seating");
    Require(sim.StartFilterBank(), "Could not start probe bank");
    Frame(view, sim);
    Require(std::any_of(rects.begin(), rects.end(), [](const auto& r) {
      return r.color.r == 127 && r.color.g == 145 && r.color.b == 97 && r.w == 544;
    }), "Central cabin-air healthy bar was not submitted to the renderer");

    sim.MutableState().docked = true;
    sim.MutableMap().here = "tolme";
    sim.MutableState().shiplets = 10000;
    ship::OpenTask(sim, "station", "tolme", true);
    Frame(view, sim);
    const auto fuel = std::find_if(texts.begin(), texts.end(), [](const auto& t) {
      return t.text.find("Fuel cell") == 0;
    });
    Require(fuel != texts.end(), "Station purchase label not painted");
    const auto point = *fuel;
    const double beforeBuy = sim.ActionCount();
    view.Click(sim, point.x + 2, point.y + 2, ship::HitButton::Left, 0);
    Frame(view, sim);
    Require(sim.ActionCount() == beforeBuy + 1, "Native station click failed to buy");
    FindText("COOLDOWN 5");
    Require(std::any_of(rects.begin(), rects.end(), [](const auto& r) {
      return r.color.r == 126 && r.color.g == 198 && r.color.b == 63 && r.w == 11 && r.h == 2;
    }), "HTA 654–659 action mark is not the green 11 by 2 bar");
    view.Click(sim, point.x + 2, point.y + 2, ship::HitButton::Left, 0);
    Require(sim.ActionCount() == beforeBuy + 1, "Locked station row remained clickable");

    sim.FindCrew("jax")->health = 25;
    sim.FindCrew("jax")->morale = 55;
    sim.FindCrew("jax")->fatigue = 80;
    sim.FindCrew("jax")->hunger = 90;
    ship::OpenTask(sim, "crew", "jax", true);
    Frame(view, sim);
    FindText("Health");
    Require(std::any_of(rects.begin(), rects.end(), [](const auto& r) {
      return r.color.r == 176 && r.color.g == 90 && r.color.b == 66 && r.h == 5 && r.w > 0;
    }), "Critical health/vital bars did not paint the source red");
    Require(std::any_of(rects.begin(), rects.end(), [](const auto& r) {
      return r.color.r == 192 && r.color.g == 138 && r.color.b == 62 && r.h == 5 && r.w > 0;
    }), "Intermediate morale did not paint the source amber");
    sim.FindCrew("jax")->health = 75;
    Frame(view, sim);
    FindText("75%  Excellent");

    ship::OpenTask(sim, "room", "engine", true);
    Frame(view, sim);
    Require(std::any_of(rects.begin(), rects.end(), [](const auto& r) {
      return r.color.r == 239 && r.color.g == 232 && r.color.b == 220 && r.w == 576 && r.h == 10;
    }), "Engine pipe used a generic palette instead of HTA .mach-pipe");

    // Bottom anchoring, measured wrapping, centered rows/labels, and free body scrolling.
    for (const int windowWidth : {1180, 1280}) {
      for (const auto& target : std::vector<std::pair<std::string, std::string>>{{"nav", "main"}, {"room", "engine"}, {"room", "o2"}}) {
        auto check = ship::Simulation::NewVoyage(sim.World());
        ship::ShellView shell;
        shell.Configure(windowWidth, 720, ship::Theme::ById("classic"));
        ship::OpenTask(check, target.first, target.second, true);
        Frame(shell, check);
        const auto band = FindBand(shell);
        RequireCenteredButtons(shell, band);
        const auto actions = check.ActionCount();
        const auto c = shell.LayoutNow().content;
        shell.Scroll(c.x + 20, c.y + 20, -100);
        Frame(shell, check);
        const auto scrolledBand = FindBand(shell);
        Require(std::abs(band.y - scrolledBand.y) < 1, "Body scrolling moved pinned action band");
        RequireCenteredButtons(shell, scrolledBand);
        Require(check.ActionCount() == actions, "UI scrolling spent an action");
      }
    }

    // Real painter geometry: fixed centered instruments with non-overlapping panels.
    for (const int width : {1180, 1280, 2200}) {
      for (const auto& target : std::vector<std::pair<std::string, ship::Area>>{
          {"engine", {0, 0, 650, 390}}, {"o2", {0, 0, 600, 360}}, {"mess", {0, 0, 600, 360}}}) {
        auto check = ship::Simulation::NewVoyage(sim.World());
        ship::ShellView shell;
        shell.Configure(width, 720, ship::Theme::ById("classic"));
        ship::OpenTask(check, "room", target.first, true);
        Frame(shell, check);
        const auto c = shell.LayoutNow().content;
        const auto scene = std::find_if(rects.begin(), rects.end(), [&](const auto& r) {
          return r.w == target.second.w && r.h == target.second.h;
        });
        Require(scene != rects.end(), "Fixed-size instrument frame missing");
        Require(std::abs(scene->x + scene->w / 2 - (c.x + c.w / 2)) < 2, "Instrument is not centered in its viewport");
        if (target.first == "engine") {
          for (const auto* title : {"Engine Condition", "Engineering Crew"}) {
            const auto text = FindText(title);
            const bool outsideHorizontally = text.x < scene->x || text.x >= scene->x + scene->w;
            Require(outsideHorizontally || text.y >= scene->y + scene->h, "Side panel painted over the fixed machine");
          }
        }
      }
    }
    for (const int width : {1180, 1280, 2200}) {
      auto check = ship::Simulation::NewVoyage(sim.World());
      ship::ShellView shell;
      shell.Configure(width, 720, ship::Theme::ById("classic"));
      ship::OpenTask(check, "room", "quarters", true);
      Frame(shell, check);
      const auto hall = std::find_if(rects.begin(), rects.end(), [](const auto& r) { return r.w == 560 && r.h == 300; });
      Require(hall != rects.end(), "Crew hall frame missing");
      const auto c = shell.LayoutNow().content;
      Require(std::abs(hall->x + hall->w / 2 - (c.x + c.w / 2)) < 2, "Crew hall is not centered");
    }
    // Wide capped maps center in their middle column. Long scan content takes
    // real height away from the map instead of relying on a 300px allowance.
    {
      auto catalog = sim.World();
      float shortMapH = 0;
      for (const bool longScan : {false, true}) {
        if (longScan) for (auto& body : catalog.bodies) if (body.id == "kessler") body.scan.assign(60, "Measured scan row");
        auto check = ship::Simulation::NewVoyage(catalog);
        check.MutableState().navTarget = "kessler";
        check.MutableState().scanned["kessler"] = true;
        ship::ShellView shell;
        shell.Configure(2600, 1400, ship::Theme::ById("classic"));
        ship::OpenTask(check, "nav", "main", true);
        Frame(shell, check);
        const auto map = std::find_if(rects.begin(), rects.end(), [](const auto& r) { return r.w == 1100 && r.h >= 240 && r.h <= 500; });
        Require(map != rects.end(), "Capped chart missing");
        const auto c = shell.LayoutNow().content;
        const float middleLeft = c.x + 8 + 214 + 12, middleRight = c.Right() - 8 - 252 - 12;
        Require(std::abs(map->x + map->w / 2 - (middleLeft + middleRight) / 2) < 2, "Capped chart is not centered in its column");
        if (!longScan) shortMapH = map->h;
        else Require(map->h < shortMapH, "Wrapped scan panel did not affect measured chart height");
        Require(check.Map().vw == 638 && check.Map().vh == 278, "Approved logical map viewport was changed");
      }
    }

    // The feed owns its wheel/drag offset; the surrounding Home column must not move.
    {
      auto check = ship::Simulation::NewVoyage(sim.World());
      check.MutableState().feed.clear();
      for (int i = 0; i < 120; ++i) check.MutableState().feed.push_back({"", "Feed-probe-" + std::to_string(i)});
      ship::ShellView shell;
      shell.Configure(1180, 720, ship::Theme::ById("classic"));
      Frame(shell, check);
      const auto c = shell.LayoutNow().content;
      shell.Scroll(c.x + 20, c.y + 20, -100);
      Frame(shell, check);
      const auto clip = scissors.back();
      Require(clip.y >= c.y && clip.Bottom() <= c.Bottom() + 1, "Feed scissor is not in screen coordinates");
      const float x = clip.x + 20, y = clip.y + 20;
      FindText("Feed-probe-119");
      const auto actions = check.ActionCount();
      shell.Scroll(x, y, -10000);
      Frame(shell, check);
      FindText("Feed-probe-0");
      Require(std::abs(scissors.back().y - clip.y) < 1, "Feed wheel moved the surrounding Home column");
      shell.Scroll(x, y, 10000);
      Frame(shell, check);
      FindText("Feed-probe-119");
      const auto p = shell.CurrentTheme().palette.borderDark;
      const auto thumb = std::find_if(rects.begin(), rects.end(), [&](const auto& r) {
        return r.w == 8 && r.x > c.Right() - 24 && r.color.r == p.r && r.color.g == p.g && r.color.b == p.b;
      });
      Require(thumb != rects.end(), "Feed scrollbar thumb is missing");
      const auto handle = *thumb;
      shell.Drag(check, handle.x + 2, handle.y + 2, 0, 0, true, true);
      shell.Drag(check, handle.x + 2, clip.Bottom() - 2, 0, 100, true, false);
      shell.Drag(check, handle.x + 2, clip.Bottom() - 2, 0, 0, false, false);
      Frame(shell, check);
      FindText("Feed-probe-0");
      Require(check.ActionCount() == actions, "Feed wheel/drag advanced the simulation");
      // Log arrival resets only the inner feed, paints HH:MM:SS, and is free.
      check.LogLine("Newest feed probe");
      const auto stamp = check.State().feed.back().stamp;
      Frame(shell, check);
      FindText(stamp + "  Newest feed probe");
      Require(std::abs(scissors.back().y - clip.y) < 1, "New activity shifted the surrounding Home column");
      Require(check.ActionCount() == actions, "Cosmetic log update spent an action");
      // Shortened history clamps safely, and long entries wrap within the feed.
      check.MutableState().feed = {{"", std::string(300, 'W')}};
      Frame(shell, check);
      int wrappedRows = 0;
      for (const auto& text : texts) if (!text.text.empty() && text.text.find_first_not_of('W') == std::string::npos) {
        ++wrappedRows;
        Require(text.x + MeasureTextEx({}, text.text.c_str(), text.size, 1).x < clip.Right() - 8, "Feed text overflowed its scrollbar gutter");
      }
      Require(wrappedRows > 1, "Long feed entry did not wrap");
    }

    // Confirmation is still required, cancel is free, and accepting dispatches the original purchase.
    {
      auto check = ship::Simulation::NewVoyage(sim.World());
      check.MutableState().docked = true;
      check.MutableMap().here = "tolme";
      check.MutableState().shiplets = 10000;
      check.MutableState().options.confirmRisk = true;
      ship::ShellView shell;
      shell.Configure(1180, 720, ship::Theme::ById("classic"));
      ship::OpenTask(check, "station", "tolme", true);
      const auto actions = check.ActionCount();
      const auto locatePurchase = [&]() {
        for (int i = 0; i < 100; ++i) {
          Frame(shell, check);
          const auto c = shell.LayoutNow().content;
          const auto it = std::find_if(texts.begin(), texts.end(), [](const auto& t) { return t.text.find("Buy larger ship") == 0; });
          Require(it != texts.end(), "Permanent ship purchase is missing");
          if (it->y >= c.y + 20 && it->y < c.Bottom() - 180) return *it;
          shell.Scroll(c.x + 20, c.y + 20, it->y >= c.Bottom() - 180 ? -1 : 1);
        }
        throw std::runtime_error("Could not scroll to permanent ship purchase");
      };
      auto purchase = locatePurchase();
      shell.Click(check, purchase.x + 2, purchase.y + 2, ship::HitButton::Left, 0);
      Frame(shell, check);
      RequireCenteredButtons(shell, FindBand(shell));
      Require(check.ActionCount() == actions, "Opening confirmation spent an action");
      const auto no = FindText("Keep as it is");
      shell.Click(check, no.x + 2, no.y + 2, ship::HitButton::Left, 0);
      Require(check.ActionCount() == actions && check.State().shipClass == "wren", "Cancel changed the ship or simulation");
      purchase = locatePurchase();
      shell.Click(check, purchase.x + 2, purchase.y + 2, ship::HitButton::Left, 0);
      Frame(shell, check);
      const auto yes = FindText("Yes, do it");
      shell.Click(check, yes.x + 2, yes.y + 2, ship::HitButton::Left, 0);
      Require(check.ActionCount() == actions + 1 && check.State().shipClass == "kestrel", "Pinned confirmation lost the original purchase dispatcher");
      Frame(shell, check);
      RequireMark(yes.x + 2, yes.y + 2);
    }

    // A positive FREE room click also gets feedback: it is not limited to spent actions.
    {
      auto check = ship::Simulation::NewVoyage(sim.World());
      ship::ShellView shell;
      shell.Configure(1180, 720, ship::Theme::ById("classic"));
      Frame(shell, check);
      ship::FxRng rng;
      const auto idle = ship::BuildIdleScreen(check, shell.LayoutNow(), shell.CurrentTheme(), false, 300, rng);
      const auto& room = idle.diagram.rooms.front();
      const float x = idle.stageArea.x + room.box.x + 2, y = idle.stageArea.y + room.box.y + 2;
      const auto actions = check.ActionCount();
      shell.Click(check, x, y, ship::HitButton::Left, 0);
      Frame(shell, check);
      Require(check.ActionCount() == actions && shell.HasSelection(), "Room briefing was not a free positive click");
      RequireMark(x, y);
      Frame(shell, check, 500);
      Require(std::any_of(rects.begin(), rects.end(), [&](const auto& r) {
        return r.w == 11 && r.h == 2 && r.color.r == 126 && r.color.a > 0 && r.color.a < 255 && r.y < y - 1;
      }), "Positive click mark did not rise and fade");
      shell.ClosePopup();
    }

    // Exercise the real pointer paths and animation, not only their models.
    {
      auto audit = ship::Simulation::NewVoyage(ship::Catalog::LoadFromAssets("assets"));
      ship::ShellView cockpit;
      io.DisplaySize = ImVec2(1280, 1600);
      cockpit.Configure(1280, 1600, ship::Theme::ById("classic"));
      Frame(cockpit, audit, 1000);
      const auto life = std::find_if(audit.Infos().begin(), audit.Infos().end(),
                                    [](const auto& i) { return i.id == "life"; });
      Require(life != audit.Infos().end(), "No source life info card");
      const auto card = FindText(life->label);
      cockpit.Click(audit, card.x + 2, card.y + 2, ship::HitButton::Right, 1000);
      Require(ship::FindTab(audit.State(), "info:life") != nullptr &&
              ship::FindTab(audit.State(), "room:o2") == nullptr && audit.State().activeTab == "home",
              "Life right-click bypassed the source information task");
      Require(audit.ActionCount() == 0, "Reading life info ticked the clock");

      // Same roster, different employment: actual painted dot must eventually
      // drift into the new compartment, then into the unemployment corridor.
      auto* hand = audit.FindCrew("jax");
      Require(hand != nullptr, "No engineer in dot fixture");
      hand->colour = "#010203";
      hand->job = "cook"; hand->room = "mess";
      for (int i = 1; i <= 450; ++i) Frame(cockpit, audit, 1000 + i * 1000);
      ship::FxRng rng;
      auto idle = ship::BuildIdleScreen(audit, cockpit.LayoutNow(), cockpit.CurrentTheme(), false, rng);
      const auto* room = ship::ActivePlan(audit).FindRoom("mess");
      Require(room != nullptr, "No source mess room");
      auto paintedHand = [&]() {
        const auto it = std::find_if(rects.begin(), rects.end(), [](const RectCall& r) {
          return r.w == 15 && r.h == 15 && r.color.r == 1 && r.color.g == 2 && r.color.b == 3;
        });
        Require(it != rects.end(), "Crew indicator was not painted"); return *it;
      };
      auto dot = paintedHand();
      Require(dot.x >= idle.stageArea.x + room->box.x && dot.x < idle.stageArea.x + room->box.x + room->box.w &&
              dot.y >= idle.stageArea.y + room->box.y && dot.y < idle.stageArea.y + room->box.y + room->box.h,
              "Same-size roster kept the crew indicator in its previous workplace");
      hand->job.clear();
      for (int i = 451; i <= 900; ++i) Frame(cockpit, audit, 1000 + i * 1000);
      dot = paintedHand();
      const auto corridor = ship::ActivePlan(audit).corridor;
      Require(dot.y >= idle.stageArea.y + corridor.y && dot.y < idle.stageArea.y + corridor.y + corridor.h,
              "Laid-off indicator did not migrate into the corridor");
      const auto before = dot;
      for (int i = 901; i <= 960; ++i) Frame(cockpit, audit, 1000 + i * 1000);
      dot = paintedHand();
      Require(dot.x != before.x, "Corridor crew indicator never drifts");
      Require(audit.ActionCount() == 0 && audit.State().wearTick == 0,
              "Cosmetic crew movement advanced the action clock");

      ship::OpenTask(audit, "crew", "missing", true);
      Frame(cockpit, audit, 970000);
      FindText("No longer aboard"); FindText("crew missing");
      ship::OpenTask(audit, "event", "missing", true);
      Frame(cockpit, audit, 971000);
      FindText("No longer aboard"); FindText("event missing");

      ship::OpenTask(audit, "nav", "main", true);
      Frame(cockpit, audit, 972000);
      const auto initialChart = ship::BuildNavChart(audit);
      const auto labelBody = std::find_if(initialChart.bodies.begin(), initialChart.bodies.end(),
                                         [](const auto& b) { return b.id == "halvan"; });
      Require(labelBody != initialChart.bodies.end(), "No Halvan chart label");
      const auto mapBox = std::find_if(rects.begin(), rects.end(), [&](const RectCall& r) {
        const auto c = cockpit.CurrentTheme().palette.chart;
        return r.w > 200 && r.h > 100 && r.color.r == c.r && r.color.g == c.g && r.color.b == c.b;
      });
      Require(mapBox != rects.end(), "No painted map for label fixture");
      cockpit.Click(audit, mapBox->x + labelBody->labelX * mapBox->w / audit.Map().vw + 20,
                     mapBox->y + labelBody->labelY * mapBox->h / audit.Map().vh + 3,
                     ship::HitButton::Left, 972000);
      Require(audit.State().navTarget == "halvan", "Visible map label did not select its body");
      Require(audit.ActionCount() == 0, "Selecting a label spent an action");
      Require(audit.StartTravel(), "Could not start ring-interpolation fixture");
      auto* trip = audit.FindOngoing("travel");
      Require(trip != nullptr, "No travel fixture"); trip->left = trip->total * .5;
      Frame(cockpit, audit, 973000);
      const auto chart = ship::BuildNavChart(audit);
      const auto chartRect = std::find_if(rects.begin(), rects.end(), [&](const RectCall& r) {
        const auto c = cockpit.CurrentTheme().palette.chart;
        return r.w > 200 && r.h > 100 && r.color.r == c.r && r.color.g == c.g && r.color.b == c.b;
      });
      Require(chartRect != rects.end(), "No painted navigation chart");
      const float ringX = chartRect->x + chart.marker.x * chartRect->w / audit.Map().vw;
      const float ringY = chartRect->y + chart.marker.y * chartRect->h / audit.Map().vh;
      const auto actions = audit.ActionCount();
      cockpit.Click(audit, ringX, ringY, ship::HitButton::Left, 973000);
      Require(audit.State().navTarget == audit.HomeBody()->id,
              "Interpolated ship-position ring did not select home");
      Require(audit.ActionCount() == actions, "Selecting the ship ring advanced travel");
    }
    {
      auto tabs = ship::Simulation::NewVoyage(ship::Catalog::LoadFromAssets("assets"));
      ship::ShellView cockpit;
      io.DisplaySize = ImVec2(1280, 720);
      cockpit.Configure(1280, 720, ship::Theme::ById("classic"));
      auto hasColour = [](ImU32 colour) {
        const auto* data = ImGui::GetDrawData();
        for (int i = 0; i < data->CmdListsCount; ++i)
          for (const auto& vertex : data->CmdLists[i]->VtxBuffer)
            if (vertex.col == colour) return true;
        return false;
      };
      ship::OpenTask(tabs, "crew", "rell", true);
      Frame(cockpit, tabs, 1000);
      Require(hasColour(IM_COL32(233,214,184,255)), "Active new tab did not receive HTA warm glow");
      Frame(cockpit, tabs, 1150);
      Require(hasColour(IM_COL32(233,214,184,255)), "Tab glow did not hold 160 ms");
      Frame(cockpit, tabs, 2000);
      Require(!hasColour(IM_COL32(233,214,184,255)), "Tab glow never faded");
      Frame(cockpit, tabs, 2700);
      tabs.MutableState().reducedMotion = true;
      ship::OpenTask(tabs, "crew", "jax", true);
      Frame(cockpit, tabs, 3000);
      Require(!hasColour(IM_COL32(233,214,184,255)), "Reduced motion did not suppress tab glow");
      ship::SwitchTab(tabs, "home"); Frame(cockpit, tabs, 4000);
      for (int i = 0; i < 18; ++i) ship::OpenTask(tabs, "crew", "missing" + std::to_string(i), false);
      Frame(cockpit, tabs, 5000);
      Require(tabs.State().activeTab == "home", "Background tab reveal stole focus");
      cockpit.Click(tabs, cockpit.LayoutNow().tabStrip.w - 12, 12, ship::HitButton::Left, 5000);
      Require(ship::FindTab(tabs.State(), "crew:missing17") == nullptr,
              "New background tab was not revealed with its close target");
      ship::OpenTask(tabs, "crew", "missing0", false);
      Frame(cockpit, tabs, 6000);
      const auto* tab = ship::FindTab(tabs.State(), "crew:missing0");
      Require(tab != nullptr, "Existing background tab vanished");
      const float closeX = ImGui::CalcTextSize((tab->kindLabel + "  " + tab->title).c_str()).x + 20;
      cockpit.Click(tabs, closeX, 12, ship::HitButton::Left, 6000);
      Require(ship::FindTab(tabs.State(), "crew:missing0") == nullptr,
              "Repeated background open did not reveal its existing tab");
      Require(tabs.ActionCount() == 0, "Tab pulse/reveal/close advanced the clock");
    }
    io.DisplaySize = ImVec2(1280, 720);

    Texture2D title{}; title.id = 1; title.width = 1672; title.height = 941;
    view.SetTitleTexture(&title);
    view.ShowTitle(false);
    Frame(view, sim);
    Require(textureCalls == 1, "Main menu did not submit the title image");
    // HTA 1412–1427 overrides title controls for every dark palette.
    const char* darkIds[] = {"void", "aurora", "nebula", "ember"};
    for (const auto* id : darkIds) {
      const auto theme = ship::Theme::ById(id);
      view.SetTheme(theme);
      Frame(view, sim);
      const auto expected = theme.palette.titleMenu;
      Require(std::any_of(rects.begin(), rects.end(), [&](const auto& r) {
        return r.color.r == expected.r && r.color.g == expected.g && r.color.b == expected.b && r.w > 100;
      }), "Title renderer ignored the source dark-theme menu background");
    }
    view.SetTheme(ship::Theme::ById("classic"));
    Frame(view, sim);
    const auto options = FindText("OPTIONS");
    view.Click(sim, options.x + 2, options.y + 2, ship::HitButton::Left, 0);
    Frame(view, sim);
    Require(view.Panel() == ship::TitlePanel::Settings && textureCalls == 1,
            "Options replaced the title image instead of opening a sheet over it");
    ImGui::DestroyContext();
    std::cout << "PASS: real Draw.cpp scroll direction, windowed Assignments, Vessel input/clamp, green action/air fills, visible station cooldown, source health colours/live values, literal engine pipe, screen-space green click mark/glow after scrolling, pinned/centered measured action bands at 1180/1280, independent feed wheel/drag/wrap/clamp plus timestamp/reset, centered fixed machine/crew scenes with panel reflow, measured chart height and wide centering, preserved purchase confirmation, free-click feedback/rise/fade, four dark title menus, title art retained behind Options; source crew relocation/corridor drift, life-info routing, crew/event GONE rendering, map-label and moving-ring selection, active-tab glow/reduced-motion, new/existing background-tab reveal; all text >=18px\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "draw probe: " << error.what() << '\n'; return 1;
  }
}
