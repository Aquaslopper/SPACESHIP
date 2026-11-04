// ============================================================================
//  main.cpp — Phase 0 client.
//  ---------------------------------------------------------------------------
//  This is a skeleton on purpose. It opens the reference edition's window,
//  loads the extracted catalogue, applies the flight-deck palette to Dear ImGui
//  and reports what it found. The shell, the ship diagram and the task windows
//  arrive in Phases 2 and 3; nothing here pretends to be a game yet.
//
//  What it does prove, on a machine with the graphics development packages:
//    * the assets are found beside the executable,
//    * the catalogue parses and validates in a real process,
//    * Raylib and ImGui are wired together by rlImGui,
//    * the bundled fonts load,
//    * no widget falls back to the stock dark-blue ImGui theme.
// ============================================================================

#include <cstdio>
#include <filesystem>
#include <string>

#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"

#include "data/Catalog.h"
#include "ui/Palette.h"

namespace {

// The reference opens itself at 1440x940 and centres the window.
constexpr int kDefaultWidth = 1440;
constexpr int kDefaultHeight = 940;

std::filesystem::path AssetDirBesideExecutable(const char* argv0) {
  std::error_code ec;
  std::filesystem::path exe = std::filesystem::weakly_canonical(argv0, ec);
  if (ec) { return {}; }
  return exe.parent_path() / "assets";
}

// Prefer the real Windows faces when they are installed, so metrics match the
// HTA on the platform it was written for; otherwise use the bundled DejaVu.
// Both are asked for at the same pixel size, 12 px, which is what the CSS
// declares for body text.
void LoadFonts(const std::filesystem::path& assets) {
  ImGuiIO& io = ImGui::GetIO();

  const std::filesystem::path bundledSans = assets / "fonts" / "DejaVuSans.ttf";
  const std::filesystem::path bundledMono = assets / "fonts" / "DejaVuSansMono.ttf";

  const char* sans = nullptr;
  const char* mono = nullptr;
  std::string sansPath;
  std::string monoPath;

  for (const char* candidate : {"C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"}) {
    if (std::filesystem::exists(candidate)) { sans = candidate; break; }
  }
  for (const char* candidate : {"C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/cour.ttf"}) {
    if (std::filesystem::exists(candidate)) { mono = candidate; break; }
  }
  if (sans == nullptr && std::filesystem::exists(bundledSans)) {
    sansPath = bundledSans.string();
    sans = sansPath.c_str();
  }
  if (mono == nullptr && std::filesystem::exists(bundledMono)) {
    monoPath = bundledMono.string();
    mono = monoPath.c_str();
  }

  // The first font added becomes the default, so the UI face goes first and the
  // monospace face second for numbers and readouts (the reference's .num class).
  if (sans != nullptr) { io.Fonts->AddFontFromFileTTF(sans, 13.0f); }
  if (mono != nullptr) { io.Fonts->AddFontFromFileTTF(mono, 13.0f); }

  std::printf("fonts: %s%s\n",
              sans ? sans : "ImGui default",
              mono ? (std::string(" + ") + mono).c_str() : "");
}

void DrawCatalogueReport(const ship::Catalog& catalog, const ship::ui::Palette& palette) {
  ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(720, 520), ImGuiCond_FirstUseEver);
  ImGui::Begin("Spaceship Command");

  ImGui::TextColored(ImVec4(palette.muted.r / 255.0f, palette.muted.g / 255.0f,
                            palette.muted.b / 255.0f, 1.0f),
                     "Phase 0 - build system, catalogue and data gate.");
  ImGui::Separator();

  const ship::Counts& n = catalog.counts;
  ImGui::Text("%d regions   %d systems   %d chart bodies   %d ports",
              n.regions, n.systems, n.bodies, n.ports);
  ImGui::Text("%d rooms on the Kestrel deck plan, %d of them fitted",
              n.roomsKestrel, n.fittedRooms);
  ImGui::Text("%d contracts   %d incidents   %d commendations   %d handbook entries",
              n.contracts, n.incidents, n.commendations, n.wiki);
  ImGui::Text("%d crew   %d candidates   %d jobs   %d task types",
              n.crew, n.hires, n.jobs, n.taskTypes);

  ImGui::Separator();
  const std::vector<std::string> problems = catalog.Validate();
  if (problems.empty()) {
    ImGui::TextColored(ImVec4(palette.sage.r / 255.0f, palette.sage.g / 255.0f,
                              palette.sage.b / 255.0f, 1.0f),
                       "Catalogue integrity: sound.");
  } else {
    ImGui::TextColored(ImVec4(palette.terracotta.r / 255.0f, palette.terracotta.g / 255.0f,
                              palette.terracotta.b / 255.0f, 1.0f),
                       "Catalogue integrity: %d problem(s).", static_cast<int>(problems.size()));
    for (const std::string& problem : problems) { ImGui::BulletText("%s", problem.c_str()); }
  }

  ImGui::Separator();
  ImGui::TextWrapped(
      "The idle screen, the deck plan and the task windows are built in Phase 2 and 3. "
      "Everything this window reports is read from assets/data, which tools/export-data.cjs "
      "generates from Spaceship.hta.");

  ImGui::Separator();
  ImGui::TextColored(ImVec4(palette.muted.r / 255.0f, palette.muted.g / 255.0f,
                            palette.muted.b / 255.0f, 1.0f),
                     "Palette: %s   Source: Spaceship.hta, %lld bytes",
                     palette.label, catalog.sourceBytes);

  ImGui::End();
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  // ---- catalogue ---------------------------------------------------------
  std::filesystem::path assets = AssetDirBesideExecutable(argv[0]);
  if (assets.empty() || !std::filesystem::exists(assets / "data" / "manifest.json")) {
    assets = ship::Catalog::DefaultAssetDir();
  }

  ship::Catalog catalog;
  try {
    catalog = ship::Catalog::LoadFromAssets(assets);
  } catch (const ship::CatalogError& error) {
    std::fprintf(stderr, "spaceship: %s\n", error.what());
    return 1;
  }

  const std::vector<std::string> problems = catalog.Validate();
  std::printf("catalogue: %d regions, %d systems, %d bodies, %d ports\n",
              catalog.counts.regions, catalog.counts.systems,
              catalog.counts.bodies, catalog.counts.ports);
  for (const std::string& problem : problems) {
    std::fprintf(stderr, "catalogue problem: %s\n", problem.c_str());
  }

  // ---- window ------------------------------------------------------------
  SetTraceLogLevel(LOG_WARNING);
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
  InitWindow(kDefaultWidth, kDefaultHeight, "Spaceship Command");
  SetWindowMinSize(1180, 720);
  SetTargetFPS(60);

  const ship::ui::Palette palette = ship::ui::ClassicPalette();

  // rlImGui owns the ImGui context, so fonts go in between its two init halves.
  rlImGuiBeginInitImGui();
  LoadFonts(assets);
  rlImGuiEndInitImGui();
  ship::ui::ApplyPalette(palette);

  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(palette.paper);

    rlImGuiBegin();
    DrawCatalogueReport(catalog, palette);
    rlImGuiEnd();

    EndDrawing();
  }

  rlImGuiShutdown();
  CloseWindow();
  return 0;
}
