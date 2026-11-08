// CPU recording backend for the real Draw.cpp, not a replacement view model.
// This checks drawing coordinates/colours and input; it is NOT a GPU screenshot.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iterator>
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
// Circles are recorded so a chart body can be shown to be a painted sphere
// rather than a flat dot: the chart's whole vocabulary is drawn with them.
struct CircleCall { float x, y, r; Color color; bool filled; };
std::vector<CircleCall> circles;
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
  texts.clear(); rects.clear(); scissors.clear(); ellipses.clear(); circles.clear(); textureDraws.clear(); textureCalls = 0; drawOrder = 0;
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
void DrawEllipseLines(int x, int y, float rx, float ry, Color c) { ellipses.push_back({float(x), y + cameraY, rx, ry, c}); }
void DrawCircle(int, int, float, Color) {}
void DrawCircleV(Vector2 center, float radius, Color color) {
  circles.push_back({center.x, center.y + cameraY, radius, color, true});
}
void DrawCircleLines(int, int, float, Color) {}
void DrawCircleLinesV(Vector2 center, float radius, Color color) {
  circles.push_back({center.x, center.y + cameraY, radius, color, false});
}
void DrawCircleGradient(Vector2 center, float radius, Color inner, Color) {
  circles.push_back({center.x, center.y + cameraY, radius, inner, true});
}
void DrawCircleSector(Vector2 center, float radius, float, float, int, Color color) {
  circles.push_back({center.x, center.y + cameraY, radius, color, true});
}
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
    ImFont* regularFont = io.Fonts->AddFontDefault(&config);
    config.SizePixels = 23;
    ImFont* largeFont = io.Fonts->AddFontDefault(&config);
    unsigned char* pixels; int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    auto sim = ship::Simulation::ReferenceVoyage(ship::Catalog::LoadFromAssets("assets"));
    ship::ShellView view;
    view.Configure(1280, 720, ship::Theme::ById("classic"));
    // A restored save is offered on the title screen, not entered immediately.
    {
      auto restored = ship::Simulation::NewVoyage(sim.World());
      restored.PerformAction("restored voyage fixture");
      const double actions = restored.ActionCount();
      ship::ShellView title;
      title.Configure(1280, 720, ship::Theme::ById("classic"));
      title.ShowTitle(true);
      Frame(title, restored);
      Require(title.TitleActive() && title.TitleFromVoyage(), "Restored voyage skipped the title screen");
      const auto resume = FindText("RESUME");
      title.Click(restored, resume.x + 4, resume.y + 4, ship::HitButton::Left, 0);
      Require(!title.TitleActive() && restored.ActionCount() == actions, "Resume did not preserve the restored voyage");
    }
    // The ship identity is a fixed left-side home control, independent of the
    // selected font; task tabs start to its right and remain separately clickable.
    for (ImFont* font : {regularFont, largeFont}) {
      io.FontDefault = font;
      auto target = ship::Simulation::NewVoyage(sim.World());
      ship::ShellView topbar;
      topbar.Configure(1280, 720, ship::Theme::ById("classic"));
      Require(ship::OpenTask(target, "nav", "main", true), "Could not open navigation fixture");
      Frame(topbar, target);
      const auto layout = topbar.LayoutNow();
      Require(layout.home.x == 0.0f && layout.home.w == ship::Metrics::kSideW,
              "Ship home control is not fixed to the sidebar width");
      Require(layout.tabStrip.x == ship::Metrics::kSideW &&
                  layout.tabStrip.Right() <= layout.controls.x,
              "Task tabs do not begin beside the fixed ship home control");
      topbar.Click(target, layout.home.Right() - 4, layout.home.y + 10, ship::HitButton::Left, 0);
      Require(target.State().activeTab == "home", "Right edge of the ship home control is not clickable");
    }
    io.FontDefault = regularFont;
    // Chart controls use their painted bounds for all reading-size steps, and
    // topmost controls take priority over map bodies behind them.
    for (int step : {0, 3}) {
      auto nav = ship::Simulation::ReferenceVoyage(sim.World());
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

    // A region must win the click against the stars drawn inside it. Every
    // galaxy carries a cluster of star dots in its own footprint, and the flat
    // hit list is searched from the END, so before the hit order was pinned a
    // click on any galaxy selected one of its stars instead - and with no
    // region selected the "View ... sector chart" row never appeared, which is
    // what made a jump to another galaxy impossible.
    {
      auto far = ship::Simulation::ReferenceVoyage(sim.World());
      ship::ShellView galaxy;
      galaxy.Configure(1280, 720, ship::Theme::ById("classic"));
      ship::OpenTask(far, "nav", "main", true);
      far.SetLayer("galaxy");
      Frame(galaxy, far);
      const ship::NavChartView chart = ship::BuildNavChart(far);
      // The chart body sits in a frame of its own logical size; the painter
      // stretches it onto that box. Map chart coordinates to the glass the way
      // PaintNavScreen does, or the click lands in empty space.
      const ship::NavRaster raster = ship::BuildNavRaster(far);
      // The frame is filled with the classic palette's chart surface (#ece2d1).
      const auto frame = std::find_if(rects.rbegin(), rects.rend(), [&](const RectCall& r) {
        return r.color.r == 236 && r.color.g == 226 && r.color.b == 209 && r.w > 200.0f;
      });
      Require(frame != rects.rend(), "The chart frame was not painted");
      const float sx = frame->w / static_cast<float>(raster.width == 0 ? 1 : raster.width);
      const float sy = frame->h / static_cast<float>(raster.height == 0 ? 1 : raster.height);

      // Stay clear of the zoom/layer stack pinned over the chart's top-right
      // corner; those genuinely are above the chart and take the click first.
      for (const char* regionId : {"gal-gen-1", "gal-gen-6", "gal-core"}) {
        const auto mine = std::find_if(chart.bodies.begin(), chart.bodies.end(),
            [&](const ship::NavBodyView& b) { return b.id == regionId; });
        Require(mine != chart.bodies.end(), "A charted galaxy was not painted");
        far.SelectTarget("");
        galaxy.Click(far, frame->x + static_cast<float>(mine->at.x) * sx,
                     frame->y + static_cast<float>(mine->at.y) * sy, ship::HitButton::Left, 0);
        Require(far.State().navTarget.value_or("") == regionId,
                "Clicking a galaxy selected a star inside it instead of the galaxy");
        Frame(galaxy, far);
        // With the region held, the free row that opens its sector chart exists.
        bool offered = false;
        for (const auto& row : ship::BuildNavScreen(far).screen.commands) {
          if (row.label.find("sector chart") != std::string::npos) { offered = true; }
        }
        Require(offered, "A selected galaxy offered no way to open its sector chart");
      }
      Require(far.ActionCount() == 0, "Selecting a galaxy spent an action");
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
      auto handbook = ship::Simulation::ReferenceVoyage(world);
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
      auto longHandbook = ship::Simulation::ReferenceVoyage(world);
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
    // The selected reading size must actually change the small labels around
    // the exterior and the compact Home info cards, not merely the sidebar.
    {
      Texture2D concept{77, 1340, 420, 1, 7};
      io.DisplaySize = ImVec2(1280, 1600);
      for (bool largeText : {false, true}) {
        int previousRoomSize = 0;
        int previousInfoSize = 0;
        for (int step = 0; step < 4; ++step) {
          auto fitted = ship::Simulation::ReferenceVoyage(sim.World());
          const auto before = fitted.ActionCount();
          ship::Theme theme = ship::Theme::ById("classic");
          theme.fontStep = step;
          theme.largeText = largeText;
          ship::ShellView home;
          home.Configure(1280, 1600, theme);
          home.SetWrenExteriorTexture(&concept);
          Frame(home, fitted);
          const auto room = FindText("Engine Room");
          const auto info = FindText(fitted.Infos().front().label);
          Require(room.size == theme.TextPx(10) && room.size > previousRoomSize,
                  "Exterior room labels did not grow with reading size");
          Require(info.size == theme.HomeInfoLabelPx() && info.size > previousInfoSize,
                  "Compact Home labels did not grow with reading size");
          Require(room.y >= home.LayoutNow().content.y &&
                      room.y + room.size < home.LayoutNow().content.Bottom(),
                  "Larger exterior room label escaped the visible area");
          home.Click(fitted, room.x + 2, room.y + 2, ship::HitButton::Right, 0);
          Require(ship::FindTab(fitted.State(), "room:engine") != nullptr,
                  "Resized exterior label stopped opening its room");
          Require(fitted.ActionCount() == before,
                  "Changing reading size or inspecting a room spent an action");
          previousRoomSize = static_cast<int>(room.size);
          previousInfoSize = static_cast<int>(info.size);
        }
      }
    }

    // Real Draw.cpp callouts route all Wren rooms to their existing handlers.
    // Cosmetic time, browsing and view switches must not advance the clock or
    // a lock; the original deck still exposes its actual crew indicators.
    {
      Texture2D concept{77, 1340, 420, 1, 7};
      auto starter = ship::Simulation::NewVoyage(sim.World());
      ship::ShellView starterView;
      io.DisplaySize = ImVec2(1280, 1200);
      starterView.Configure(1280, 1200, ship::Theme::ById("classic"));
      starterView.SetWrenExteriorTexture(&concept);
      Frame(starterView, starter);
      Require(std::any_of(textureDraws.begin(), textureDraws.end(), [](const TextureCall& t) { return t.id == 77; }),
              "The Wren exterior disappeared from the starter ship");
      Require(starter.Rooms().size() == 6, "Starter has nonstandard rooms");
      const auto engineCallout = FindText("Engine Room");
      const auto oxygenCallout = FindText("O2 Filter Room");
      const auto cargoCallout = FindText("Cargo Hold");
      const auto crewCallout = FindText("Crew Quarters");
      const auto messCallout = FindText("Mess Hall");
      const auto bridgeCallout = FindText("Bridge");
      Require(engineCallout.x < oxygenCallout.x && oxygenCallout.x < cargoCallout.x &&
                  engineCallout.y < oxygenCallout.y && oxygenCallout.y < cargoCallout.y &&
                  crewCallout.x < messCallout.x && messCallout.x < bridgeCallout.x &&
                  crewCallout.y < messCallout.y && messCallout.y < bridgeCallout.y,
              "Three-row room callouts lost their measured, non-overlapping stagger");
      const auto art = std::find_if(textureDraws.begin(), textureDraws.end(),
                                    [](const TextureCall& draw) { return draw.id == 77; });
      Require(art != textureDraws.end(), "Wren art bounds are missing for callout spacing check");
      const auto panelFor = [&](const TextCall& label) {
        return std::find_if(rects.begin(), rects.end(), [&](const RectCall& rect) {
          return rect.color.r == starterView.CurrentTheme().palette.panel.r &&
                 rect.color.g == starterView.CurrentTheme().palette.panel.g &&
                 rect.color.b == starterView.CurrentTheme().palette.panel.b &&
                 label.x >= rect.x && label.x < rect.x + rect.w &&
                 label.y >= rect.y && label.y < rect.y + rect.h && rect.w > 100.0f;
        });
      };
      const auto oxygenPanel = panelFor(oxygenCallout);
      const auto cargoPanel = panelFor(cargoCallout);
      const auto crewPanel = panelFor(crewCallout);
      const auto bridgePanel = panelFor(bridgeCallout);
      Require(oxygenPanel != rects.end() && cargoPanel != rects.end() &&
                  crewPanel != rects.end() && bridgePanel != rects.end() &&
                  oxygenPanel->x + oxygenPanel->w + 16.0f <= art->dest.x &&
                  cargoPanel->x + cargoPanel->w + 16.0f <= art->dest.x &&
                  crewPanel->x >= art->dest.x + art->dest.width + 16.0f &&
                  bridgePanel->x >= art->dest.x + art->dest.width + 16.0f,
              "Room callout panels overlap the Wren art bounds");
      Require(std::none_of(texts.begin(), texts.end(), [](const TextCall& t) { return t.text == "Science Room"; }),
              "Uninstalled room was shown as an exterior callout");
      for (const auto& room : starter.Rooms()) {
        Frame(starterView, starter);
        const auto label = FindText(room.name);
        starterView.Click(starter, label.x + 2, label.y + 2, ship::HitButton::Right, 0);
        Require(ship::FindTab(starter.State(), "room:" + room.id) != nullptr,
                "Starter room callout did not open its corresponding room");
      }
      starter.MutableMap().here = sim.World().ports.front().id;
      starter.MutableState().docked = true;
      starter.MutableState().shiplets = 1000;
      Require(starter.BuyRoom("science"), "Could not install science room for exterior test");
      Frame(starterView, starter);
      const auto science = FindText("Science Room");
      starterView.Click(starter, science.x + 2, science.y + 2, ship::HitButton::Right, 0);
      Require(ship::FindTab(starter.State(), "room:science") != nullptr,
              "Newly installed room did not gain a working exterior callout");
      auto artShip = ship::Simulation::ReferenceVoyage(sim.World());
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
      const Rectangle shipImage = textureDraws.back().dest;
      hull.Click(artShip, shipImage.x + shipImage.width / 2,
                 shipImage.y + shipImage.height / 2, ship::HitButton::Left, 100000);
      auto* person = artShip.FindCrew("jax");
      Require(person != nullptr, "No crew fixture");
      person->colour = "#010203";
      Frame(hull, artShip, 100100);
      Require(std::none_of(texts.begin(), texts.end(), [](const TextCall& t) {
                return t.text == "Deck plan / crew" || t.text == "Exterior view";
              }), "A separate ship-view button is still drawn");
      Require(std::none_of(textureDraws.begin(), textureDraws.end(), [](const TextureCall& t) { return t.id == 77; }),
              "Clicking the ship did not open the deck plan");
      const auto dot = std::find_if(rects.begin(), rects.end(), [](const RectCall& r) {
        return r.w == 15 && r.h == 15 && r.color.r == 1 && r.color.g == 2 && r.color.b == 3;
      });
      Require(dot != rects.end(), "Deck plan lost the crew indicator");
      hull.Click(artShip, dot->x + 3, dot->y + 3, ship::HitButton::Right, 100100);
      Require(ship::FindTab(artShip.State(), "crew:jax") != nullptr, "Crew dot stopped opening its record");

      ship::ShellView exteriorAgain;
      exteriorAgain.Configure(1280, 1200, ship::Theme::ById("classic"));
      exteriorAgain.SetWrenExteriorTexture(&concept);
      Frame(exteriorAgain, artShip, 100300);
      const auto label = FindText("Engine Room");
      exteriorAgain.Click(artShip, label.x + 2, label.y + 2, ship::HitButton::Left, 100300);
      Require(exteriorAgain.HasSelection() && exteriorAgain.Selection().id == "engine",
              "Exterior left click did not open the room briefing");
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
      auto narrowShip = ship::Simulation::ReferenceVoyage(sim.World());
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
    sim = ship::Simulation::ReferenceVoyage(sim.World());
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
    // Three columns walked in one direction: five cards on the left, the open
    // card's items in the middle, and the inspector with its one commit button
    // on the right. Cards and items are free; the market's commit is money only.
    const auto startsWith = [](const char* prefix, const char* missing) {
      const auto it = std::find_if(texts.begin(), texts.end(), [&](const auto& t) {
        return t.text.find(prefix) == 0;
      });
      Require(it != texts.end(), missing);
      return *it;
    };
    const auto marketCard = startsWith("Market - purse", "the Market card did not paint");
    const auto salesCard = startsWith("Sell cargo and survey data", "the sales card did not paint");
    const auto boardCard = startsWith("Star-map", "the star-map card did not paint");
    const auto yardCard = startsWith("Shipyard and crew", "the shipyard card did not paint");
    const auto tavernCard = startsWith("Trade contracts", "the trade contracts card did not paint");
    Require(std::abs(salesCard.x - marketCard.x) < 1 && std::abs(boardCard.x - marketCard.x) < 1 &&
            std::abs(yardCard.x - marketCard.x) < 1 && std::abs(tavernCard.x - marketCard.x) < 1,
            "The five station cards are not in one column");
    Require(salesCard.y > marketCard.y && boardCard.y > salesCard.y && tavernCard.y > boardCard.y &&
            yardCard.y > tavernCard.y,
            "The station cards are not stacked in their own order");
    const auto fuel = startsWith("Fuel cell", "the market items did not paint");
    Require(fuel.x > marketCard.x, "The market's items are not in the middle column");
    const auto commit = startsWith("Buy - 45 sh", "the inspector commit button did not paint");
    Require(commit.x > fuel.x, "The inspector's commit button is not in the right column");
    const double beforeBuy = sim.ActionCount();
    const double cellsBefore = sim.State().fuelCells;
    view.Click(sim, yardCard.x + 2, yardCard.y + 2, ship::HitButton::Left, 0);
    Frame(view, sim);
    Require(sim.ActionCount() == beforeBuy, "Opening a station card spent an action");
    Require(startsWith("Buy larger ship", "the yard items did not open").x > marketCard.x, "The yard's items did not open");
    view.Click(sim, marketCard.x + 2, marketCard.y + 2, ship::HitButton::Left, 0);
    Frame(view, sim);
    Require(sim.ActionCount() == beforeBuy, "Returning to the market card spent an action");
    const auto fuelItem = startsWith("Fuel cell", "the market items did not paint");
    view.Click(sim, fuelItem.x + 2, fuelItem.y + 2, ship::HitButton::Left, 0);
    Frame(view, sim);
    Require(sim.ActionCount() == beforeBuy, "Picking a station item spent an action");
    const auto buyNow = startsWith("Buy - 45 sh", "the inspector commit button did not paint");
    view.Click(sim, buyNow.x + 2, buyNow.y + 2, ship::HitButton::Left, 0);
    Frame(view, sim);
    Require(sim.ActionCount() == beforeBuy, "A market purchase spent an action");
    Require(sim.State().fuelCells == cellsBefore + 1, "Native station click failed to buy");
    const auto buyAgain = startsWith("Buy - 45 sh", "the inspector commit button did not paint");
    view.Click(sim, buyAgain.x + 2, buyAgain.y + 2, ship::HitButton::Left, 0);
    Frame(view, sim);
    Require(sim.State().fuelCells == cellsBefore + 2 && sim.ActionCount() == beforeBuy,
            "The money-only counter closed after one purchase");
    // A station row that does cost an action still marks the clock in HTA green.
    view.Click(sim, salesCard.x + 2, salesCard.y + 2, ship::HitButton::Left, 0);
    Frame(view, sim);
    const auto sellCommit = startsWith("Sell - ", "the sales commit button did not paint");
    view.Click(sim, sellCommit.x + 2, sellCommit.y + 2, ship::HitButton::Left, 0);
    Frame(view, sim);
    Require(sim.ActionCount() == beforeBuy + 1, "Selling a lot did not spend one action");
    Require(std::any_of(rects.begin(), rects.end(), [](const auto& r) {
      return r.color.r == 126 && r.color.g == 198 && r.color.b == 63 && r.w == 11 && r.h == 2;
    }), "HTA 654–659 action mark is not the green 11 by 2 bar");

    // The Mess Hall must paint values, never the ids it used to carry as text.
    {
      auto galley = ship::Simulation::ReferenceVoyage(sim.World());
      ship::ShellView kitchen;
      kitchen.Configure(1280, 720, ship::Theme::ById("classic"));
      ship::OpenTask(galley, "room", "mess", true);
      Frame(kitchen, galley);
      for (const auto& painted : texts) {
        Require(painted.text.find("mess-") == std::string::npos,
                "The galley painted an element id instead of a value");
      }
      FindText("18 of 24 food in the galley");
      FindText("25 actions to next meal");
      // Hunger is a live value on the crew record, and it drives a red bar.
      galley.FindCrew("jax")->hunger = 88;
      ship::OpenTask(galley, "crew", "jax", true);
      Frame(kitchen, galley);
      FindText("88%  Starving");
      Require(std::any_of(rects.begin(), rects.end(), [](const auto& r) {
        return r.color.r == 176 && r.color.g == 90 && r.color.b == 66 && r.w > 0;
      }), "Starving hunger did not paint the source red bar");
      // One food drains five hunger; the click that triggers the meal still
      // adds its 0.6 before the three-food meal is applied.
      auto hungry = ship::Simulation::ReferenceVoyage(sim.World());
      auto* hand = hungry.FindCrew("rell");
      hand->hunger = 30;
      hand->hungerTick = hungry.GameRules().mess.every - 1;
      const double rackBefore = hungry.MessRack();
      const double floorAt = hungry.GameRules().mess.hungerFloor;
      // One food per point of hunger, capped by what is actually on the
      // counter: with less food than hunger above the floor, the hand eats the
      // counter dry and keeps the rest of its hunger.
      // A meal is three food - the fifteen hunger a hand gains over its own
      // twenty-five clicks, five points per food - capped by what is on the
      // counter. It is not "eat the shelf empty".
      const auto& mealRules = hungry.GameRules().mess;
      const double mealFood = mealRules.hungerPerMeal / mealRules.hungerPerFood;
      const double eaten = std::min(mealFood, rackBefore);
      const double risePerClick = mealRules.hungerPerMeal / mealRules.every;
      hungry.PerformAction();
      Require(std::abs(hand->hunger - (30 + risePerClick -
                                      eaten * mealRules.hungerPerFood)) < 1e-9,
              "The meal did not drain its own three food after the due-click rise");
      Require(hand->hunger >= floorAt, "A fed hand fell under the hunger floor");
      Require(hungry.MessRack() == rackBefore - eaten, "The meal took the wrong amount of food");
    }

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
        auto check = ship::Simulation::ReferenceVoyage(sim.World());
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
        auto check = ship::Simulation::ReferenceVoyage(sim.World());
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
      auto check = ship::Simulation::ReferenceVoyage(sim.World());
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
        auto check = ship::Simulation::ReferenceVoyage(catalog);
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
        Require(check.Map().vw == 638 && check.Map().vh == 278, "Configured logical map viewport was changed");
      }
    }

    // The feed owns its wheel/drag offset; the surrounding Home column must not move.
    {
      auto check = ship::Simulation::ReferenceVoyage(sim.World());
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
      auto check = ship::Simulation::ReferenceVoyage(sim.World());
      check.MutableState().docked = true;
      check.MutableMap().here = "tolme";
      check.MutableState().shiplets = 10000;
      check.MutableState().options.confirmRisk = false; // Ship purchase always confirms in the HTA.
      ship::ShellView shell;
      shell.Configure(1180, 720, ship::Theme::ById("classic"));
      ship::OpenTask(check, "station", "tolme", true);
      const auto actions = check.ActionCount();
      const auto locatePurchase = [&]() {
        // The yard card opens the middle column, the item opens the inspector,
        // and the inspector's own button is what the confirmation hangs off.
        for (int i = 0; i < 100; ++i) {
          Frame(shell, check);
          const auto card = std::find_if(texts.begin(), texts.end(), [](const auto& t) {
            return t.text.find("Shipyard") == 0; });
          Require(card != texts.end(), "Shipyard card is missing");
          const float cardX = card->x, cardY = card->y;   // texts refill every frame
          shell.Click(check, cardX + 2, cardY + 2, ship::HitButton::Left, 0);
          Frame(shell, check);
          const auto c = shell.LayoutNow().content;
          // Opening the card picks its first item, the larger ship, so the
          // inspector's own button is what the confirmation hangs off.
          const auto commit = std::find_if(texts.begin(), texts.end(), [&](const auto& t) {
            return t.text.find("Buy - 4,000 sh") == 0 && t.x > cardX; });
          Require(commit != texts.end(), "Ship purchase commit button is missing");
          if (commit->y >= c.y + 20 && commit->y < c.Bottom() - 180) return *commit;
          shell.Scroll(c.x + 20, c.y + 20, commit->y >= c.Bottom() - 180 ? -1 : 1);
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
      Require(check.ActionCount() == actions && check.State().shipClass == "kestrel", "Pinned confirmation lost the original purchase dispatcher");
      Frame(shell, check);
      RequireMark(yes.x + 2, yes.y + 2);
    }

    // A positive FREE room click also gets feedback: it is not limited to spent actions.
    {
      auto check = ship::Simulation::ReferenceVoyage(sim.World());
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
      auto talkSim = ship::Simulation::ReferenceVoyage(ship::Catalog::LoadFromAssets("assets"));
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
      auto talkSim = ship::Simulation::ReferenceVoyage(ship::Catalog::LoadFromAssets("assets"));
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
        auto audit = ship::Simulation::ReferenceVoyage(ship::Catalog::LoadFromAssets("assets"));
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
        Require(model.stats.front().box.h <= (step == 0 ? 64.0f : 74.0f),
                "Ship-system gauge panels were not compacted");
        const auto& homeCard = model.infos.front().box;
        const float lowerBottom = std::max(model.nav.box.Bottom(), model.eventsBox.Bottom());
        Require(model.stageArea.h >= ship::Metrics::kHomeStageMinH &&
                    model.nav.box.y >= homeCard.Bottom() + ship::Metrics::kHomePanelGap &&
                    model.eventsBox.y == model.nav.box.y,
                "The six small Home panels are not grouped below the centered ship stage");
        Require(std::all_of(model.infos.begin(), model.infos.end(), [&](const auto& info) {
                  return info.box.w == homeCard.w && info.box.h == homeCard.h;
                }) && model.nav.box.w == homeCard.w && model.nav.box.h == homeCard.h &&
                    model.events.size() == 2 &&
                    model.events[0].box.w == homeCard.w && model.events[1].box.w == homeCard.w &&
                    model.events[0].box.h == homeCard.h && model.events[1].box.h == homeCard.h,
                "The six Home cards do not share identical dimensions");
        Require(model.nav.box.x == homeCard.x &&
                    model.events[0].box.x == homeCard.x + homeCard.w + ship::Metrics::kHomePanelGap &&
                    model.events[1].box.x == model.events[0].box.x + homeCard.w + ship::Metrics::kHomePanelGap &&
                    model.feedBox.y == lowerBottom + ship::Metrics::kHomePanelGap,
                "Home cards do not have consistent gutters above Activity Feed");
        filledAt(model.stats.front().box.x, model.stats.front().box.y);
        const auto& navTitle = FindText("Navigation");
        const auto navSurface = std::find_if(rects.begin(), rects.end(), [&](const auto& r) {
          return navTitle.x >= r.x && navTitle.x < r.x + r.w &&
                 navTitle.y >= r.y && navTitle.y < r.y + r.h &&
                 r.color.r == panel.r && r.color.g == panel.g && r.color.b == panel.b;
        });
        Require(navSurface != rects.end(), "Navigation panel is missing its warm surface fill");
        for (const auto& info : model.infos) {
          const auto& background = filledAt(info.box.x, info.box.y);
          Require(background.h > model.infos.front().box.h, "Long info did not grow the rail");
          const auto& label = FindText(info.label);
          Require(label.size == theme.HomeInfoLabelPx(), "Info label ignored its compact font size");
          Require(label.y >= background.y && label.y + label.size <= background.y + background.h,
                  "Info label escaped its background");
        }
        const auto& firstPanel = filledAt(model.infos.front().box.x, model.infos.front().box.y);
        Require(navTitle.y >= firstPanel.y + firstPanel.h,
                "Navigation was not moved below the three information panels");
        std::vector<TextCall> eventTitles;
        std::copy_if(texts.begin(), texts.end(), std::back_inserter(eventTitles), [&](const auto& text) {
          return text.text == "Events" && text.x >= home.LayoutNow().content.x;
        });
        Require(eventTitles.size() == 2 && eventTitles.front().y >= firstPanel.y + firstPanel.h,
                "Two home event cards were not placed below the three information panels");
        std::vector<RectCall> paintedCards;
        for (const auto& info : model.infos) {
          paintedCards.push_back(filledAt(info.box.x, info.box.y));
        }
        paintedCards.push_back(*navSurface);
        for (const auto& title : eventTitles) {
          const auto surface = std::find_if(rects.begin(), rects.end(), [&](const auto& rect) {
            return rect.color.r == panel.r && rect.color.g == panel.g && rect.color.b == panel.b &&
                   title.x >= rect.x && title.x < rect.x + rect.w &&
                   title.y >= rect.y && title.y < rect.y + rect.h;
          });
          Require(surface != rects.end(), "Events card is missing its warm surface fill");
          paintedCards.push_back(*surface);
        }
        Require(paintedCards.size() == 6 && std::all_of(paintedCards.begin(), paintedCards.end(),
                  [&](const auto& card) {
                    return card.w == paintedCards.front().w && card.h == paintedCards.front().h;
                  }), "Painted Home cards do not have exactly equal dimensions");
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
        const auto& feedSurface = filledAt(model.feedBox.x, feedHead.y - 6.0f);
        Require(feedSurface.w > 0, "Feed has no warm background");
        Require(feedHead.y >= feedSurface.y + 4.0f &&
                    feedHead.y + feedHead.size <= feedSurface.y + feedSurface.h,
                "Activity Feed heading is not inside its panel");
        Require(feedSurface.h <= ship::Metrics::kHomeFeedMax + 1.0f,
                "Home activity feed grows too far up the screen");
        Require(std::abs(feedSurface.y + feedSurface.h - (home.LayoutNow().content.Bottom() - 8.0f)) < 2.0f,
                "Activity feed does not reach the bottom of the content panel");
      }
    }

    // Docking is represented inside one of the two event cards, not as a
    // seventh panel under Navigation.
    {
      auto berthed = ship::Simulation::ReferenceVoyage(ship::Catalog::LoadFromAssets("assets"));
      berthed.MutableState().docked = true;
      ship::ShellView home;
      home.Configure(1280, 900, ship::Theme::ById("classic"));
      io.DisplaySize = ImVec2(1280, 900);
      Frame(home, berthed);
      const auto& dockedHeading = FindText("Docked");
      const auto& feedHead = FindText("Activity Feed");
      Require(dockedHeading.x > home.LayoutNow().content.x + 300.0f,
              "Docked status was not placed in an Events card");
      Require(feedHead.y >= dockedHeading.y + 20.0f,
              "Activity Feed overlaps the dock/event cards");
      const auto eventHeadingCount = std::count_if(texts.begin(), texts.end(), [&](const auto& text) {
        return text.text == "Events" && text.x >= home.LayoutNow().content.x;
      });
      Require(eventHeadingCount == 1,
              "Docked status did not occupy one of the two event slots");
    }

    // Exercise the real pointer paths and animation, not only their models.
    {
      auto audit = ship::Simulation::ReferenceVoyage(ship::Catalog::LoadFromAssets("assets"));
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
      // The chart draws bodies as themselves, not as dots: a planet is a lit
      // disc with a shadowed hemisphere, a gas giant carries its ring, and a
      // star bleeds light. Recorded here so the art cannot silently regress to
      // "one filled circle per name".
      std::vector<CircleCall> chartCircles;
      for (const auto& circle : circles) {
        if (circle.x < mapBox->x || circle.x > mapBox->x + mapBox->w) continue;
        if (circle.y < mapBox->y || circle.y > mapBox->y + mapBox->h) continue;
        chartCircles.push_back(circle);
      }
      Require(chartCircles.size() >= 30, "Chart bodies were not painted as spheres");
      Require(std::any_of(chartCircles.begin(), chartCircles.end(),
                          [](const CircleCall& c) { return c.filled && c.r >= 2.5f; }),
              "No chart body was drawn as a disc");
      Require(std::any_of(chartCircles.begin(), chartCircles.end(),
                          [](const CircleCall& c) { return c.color.a < 255 && c.r >= 6.0f; }),
              "No chart star painted a corona");
      Require(std::any_of(ellipses.begin(), ellipses.end(), [&](const EllipseCall& e) {
                return e.x >= mapBox->x && e.x <= mapBox->x + mapBox->w && e.y >= mapBox->y &&
                       e.y <= mapBox->y + mapBox->h && e.rx > e.ry * 2.0f && e.ry >= 1.0f;
              }),
              "A gas giant's ring was not painted");
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
      Require(std::any_of(circles.begin(), circles.end(), [](const CircleCall& c) {
                return c.color.r >= 200 && c.color.g < 60 && c.color.b < 60 && c.r >= 9.0f;
              }),
              "Player ship position marker was not painted in vivid red");

      audit.SetLayer("sector");
      for (int z = 0; z < 20; ++z) { audit.ZoomMap(1); }
      Frame(cockpit, audit, 974000);
      Require(std::any_of(ellipses.begin(), ellipses.end(), [](const EllipseCall& e) {
                return e.rx >= 20.0f && e.ry >= 15.0f;
              }),
              "Sector layer did not paint distinct sector territory contours");
      Require(std::any_of(circles.begin(), circles.end(), [](const CircleCall& c) {
                return c.color.r >= 200 && c.color.g < 60 && c.color.b < 60 && c.r >= 9.0f;
              }),
              "Sector layer did not paint the vivid red player position marker");
    }
    {
      auto tabs = ship::Simulation::ReferenceVoyage(ship::Catalog::LoadFromAssets("assets"));
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
      cockpit.Click(tabs, cockpit.LayoutNow().tabStrip.Right() - 12, 12, ship::HitButton::Left, 5000);
      Require(ship::FindTab(tabs.State(), "crew:missing17") == nullptr,
              "New background tab was not revealed with its close target");
      ship::OpenTask(tabs, "crew", "missing0", false);
      Frame(cockpit, tabs, 6000);
      const auto* tab = ship::FindTab(tabs.State(), "crew:missing0");
      Require(tab != nullptr, "Existing background tab vanished");
      const float closeX = cockpit.LayoutNow().tabStrip.x +
          ImGui::CalcTextSize((tab->kindLabel + "  " + tab->title).c_str()).x + 20;
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
    std::cout << "PASS: real Draw.cpp scroll direction, windowed Assignments, Vessel input/clamp, green action/air fills, station card/item/inspector walk with money-only purchases, source health colours/live values, literal engine pipe, screen-space green click mark/glow after scrolling, pinned/centered measured action bands at 1180/1280, independent feed wheel/drag/wrap/clamp plus timestamp/reset, centered fixed machine/crew scenes with panel reflow, measured chart height and wide centering, preserved purchase confirmation, free-click feedback/rise/fade, four dark title menus, title art retained behind Options; source crew relocation/corridor drift, life-info routing, crew/event GONE rendering, map-label and moving-ring selection, active-tab glow/reduced-motion, new/existing background-tab reveal; general text >=18px; compact scaled Home info\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "draw probe: " << error.what() << '\n'; return 1;
  }
}
