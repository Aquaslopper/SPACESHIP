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
struct TextCall { std::string text; float x, y, size; };
struct RectCall { float x, y, w, h; Color color; };
std::vector<TextCall> texts;
std::vector<RectCall> rects;
float cameraY = 0;
int textureCalls = 0;
void Require(bool ok, const char* message) {
  if (!ok) { throw std::runtime_error(message); }
}
const TextCall& FindText(const std::string& value) {
  const auto it = std::find_if(texts.begin(), texts.end(), [&](const auto& t) { return t.text == value; });
  if (it == texts.end()) { throw std::runtime_error("Not painted: " + value); }
  return *it;
}
void Frame(ship::ShellView& view, ship::Simulation& sim) {
  texts.clear(); rects.clear(); textureCalls = 0;
  ImGui::NewFrame(); view.Draw(sim, 0); ImGui::Render();
  for (const auto& text : texts) { Require(text.size >= 18, "Native text fell below 18px"); }
}
}

// Match raylib's camera transform: screen = (world - target) * zoom + offset.
void BeginMode2D(Camera2D c) { cameraY = c.offset.y - c.target.y * c.zoom; }
void EndMode2D() { cameraY = 0; }
Color Fade(Color color, float alpha) { color.a = static_cast<unsigned char>(255 * std::clamp(alpha, 0.0f, 1.0f)); return color; }
void ClearBackground(Color) {}
void BeginScissorMode(int, int, int, int) {}
void EndScissorMode() {}
Vector2 MeasureTextEx(Font, const char* text, float size, float spacing) {
  auto p = ImGui::GetFont()->CalcTextSizeA(size, 100000, 0, text);
  return {p.x + std::max(0, static_cast<int>(std::string(text).size()) - 1) * spacing, p.y};
}
void DrawTextEx(Font, const char* text, Vector2 p, float size, float, Color) {
  texts.push_back({text, p.x, p.y + cameraY, size});
}
void DrawRectangle(int x, int y, int w, int h, Color color) {
  rects.push_back({static_cast<float>(x), y + cameraY, static_cast<float>(w), static_cast<float>(h), color});
}
void DrawRectangleLines(int, int, int, int, Color) {}
void DrawRectangleLinesEx(Rectangle, float, Color) {}
void DrawRectangleRounded(Rectangle, float, int, Color) {}
void DrawRectangleRoundedLinesEx(Rectangle, float, int, float, Color) {}
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
    const auto beforeCommit = sim.ActionCount();
    view.Click(sim, commitText.x + 2, commitText.y + 2, ship::HitButton::Left, 0);
    Require(sim.ActionCount() == beforeCommit + 1 && sim.FindOngoing("crew-assign") != nullptr,
            "Pinned commit click did not reach the source order dispatcher");
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
    view.Click(sim, point.x + 2, point.y + 2, ship::HitButton::Left, 0);
    Require(sim.ActionCount() == beforeBuy + 1, "Locked station row remained clickable");

    Texture2D title{}; title.id = 1; title.width = 1672; title.height = 941;
    view.SetTitleTexture(&title);
    view.ShowTitle(false);
    Frame(view, sim);
    Require(textureCalls == 1, "Main menu did not submit the title image");
    const auto options = FindText("OPTIONS");
    view.Click(sim, options.x + 2, options.y + 2, ship::HitButton::Left, 0);
    Frame(view, sim);
    Require(view.Panel() == ship::TitlePanel::Settings && textureCalls == 1,
            "Options replaced the title image instead of opening a sheet over it");
    ImGui::DestroyContext();
    std::cout << "PASS: real Draw.cpp scroll direction, windowed Assignments, Vessel input/clamp, green action/air fills, visible station cooldown, title art retained behind Options; all text >=18px\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "draw probe: " << error.what() << '\n'; return 1;
  }
}
