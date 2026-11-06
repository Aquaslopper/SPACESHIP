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
struct EllipseCall { float x, y, rx, ry; Color color; };
std::vector<EllipseCall> ellipses;
float cameraY = 0;
int drawOrder = 0;
int textureCalls = 0;
struct TextureCall { unsigned id; Rectangle dest; Color tint; };
std::vector<TextureCall> textureDraws;
void Require(bool ok, const char* message) {
  if (!ok) { throw std::runtime_error(message); }
}
const TextCall& FindText(const std::string& value) {
  const auto it = std::find_if(texts.begin(), texts.end(), [&](const auto& t) { return t.text == value; });
  if (it == texts.end()) { throw std::runtime_error("Not painted: " + value); }
  return *it;
}
void Frame(ship::ShellView& view, ship::Simulation& sim, double nowMs = 0) {
  texts.clear(); rects.clear(); scissors.clear(); ellipses.clear(); textureDraws.clear(); textureCalls = 0; drawOrder = 0;
  // ImGui 1.92 builds glyphs lazily at each new text size. This recording
  // backend has no renderer to upload the rebuilt atlas automatically.
  unsigned char* pixels; int w, h;
  ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
  ImGui::NewFrame(); view.Draw(sim, nowMs); ImGui::Render();
  for (const auto& text : texts) {
    if (text.size >= 18) continue;
    const bool infoText = sim.State().activeTab == "home" && !text.text.empty() &&
        std::any_of(sim.Infos().begin(), sim.Infos().end(), [&](const auto& info) {
          return info.label.find(text.text) != std::string::npos || info.value.find(text.text) != std::string::npos;
        });
    Require(infoText && text.size >= 12, "Non-info native text fell below 18px");
  }
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
    if (r.order <= band.order || r.y < band.y || r.y + r.h > band.y + band.h || r.w < 150 || r.h < 30) continue;
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
void DrawEllipse(int x, int y, float rx, float ry, Color c) { ellipses.push_back({float(x), y + cameraY, rx, ry, c}); }
void DrawCircle(int, int, float, Color) {}
void DrawCircleV(Vector2, float, Color) {}
void DrawCircleLines(int, int, float, Color) {}
void DrawLine(int, int, int, int, Color) {}
void DrawLineEx(Vector2, Vector2, float, Color) {}
void DrawRing(Vector2, float, float, float, float, int, Color) {}
void DrawTexturePro(Texture2D texture, Rectangle, Rectangle dest, Vector2, float, Color color) { ++textureCalls; textureDraws.push_back({texture.id, dest, color}); }

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
    // Chart controls use their painted bounds for all reading-size steps, and
    // topmost controls take priority over map bodies behind them.
    for (int step : {0, 3}) {
      auto nav = ship::Simulation::NewVoyage(sim.World());
      nav.MutableState().options.fontStep = step;
      ship::ShellView mapView; mapView.Configure(1280, 720, ship::Theme::ById("classic"));
      ship::OpenTask(nav, "nav", "main", true);
      Frame(mapView, nav);
      const auto minus = FindText("-");
      const auto system = FindText("SYSTEM");
      auto containing = [](const TextCall& t) {
        auto it = std::find_if(rects.rbegin(), rects.rend(), [&](const RectCall& r) {
          return r.order < t.order && t.x >= r.x && t.x < r.x + r.w &&
                 t.y >= r.y && t.y + t.size <= r.y + r.h + 1;
        });
        Require(it != rects.rend(), "No control rectangle for map label");
        return *it;
      };
      const auto minusBox = containing(minus);
      const auto systemBox = containing(system);
      Require(minusBox.y + minusBox.h < systemBox.y, "Map zoom and layer controls overlap");
      const double zoom = nav.Map().zoom;
      mapView.Click(nav, minusBox.x + minusBox.w / 2, minusBox.y + minusBox.h / 2, ship::HitButton::Left, 0);
      Require(nav.Map().zoom < zoom, "Zoom-out hit missed its painted control");
      Frame(mapView, nav);
      const auto sector = FindText("SECTOR");
      mapView.Click(nav, sector.x + 2, sector.y + 2, ship::HitButton::Left, 0);
      Require(nav.Map().layer == "sector", "Map layer hit missed its painted control");
    }

    // Free handbook selection and reveal do not produce action feedback; the
    // selected article stays in view as the index scrolls and its controls
    // remain clickable at the translated position.
    {
      auto world = ship::Catalog::LoadFromAssets("assets");
      world.wiki.clear();
      world.wiki.push_back({"first", "GUIDE", "First entry", "A short handbook article.", "", "", false});
      world.wiki.push_back({"spoiler", "GUIDE", "Test spoiler", "The hidden article.", "", "", true});
      for (int i = 0; i < 35; ++i)
        world.wiki.push_back({"entry" + std::to_string(i), "GUIDE", "Extra entry " + std::to_string(i), "More text.", "", "", false});
      auto handbook = ship::Simulation::NewVoyage(world);
      ship::ShellView reader; reader.Configure(1280, 720, ship::Theme::ById("classic"));
      ship::OpenTask(handbook, "wiki", "main", true);
      Frame(reader, handbook);
      const auto pick = FindText("Test spoiler");
      reader.Click(handbook, pick.x + 2, pick.y + 2, ship::HitButton::Left, 0);
      Frame(reader, handbook);
      FindText("Reveal the spoiler entry");
      Require(std::none_of(rects.begin(), rects.end(), [](const RectCall& r) {
        return r.color.r == 126 && r.color.g == 198 && r.color.b == 63 && r.w == 11 && r.h == 2;
      }), "Free wiki selection displayed a positive action mark");
      const auto content = reader.LayoutNow().content;
      reader.Scroll(content.Right() - 10, content.y + 50, -20);
      Frame(reader, handbook);
      const auto pinned = FindText("Test spoiler");
      // Index and article both contain the title; find the right-hand one.
      const auto articleTitle = std::find_if(texts.begin(), texts.end(), [&](const TextCall& t) {
        return t.text == "Test spoiler" && t.x > content.x + content.w / 2 && t.y >= content.y && t.y < content.Bottom();
      });
      (void)pinned;
      Require(articleTitle != texts.end(), "Wiki article did not follow the index scroll");
      const auto reveal = FindText("Reveal the spoiler entry");
      Require(reveal.y >= content.y && reveal.y < content.Bottom(), "Sticky wiki control is not visible");
      reader.Click(handbook, reveal.x + 2, reveal.y + 2, ship::HitButton::Left, 0);
      Frame(reader, handbook);
      FindText("The hidden article.");
      Require(handbook.ActionCount() == 0, "Free wiki navigation spent an action");
      Require(std::none_of(rects.begin(), rects.end(), [](const RectCall& r) {
        return r.color.r == 126 && r.color.g == 198 && r.color.b == 63 && r.w == 11 && r.h == 2;
      }), "Free wiki reveal displayed a positive action mark");

      // A tall article scrolls through its bottom before the sticky position is
      // reached, rather than trapping the end below the viewport.
      world.wiki[0].text.clear();
      for (int i = 0; i < 300; ++i) world.wiki[0].text += "Long article text continues. ";
      world.wiki[0].text += "END-OF-ARTICLE";
      auto longHandbook = ship::Simulation::NewVoyage(world);
      ship::ShellView longReader; longReader.Configure(1280, 720, ship::Theme::ById("classic"));
      ship::OpenTask(longHandbook, "wiki", "main", true);
      Frame(longReader, longHandbook);
      longReader.Scroll(content.Right() - 10, content.y + 50, -1000);
      Frame(longReader, longHandbook);
      const auto end = std::find_if(texts.begin(), texts.end(), [](const TextCall& t) {
        return t.text.find("END-OF-ARTICLE") != std::string::npos;
      });
      Require(end != texts.end() && end->y >= content.y && end->y + end->size < content.Bottom(),
              "Long wiki article bottom is unreachable");
    }
    // Real Draw.cpp callouts route all Wren rooms to their existing handlers.
    // Cosmetic time, browsing and view switches must not advance the clock or
    // a lock; the original deck still exposes its actual crew indicators.
    {
      Texture2D concept{77, 1340, 420, 1, 7};
      auto artShip = ship::Simulation::NewVoyage(sim.World());
      Require(ship::PressNavAction(artShip, "stand-watch"), "Could not start clock guard fixture");
      const auto lockKey = ship::ActionCooldownKey(artShip, "command:stand-watch-advance-ongoing-work:0");
      const auto lock = ship::CooldownRemaining(artShip, lockKey);
      Require(lock > 0, "Clock guard has no active lock");
      const auto actions = artShip.ActionCount();
      const auto wear = artShip.State().wearTick;
      ship::ShellView hull;
      io.DisplaySize = ImVec2(1280, 1200);
      hull.Configure(1280, 1200, ship::Theme::ById("classic"));
      hull.SetWrenExteriorTexture(&concept);
      Frame(hull, artShip, 1000);
      Require(std::any_of(textureDraws.begin(), textureDraws.end(), [](const TextureCall& t) { return t.id == 77; }),
              "Wren exterior concept was not painted");
      for (const auto& room : ship::ActivePlan(artShip).rooms) {
        Frame(hull, artShip, 1200);
        const auto label = FindText(room.name);
        Require(label.y >= hull.LayoutNow().content.y && label.y + label.size < hull.LayoutNow().content.Bottom(),
                "Wren callout is not visible in the tall fixture");
        hull.Click(artShip, label.x + 2, label.y + 2, ship::HitButton::Right, 1200);
        Require(ship::FindTab(artShip.State(), "room:" + room.id) != nullptr,
                "Exterior callout did not open its corresponding room");
      }
      Frame(hull, artShip, 50000);
      const auto engine = FindText("Engine Room");
      hull.Move(engine.x + 2, engine.y + 2);
      Frame(hull, artShip, 51000);
      Require(!ellipses.empty(), "Hovering a room did not light its exterior anchor");
      artShip.MutableState().reducedMotion = true;
      Frame(hull, artShip, 52000);
      const auto glow = ellipses.back().color.a;
      Frame(hull, artShip, 100000);
      Require(!ellipses.empty() && ellipses.back().color.a == glow,
              "Reduced motion did not make the hover highlight static");
      const auto deckToggle = FindText("Deck plan / crew");
      hull.Click(artShip, deckToggle.x + 2, deckToggle.y + 2, ship::HitButton::Left, 100000);
      auto* person = artShip.FindCrew("jax");
      Require(person != nullptr, "No crew fixture");
      person->colour = "#010203";
      Frame(hull, artShip, 100100);
      FindText("Exterior view");
      Require(std::none_of(textureDraws.begin(), textureDraws.end(), [](const TextureCall& t) { return t.id == 77; }),
              "Deck-plan switch did not restore the original diagram");
      const auto dot = std::find_if(rects.begin(), rects.end(), [](const RectCall& r) {
        return r.w == 15 && r.h == 15 && r.color.r == 1 && r.color.g == 2 && r.color.b == 3;
      });
      Require(dot != rects.end(), "Deck plan lost the crew indicator");
      hull.Click(artShip, dot->x + 3, dot->y + 3, ship::HitButton::Right, 100100);
      Require(ship::FindTab(artShip.State(), "crew:jax") != nullptr, "Crew dot stopped opening its record");
      const auto exteriorToggle = FindText("Exterior view");
      hull.Click(artShip, exteriorToggle.x + 2, exteriorToggle.y + 2, ship::HitButton::Left, 100200);
      Frame(hull, artShip, 100300);
      const auto label = FindText("Engine Room");
      hull.Click(artShip, label.x + 2, label.y + 2, ship::HitButton::Left, 100300);
      Require(hull.HasSelection() && hull.Selection().id == "engine", "Exterior left click did not open the room briefing");
      Require(artShip.ActionCount() == actions && artShip.State().wearTick == wear &&
              ship::CooldownRemaining(artShip, lockKey) == lock,
              "Exterior browsing, view switching or wall time advanced the action clock");

      // Missing art and a different ship class retain their deck plans.
      ship::ShellView fallback; fallback.Configure(1280, 1200, ship::Theme::ById("classic"));
      Frame(fallback, artShip);
      Require(std::none_of(textureDraws.begin(), textureDraws.end(), [](const TextureCall& t) { return t.id == 77; }),
              "Missing art did not fall back to the deck plan");
      artShip.MutableState().shipClass = "kestrel";
      ship::ShellView larger; larger.Configure(1280, 1200, ship::Theme::ById("classic"));
      larger.SetWrenExteriorTexture(&concept);
      Frame(larger, artShip);
      Require(std::none_of(textureDraws.begin(), textureDraws.end(), [](const TextureCall& t) { return t.id == 77; }),
              "Wren concept was incorrectly used as the Kestrel hull");

      // On a short narrow window, a callout remains reachable after scrolling.
      auto narrowShip = ship::Simulation::NewVoyage(sim.World());
      ship::ShellView narrow; io.DisplaySize = ImVec2(900, 500);
      ship::Theme large = ship::Theme::ById("classic"); large.fontStep = 3;
      narrow.Configure(900, 500, large); narrow.SetWrenExteriorTexture(&concept);
      Frame(narrow, narrowShip);
      auto last = FindText("Recreation");
      const auto c = narrow.LayoutNow().content;
      if (last.y >= c.Bottom() - 30) {
        narrow.Scroll(c.x + 8, c.y + 40, -(last.y - (c.y + 130)) / (40.0f * large.FontPx() / 13.0f));
        Frame(narrow, narrowShip);
        last = FindText("Recreation");
      }
      Require(last.y >= c.y && last.y + last.size < c.Bottom(), "Narrow-window callout cannot be scrolled into view");
      narrow.Click(narrowShip, last.x + 2, last.y + 2, ship::HitButton::Right, 0);
      Require(ship::FindTab(narrowShip.State(), "room:r13") != nullptr,
              "Scrolled callout hit testing diverged from its drawing");
      Require(narrowShip.ActionCount() == 0, "Scrolling and browsing a callout advanced time");
      io.DisplaySize = ImVec2(1280, 720);
    }
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
    const auto salesHeading = FindText("Sell cargo and survey data");
    const auto boardHeading = FindText("Mission board");
    const auto yardHeading = FindText("Shipyard and crew");
    const auto tavernHeading = FindText("Tavern and departure");
    Require(std::abs(salesHeading.x - boardHeading.x) < 1 && boardHeading.y > salesHeading.y,
            "Mission board is not stacked beneath sales in the middle column");
    Require(std::abs(yardHeading.x - tavernHeading.x) < 1 && tavernHeading.y > yardHeading.y,
            "Tavern is not stacked beneath the shipyard in the right column");
    Require(yardHeading.x > boardHeading.x, "Station columns have the wrong order");
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
      check.MutableState().options.confirmRisk = false; // Ship purchase always confirms in the HTA.
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

    // Both the crew record and quarters expose the same priced conversation;
    // the real click path must tick once, raise morale, and respect the lock.
    {
      auto talkSim = ship::Simulation::NewVoyage(ship::Catalog::LoadFromAssets("assets"));
      talkSim.FindCrew("suri")->morale = 40;
      ship::ShellView conversation;
      conversation.Configure(1280, 1600, ship::Theme::ById("classic"));
      io.DisplaySize = ImVec2(1280, 1600);
      ship::OpenTask(talkSim, "crew", "suri", true);
      Frame(conversation, talkSim);
      const auto button = FindText("Talk to Suri - 1 action, up to +5 morale");
      const double before = talkSim.ActionCount();
      conversation.Click(talkSim, button.x + 2, button.y + 2, ship::HitButton::Left, 0);
      Require(talkSim.ActionCount() == before + 1 && talkSim.FindCrew("suri")->morale == 45,
              "Crew record conversation did not spend one action and improve morale");
      Frame(conversation, talkSim);
      const auto locked = FindText("Talk to Suri - 1 action, up to +5 morale - COOLDOWN 5");
      conversation.Click(talkSim, locked.x + 2, locked.y + 2, ship::HitButton::Left, 0);
      Require(talkSim.ActionCount() == before + 1, "Locked crew record conversation was clickable");
      ship::OpenTask(talkSim, "room", "quarters", true);
      ship::PressQuartersAction(talkSim, "select", "suri");
      Frame(conversation, talkSim);
      const auto it = std::find_if(texts.begin(), texts.end(), [](const auto& text) {
        return text.text.find("Talk to Suri") == 0;
      });
      Require(it != texts.end(), "Quarters conversation label did not paint");
      Require(std::any_of(texts.begin(), texts.end(), [](const auto& text) {
        return text.text.find("COOLDOWN 5") != std::string::npos;
      }), "Quarters did not show the conversation cooldown");
      const auto quarterLocked = *it;
      conversation.Click(talkSim, quarterLocked.x + 2, quarterLocked.y + 2, ship::HitButton::Left, 0);
      Require(talkSim.ActionCount() == before + 1, "Quarters bypassed the crew record conversation lock");
    }
    {
      auto talkSim = ship::Simulation::NewVoyage(ship::Catalog::LoadFromAssets("assets"));
      talkSim.FindCrew("suri")->morale = 40;
      ship::ShellView conversation;
      conversation.Configure(1280, 1600, ship::Theme::ById("classic"));
      io.DisplaySize = ImVec2(1280, 1600);
      ship::OpenTask(talkSim, "room", "quarters", true);
      ship::PressQuartersAction(talkSim, "select", "suri");
      Frame(conversation, talkSim);
      const auto button = FindText("Talk to Suri - 1 action, up to +5 morale");
      conversation.Click(talkSim, button.x + 2, button.y + 2, ship::HitButton::Left, 0);
      Require(talkSim.ActionCount() == 1 && talkSim.FindCrew("suri")->morale == 45,
              "Quarters conversation did not spend one action and improve morale");
    }

    // The Home painter must fill the pale warm CSS panels, not leave them on
    // the white content background. Compact information text still scales and
    // measured wraps must stay within their cards and above the feed.
    for (const std::string themeId : {"classic", "void"}) {
      for (int step : {0, 3}) {
        auto audit = ship::Simulation::NewVoyage(ship::Catalog::LoadFromAssets("assets"));
        auto& infos = const_cast<std::vector<ship::InfoRow>&>(audit.Infos());
        infos.front().value = "A particularly long location name that must wrap inside this information card";
        ship::Theme theme = ship::Theme::ById(themeId);
        theme.fontStep = step;
        ship::ShellView home;
        home.Configure(1280, 1600, theme);
        io.DisplaySize = ImVec2(1280, 1600);
        Frame(home, audit);
        ship::FxRng rng;
        auto model = ship::BuildIdleScreen(audit, home.LayoutNow(), theme, false, rng);
        const auto panel = theme.palette.panel;
        auto filledAt = [&](float x, float y) -> const RectCall& {
          const auto found = std::find_if(rects.begin(), rects.end(), [&](const auto& r) {
            return std::abs(r.x - int(x)) < 1 && std::abs(r.y - int(y)) < 1 &&
                   r.color.r == panel.r && r.color.g == panel.g && r.color.b == panel.b;
          });
          Require(found != rects.end(), "Home panel is missing its warm surface fill");
          return *found;
        };
        filledAt(model.stats.front().box.x, model.stats.front().box.y);
        filledAt(model.nav.box.x, model.nav.box.y);
        for (const auto& info : model.infos) {
          const auto& background = filledAt(info.box.x, info.box.y);
          Require(background.h > model.infos.front().box.h, "Long info did not grow the rail");
          const auto& label = FindText(info.label);
          Require(label.size == theme.HomeInfoLabelPx(), "Info label ignored its compact font size");
          Require(label.y >= background.y && label.y + label.size <= background.y + background.h,
                  "Info label escaped its background");
        }
        const auto& firstPanel = filledAt(model.infos.front().box.x, model.infos.front().box.y);
        int valueLines = 0;
        for (const auto& line : texts) {
          if (line.size != theme.HomeInfoValuePx() || line.text.empty() ||
              infos.front().value.find(line.text) == std::string::npos ||
              line.x < firstPanel.x || line.x >= firstPanel.x + firstPanel.w) continue;
          ++valueLines;
          Require(line.y >= firstPanel.y && line.y + line.size <= firstPanel.y + firstPanel.h,
                  "Wrapped info escaped its background");
          Require(line.x + MeasureTextEx({}, line.text.c_str(), line.size, 1).x <= firstPanel.x + firstPanel.w,
                  "Wrapped info crossed into another card");
        }
        Require(valueLines > 1, "Long info value was not wrapped at the compact value size");
        const auto& feedHead = FindText("Activity Feed");
        Require(feedHead.y >= firstPanel.y + firstPanel.h, "Feed overlaps the expanded info rail");
        Require(filledAt(model.feedBox.x, feedHead.y + feedHead.size + 2).w > 0,
                "Feed has no warm background");
      }
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
    // HOW TO PLAY opens the six supplied cards, not the old text sheet.
    std::array<Texture2D, 6> cards{};
    for (std::size_t i = 0; i < cards.size(); ++i) {
      cards[i].id = static_cast<unsigned>(20 + i); cards[i].width = 755; cards[i].height = 340;
      view.SetTutorialTexture(i, &cards[i]);
    }
    Frame(view, sim, 1000);
    const auto guideButton = FindText("HOW TO PLAY");
    const auto actionsBeforeGuide = sim.ActionCount();
    view.Click(sim, guideButton.x + 2, guideButton.y + 2, ship::HitButton::Left, 1000);
    Frame(view, sim, 1100);
    Require(view.Panel() == ship::TitlePanel::Guide && textureCalls == 1 && textureDraws[0].id == 20,
            "How to Play did not open the first illustrated card");
    Require(textureDraws[0].tint.a > 0 && textureDraws[0].tint.a < 255,
            "Tutorial did not animate its entrance");
    FindText("HOW TO PLAY   1 / 6");
    for (int i = 1; i < 6; ++i) {
      const auto next = FindText("NEXT");
      const double now = 1100 + i * 500;
      view.Click(sim, next.x + 2, next.y + 2, ship::HitButton::Left, now);
      Frame(view, sim, now + 400);
      Require(textureCalls == 1 && textureDraws[0].id == static_cast<unsigned>(20 + i),
              "Next card did not show the expected illustration");
    }
    FindText("HOW TO PLAY   6 / 6");
    const auto endNext = FindText("NEXT");
    view.Click(sim, endNext.x + 2, endNext.y + 2, ship::HitButton::Left, 5000);
    Frame(view, sim, 5000);
    Require(textureDraws[0].id == 25, "Next wrapped past the last card");
    view.PageTutorial(-1, 5100);
    Frame(view, sim, 5500);
    Require(textureDraws[0].id == 24, "Keyboard paging did not move back");
    view.Escape(sim);
    Frame(view, sim, 5600);
    Require(view.Panel() == ship::TitlePanel::None && textureDraws[0].id == 1,
            "Escape did not return to the main menu");
    sim.MutableState().reducedMotion = true;
    const auto guideAgain = FindText("HOW TO PLAY");
    view.Click(sim, guideAgain.x + 2, guideAgain.y + 2, ship::HitButton::Left, 5700);
    Frame(view, sim, 5700);
    Require(textureDraws[0].id == 20 && textureDraws[0].tint.a == 255,
            "Reduced motion did not skip the tutorial animation");
    view.SetTutorialTexture(0, nullptr);
    Frame(view, sim, 5800);
    Require(textureCalls == 0, "Missing tutorial art unexpectedly drew a texture");
    FindText("Illustration unavailable");
    Require(sim.ActionCount() == actionsBeforeGuide, "Tutorial browsing advanced the voyage");
    view.Escape(sim);
    sim.MutableState().reducedMotion = false;
    Frame(view, sim, 5900);
    const auto options = FindText("OPTIONS");
    view.Click(sim, options.x + 2, options.y + 2, ship::HitButton::Left, 0);
    Frame(view, sim);
    Require(view.Panel() == ship::TitlePanel::Settings && textureCalls == 1,
            "Options replaced the title image instead of opening a sheet over it");
    ImGui::DestroyContext();
    std::cout << "PASS: real Draw.cpp scroll direction, windowed Assignments, Vessel input/clamp, green action/air fills, visible station cooldown, source health colours/live values, literal engine pipe, screen-space green click mark/glow after scrolling, pinned/centered measured action bands at 1180/1280, independent feed wheel/drag/wrap/clamp plus timestamp/reset, centered fixed machine/crew scenes with panel reflow, measured chart height and wide centering, preserved purchase confirmation, free-click feedback/rise/fade, four dark title menus, title art retained behind Options; source crew relocation/corridor drift, life-info routing, crew/event GONE rendering, map-label and moving-ring selection, active-tab glow/reduced-motion, new/existing background-tab reveal; general text >=18px; compact scaled Home info\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "draw probe: " << error.what() << '\n'; return 1;
  }
}
