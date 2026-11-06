// ============================================================================
//  main.cpp — the client.
//  ---------------------------------------------------------------------------
//  Phase 2 turns the Phase 0 skeleton into the cockpit: the window opens at the
//  reference's size, the catalogue loads, a voyage begins, and ShellView paints
//  the taskbar, the sidebar, the pinned foot, the status bar and the idle
//  screen's ship cross-section. Task windows arrive in Phase 3.
//
//  Everything the shell decides is in cpp/ui's headless view-models; this file
//  is only the loop that turns a mouse into a verb and a frame into a paint.
// ============================================================================

#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"
#include "raylib.h"
#include "rlImGui.h"

#include "audio/Audio.h"
#include "audio/AudioDevice.h"
#include "core/Simulation.h"
#include "core/AutoSave.h"
#include "core/DisplaySettings.h"
#include "data/Catalog.h"
#include "ui/Draw.h"
#include "ui/PointerOwnership.h"
#include "ui/Palette.h"
#include "ui/Theme.h"

#ifdef _WIN32
extern "C" __declspec(dllimport) int __stdcall MessageBoxA(void*, const char*, const char*, unsigned int);
#endif

namespace {

void SaveWarning(const std::string& message) {
  std::fprintf(stderr, "spaceship: %s\n", message.c_str());
#ifdef _WIN32
  MessageBoxA(nullptr, message.c_str(), "Spaceship save warning", 0x30u);
#endif
}

using ship::FeedLine;
using ship::Options;
using ship::Simulation;
namespace audio = ship::audio;

// The reference opens itself at 1440x940 and centres the window.
constexpr int kDefaultWidth = 1440;
constexpr int kDefaultHeight = 940;

bool HasCatalog(const std::filesystem::path& assets) {
  return std::filesystem::exists(assets / "data" / "manifest.json");
}

std::filesystem::path FindAssetDir() {
  std::filesystem::path selected;
  const std::filesystem::path relativeAssets = "assets";
  if (HasCatalog(relativeAssets)) {
    std::error_code ec;
    selected = std::filesystem::absolute(relativeAssets, ec);
    if (ec) { selected = relativeAssets; }
  }

  const char* applicationDirectory = GetApplicationDirectory();
  if (applicationDirectory != nullptr && *applicationDirectory != '\0') {
    const std::filesystem::path appDir(applicationDirectory);
    const std::filesystem::path besideExecutable = appDir / "assets";
    if (HasCatalog(besideExecutable)) { selected = besideExecutable; }

    // Save slots are relative to the working directory. Explorer may launch an
    // application with an unrelated current directory, so anchor runtime data
    // beside the executable before the UI or save system is initialized.
    std::error_code ec;
    std::filesystem::current_path(appDir, ec);
  }

  return selected.empty() ? ship::Catalog::DefaultAssetDir() : selected;
}

struct FontResources {
  Font body{};
  Font bold{};
  Font mono{};
  std::array<ImFont*, 6> imguiBody{};
};

std::string FirstFont(const std::vector<std::filesystem::path>& candidates) {
  for (const std::filesystem::path& candidate : candidates) {
    if (std::filesystem::exists(candidate)) { return candidate.string(); }
  }
  return {};
}

Font LoadRayFont(const std::vector<std::filesystem::path>& candidates) {
  std::vector<int> codepoints;
  for (int cp = 32; cp <= 126; ++cp) { codepoints.push_back(cp); }
  for (int cp = 160; cp <= 255; ++cp) { codepoints.push_back(cp); }
  for (int cp : {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2026,
                 0x2190, 0x2191, 0x2192, 0x2193}) {
    codepoints.push_back(cp);
  }
  for (const std::filesystem::path& candidate : candidates) {
    if (!std::filesystem::exists(candidate)) { continue; }
    Font font = LoadFontEx(candidate.string().c_str(), 48, codepoints.data(),
                           static_cast<int>(codepoints.size()));
    if (font.texture.id != 0) {
      SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
      return font;
    }
  }
  return Font{};
}

FontResources LoadFonts(const std::filesystem::path& assets) {
  const std::filesystem::path fonts = assets / "fonts";
  const std::vector<std::filesystem::path> sans = {
      "C:/Windows/Fonts/segoeui.ttf", fonts / "DejaVuSans.ttf"};
  const std::vector<std::filesystem::path> bold = {
      "C:/Windows/Fonts/segoeuib.ttf", fonts / "DejaVuSans-Bold.ttf"};
  const std::vector<std::filesystem::path> mono = {
      "C:/Windows/Fonts/consola.ttf", fonts / "DejaVuSansMono.ttf"};
  const std::string sansPath = FirstFont(sans);
  const std::string boldPath = FirstFont(bold);
  const std::string monoPath = FirstFont(mono);

  FontResources loaded;
  loaded.body = LoadRayFont(sans);
  loaded.bold = LoadRayFont(bold);
  loaded.mono = LoadRayFont(mono);
  if (loaded.body.texture.id == 0) { loaded.body = loaded.bold; }
  if (loaded.bold.texture.id == 0) { loaded.bold = loaded.body; }
  if (loaded.mono.texture.id == 0) { loaded.mono = loaded.body; }

  ImGuiIO& io = ImGui::GetIO();
  // Include the +2px larger-reading preference, not only the four steps.
  // Even without an installed font file the ImGui fallback must stay >=18px.
  for (int i = 0; i < 6; ++i) {
    ImFontConfig config;
    config.SizePixels = static_cast<float>(18 + i);
    loaded.imguiBody[static_cast<std::size_t>(i)] = sansPath.empty()
        ? io.Fonts->AddFontDefault(&config)
        : io.Fonts->AddFontFromFileTTF(sansPath.c_str(), config.SizePixels);
  }
  if (!monoPath.empty()) { io.Fonts->AddFontFromFileTTF(monoPath.c_str(), 18.0f); }
  std::printf("fonts: %s | %s | %s\n", sansPath.c_str(), boldPath.c_str(), monoPath.c_str());
  return loaded;
}

void ReloadImGuiFonts() {
  // rlImGui 6.0 has no rlImGuiReloadFonts() entry point. Building the atlas here
  // is the supported equivalent; its raylib backend uploads the texture on the
  // first frame, including every TTF size added above.
  ImGui::GetIO().Fonts->Build();
}

void SelectImGuiFont(const FontResources& fonts, int fontStep, bool largeText) {
  const int clamped = fontStep < 0 ? 0 : (fontStep > 3 ? 3 : fontStep);
  ImFont* selected = fonts.imguiBody[static_cast<std::size_t>(clamped + (largeText ? 2 : 0))];
  if (selected != nullptr) { ImGui::GetIO().FontDefault = selected; }
}

Texture2D LoadTitleTexture(const std::filesystem::path& assets) {
  const char* appDirectory = GetApplicationDirectory();
  const std::filesystem::path appDir =
      appDirectory != nullptr && *appDirectory != '\0'
          ? std::filesystem::path(appDirectory)
          : std::filesystem::current_path();
  const std::vector<std::filesystem::path> candidates = {
      assets / "art" / "title.jpg", appDir / "assets" / "art" / "title.jpg",
      std::filesystem::current_path() / "assets" / "art" / "title.jpg",
      std::filesystem::current_path() / "spaceship.png",
      assets / "art" / "spaceship.png", appDir / "spaceship.png"};
  for (const std::filesystem::path& candidate : candidates) {
    if (!std::filesystem::exists(candidate)) { continue; }
    Texture2D texture = LoadTexture(candidate.string().c_str());
    if (texture.id != 0) {
      SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
      std::printf("title artwork: %s\n", candidate.string().c_str());
      return texture;
    }
    std::fprintf(stderr, "spaceship: title artwork exists but could not be decoded/uploaded: %s\n",
                 candidate.string().c_str());
  }
  std::fprintf(stderr, "spaceship: no usable title artwork\n");
  return Texture2D{};
}

// The calibrated default needs the shader. Only an identity transform can
// bypass the render target. The shader and target cover the complete frame
// (including ImGui, title art and popups), rather than changing the theme.
class DisplayAdjustment {
 public:
  void Init(int width, int height) {
    static const char* fragment = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float displayGamma;
uniform float displayBrightness;
out vec4 finalColor;
void main() {
  vec4 colour = texture(texture0, fragTexCoord) * colDiffuse * fragColor;
  finalColor = vec4(clamp(pow(clamp(colour.rgb, 0.0, 1.0), vec3(displayGamma)) * displayBrightness, 0.0, 1.0), colour.a);
}
)GLSL";
    shader_ = LoadShaderFromMemory(nullptr, fragment);
    gammaLoc_ = GetShaderLocation(shader_, "displayGamma");
    brightnessLoc_ = GetShaderLocation(shader_, "displayBrightness");
    shaderReady_ = IsShaderValid(shader_) && gammaLoc_ >= 0 && brightnessLoc_ >= 0;
    if (!shaderReady_) {
      std::fprintf(stderr, "spaceship: display adjustment shader unavailable; using unadjusted display\n");
      return;
    }
    Resize(width, height);
  }
  void Resize(int width, int height) {
    if (!shaderReady_ || width < 1 || height < 1) return;
    if (width == width_ && height == height_) return;
    if (target_.id != 0) UnloadRenderTexture(target_);
    target_ = LoadRenderTexture(width, height);
    width_ = width; height_ = height;
    if (!IsRenderTextureValid(target_)) {
      std::fprintf(stderr, "spaceship: display adjustment render target unavailable\n");
    }
  }
  bool Ready() const { return shaderReady_ && IsRenderTextureValid(target_); }
  void Begin() const { BeginTextureMode(target_); }
  void End(double gamma, double brightness) const {
    EndTextureMode();
    const float g = static_cast<float>(ship::EffectiveDisplayGamma(gamma));
    const float b = static_cast<float>(ship::DisplayBrightness(brightness));
    SetShaderValue(shader_, gammaLoc_, &g, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, brightnessLoc_, &b, SHADER_UNIFORM_FLOAT);
    BeginShaderMode(shader_);
    DrawTexturePro(target_.texture,
                   Rectangle{0, 0, static_cast<float>(width_), -static_cast<float>(height_)},
                   Rectangle{0, 0, static_cast<float>(width_), static_cast<float>(height_)},
                   Vector2{0, 0}, 0, WHITE);
    EndShaderMode();
  }
  void Shutdown() {
    if (target_.id != 0) UnloadRenderTexture(target_);
    if (shader_.locs != nullptr) UnloadShader(shader_);
  }
 private:
  Shader shader_{};
  RenderTexture2D target_{};
  int gammaLoc_ = -1, brightnessLoc_ = -1;
  int width_ = 0, height_ = 0;
  bool shaderReady_ = false;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--check-music-file") {
    // Decodes with the same raylib codecs as music streams; no sound device or
    // graphics context is required. Keep the argument relative to launch CWD.
    Wave wave = LoadWave(argv[2]);
    const bool decoded = wave.data != nullptr && wave.frameCount > 0;
    std::printf("music decode: %s (%u frames): %s\n", decoded ? "PASS" : "FAIL", wave.frameCount, argv[2]);
    if (wave.data != nullptr) UnloadWave(wave);
    return decoded ? 0 : 1;
  }

  // This CPU-only probe exercises the *linked raylib decoder*, not a separate
  // JPEG library or an existence check. Run it against the packaged executable
  // in CI before opening any window or initializing graphics/audio.
  std::error_code cwdError;
  const std::filesystem::path launchAssets = std::filesystem::current_path(cwdError) / "assets";
  const std::filesystem::path assets = FindAssetDir();
  if (argc == 2 && std::string(argv[1]) == "--check-title-art") {
    const auto path = assets / "art" / "title.jpg";
    Image image = LoadImage(path.string().c_str());
    const bool decoded = image.data != nullptr && image.width > 0 && image.height > 0;
    std::printf("title JPEG decode: %s (%d x %d): %s\n",
                decoded ? "PASS" : "FAIL", image.width, image.height, path.string().c_str());
    if (image.data != nullptr) { UnloadImage(image); }
    return decoded ? 0 : 1;
  }

  if (argc == 2 && std::string(argv[1]) == "--check-ship-art") {
    const auto path = assets / "art" / "ships" / "wren-exterior.png";
    Image image = LoadImage(path.string().c_str());
    const bool decoded = image.data != nullptr && image.width > 0 && image.height > 0;
    std::printf("Wren PNG decode: %s (%d x %d): %s\n",
                decoded ? "PASS" : "FAIL", image.width, image.height, path.string().c_str());
    if (image.data != nullptr) { UnloadImage(image); }
    return decoded ? 0 : 1;
  }

  // ---- catalogue ---------------------------------------------------------

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

  // ---- the voyage --------------------------------------------------------
  ship::Simulation sim = ship::Simulation::NewVoyage(catalog);
  const char* appDir = GetApplicationDirectory();
  const std::filesystem::path saveDir = appDir != nullptr && *appDir != '\0'
      ? std::filesystem::path(appDir) / "runtime" / "saves"
      : std::filesystem::path("runtime") / "saves";
  std::string loadedPath, loadWarning;
  const auto autoLoad = ship::LoadNewestSave(sim, saveDir.u8string(), &loadedPath, &loadWarning);
  if (!loadWarning.empty()) SaveWarning(loadWarning +
      (autoLoad == ship::AutoLoadResult::Loaded ? "\nAn older valid save was loaded instead." :
       "\nYour files were not changed. You can start a new voyage or load a slot manually."));
  bool voyageStarted = autoLoad == ship::AutoLoadResult::Loaded;

  // ---- window ------------------------------------------------------------
  SetTraceLogLevel(LOG_WARNING);
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
  InitWindow(kDefaultWidth, kDefaultHeight, "Spaceship Command");
  SetWindowMinSize(1180, 720);
  SetTargetFPS(60);
  SetExitKey(KEY_NULL);

  ship::Theme theme = ship::Theme::ById(sim.State().options.theme);
  theme.fontStep = sim.State().options.fontStep;   // Phase 9's reading size
  theme.largeText = sim.State().options.largeText;
  theme.hideCrewLabels = sim.State().options.hideCrewLabels;
  theme.reducedMotion = sim.State().reducedMotion;
  theme.stageCompact = sim.State().shipClass == "wren";

  // rlImGui owns the ImGui context, so add the four reading sizes between its
  // two init halves. The renderer builds/uploads the complete atlas afterward.
  rlImGuiBeginInitImGui();
  const FontResources fonts = LoadFonts(assets);
  rlImGuiEndInitImGui();
  ReloadImGuiFonts();
  SelectImGuiFont(fonts, sim.State().options.fontStep, sim.State().options.largeText);
  ship::ui::ApplyPalette(theme);

  Texture2D titleTexture = LoadTitleTexture(assets);
  Texture2D wrenExterior{};
  const auto exteriorPath = assets / "art" / "ships" / "wren-exterior.png";
  if (std::filesystem::exists(exteriorPath)) {
    wrenExterior = LoadTexture(exteriorPath.string().c_str());
    if (wrenExterior.id != 0) SetTextureFilter(wrenExterior, TEXTURE_FILTER_BILINEAR);
  }
  if (wrenExterior.id == 0) {
    std::fprintf(stderr, "spaceship: Wren exterior unavailable; using deck plan\n");
  }
  std::array<Texture2D, 6> tutorialTextures{};
  for (std::size_t i = 0; i < tutorialTextures.size(); ++i) {
    const auto path = assets / "art" / "tutorial" / ("spaceship0" + std::to_string(i + 1) + ".png");
    if (std::filesystem::exists(path)) {
      tutorialTextures[i] = LoadTexture(path.string().c_str());
      if (tutorialTextures[i].id != 0) { SetTextureFilter(tutorialTextures[i], TEXTURE_FILTER_BILINEAR); }
    }
    if (tutorialTextures[i].id == 0) { std::fprintf(stderr, "spaceship: tutorial card unavailable: %s\n", path.string().c_str()); }
  }
  ship::ShellView shell;
  shell.SetSaveDir(saveDir.u8string());
  shell.SetFonts(fonts.body, fonts.bold, fonts.mono);
  shell.SetTitleTexture(&titleTexture);
  shell.SetWrenExteriorTexture(&wrenExterior);
  for (std::size_t i = 0; i < tutorialTextures.size(); ++i) { shell.SetTutorialTexture(i, &tutorialTextures[i]); }
  shell.Configure(static_cast<float>(GetScreenWidth()),
                  static_cast<float>(GetScreenHeight()), theme);

  DisplayAdjustment display;
  display.Init(GetScreenWidth(), GetScreenHeight());
  shell.SetDisplayAdjustmentAvailable(display.Ready());

  // Sound is optional: a machine without a device keeps a silent, working ship.
  audio::AudioDevice audio;
  const bool audioReady = audio.Start(assets.string(), launchAssets.string());
  audio.Configure(sim.State().options);
  std::printf("audio: %zu music file(s), %zu sound file(s)\n",
              audio.External().music.size(), audio.External().sfx.size());

  // The reference opens on its title menu, with the voyage already begun.
  if (voyageStarted) shell.HideTitle();
  else shell.ShowTitle(false);

  // Phase 9: the borderless window follows Options, whichever way it changes.
  bool fullscreen = sim.State().options.fullscreen;
  if (fullscreen != IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) {
    ToggleBorderlessWindowed();
  }

  ship::PointerOwnership leftPointer, rightPointer;
  while (!WindowShouldClose() && !shell.QuitRequested()) {
    if (IsKeyPressed(KEY_ESCAPE)) { shell.Escape(sim); }
    if (IsKeyPressed(KEY_LEFT)) { shell.PageTutorial(-1, GetTime() * 1000.0); }
    if (IsKeyPressed(KEY_RIGHT)) { shell.PageTutorial(1, GetTime() * 1000.0); }
    display.Resize(GetScreenWidth(), GetScreenHeight());
    shell.SetDisplayAdjustmentAvailable(display.Ready());
    BeginDrawing();
    // rlImGui's event processing and frame setup happen inside the active
    // raylib drawing frame, matching its documented render-loop contract.
    rlImGuiBegin();

    // Dear ImGui owns a complete press/release gesture. A close button may
    // remove its window before release, but that release must not hit the ship.
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
      leftPointer.Press(ImGui::GetIO().WantCaptureMouse);
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
      rightPointer.Press(ImGui::GetIO().WantCaptureMouse);
    }
    const Vector2 pointer = GetMousePosition();
    shell.Move(pointer.x, pointer.y);
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !leftPointer.UiOwned()) {
      const Vector2 delta = GetMouseDelta();
      shell.Drag(sim, pointer.x, pointer.y, delta.x, delta.y, true,
                 IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
    }
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) { shell.Scroll(pointer.x, pointer.y, wheel); }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
      const Vector2 mouse = GetMousePosition();
      if (!leftPointer.Release()) {
        shell.Click(sim, mouse.x, mouse.y, ship::HitButton::Left,
                    static_cast<double>(GetTime()) * 1000.0);
      }
    } else if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
      const Vector2 mouse = GetMousePosition();
      if (!rightPointer.Release()) {
        shell.Click(sim, mouse.x, mouse.y, ship::HitButton::Right,
                    static_cast<double>(GetTime()) * 1000.0);
      }
    }

    // Apply settings from either the title sheet or the in-game Options panel
    // before rendering this frame, so palettes and type sizes change at once.
    ship::Theme nextTheme = ship::Theme::ById(sim.State().options.theme);
    nextTheme.fontStep = sim.State().options.fontStep;
    nextTheme.largeText = sim.State().options.largeText;
    nextTheme.hideCrewLabels = sim.State().options.hideCrewLabels;
    nextTheme.reducedMotion = sim.State().reducedMotion;
    nextTheme.stageCompact = sim.State().shipClass == "wren";
    const bool themeChanged = nextTheme.id != theme.id ||
                              nextTheme.fontStep != theme.fontStep ||
                              nextTheme.largeText != theme.largeText ||
                              nextTheme.hideCrewLabels != theme.hideCrewLabels ||
                              nextTheme.reducedMotion != theme.reducedMotion ||
                              nextTheme.stageCompact != theme.stageCompact;
    const bool resized = IsWindowResized();
    if (themeChanged || resized) {
      theme = nextTheme;
      shell.Configure(static_cast<float>(GetScreenWidth()),
                      static_cast<float>(GetScreenHeight()), theme);
      ship::ui::ApplyPalette(theme);
    }
    SelectImGuiFont(fonts, sim.State().options.fontStep, sim.State().options.largeText);

    if (sim.State().options.fullscreen != fullscreen) {
      fullscreen = sim.State().options.fullscreen;
      ToggleBorderlessWindowed();
    }

    audio.Configure(sim.State().options);
    audio.Tick(sim);
    shell.SetAudioStatus(audio.Status());

    const bool adjusted = display.Ready() &&
        (ship::EffectiveDisplayGamma(sim.State().options.displayGamma) != 1.0 ||
         ship::DisplayBrightness(sim.State().options.displayBrightness) != 1.0);
    if (adjusted) {
      display.Begin();
      // The offscreen texture uses logical screen pixels even on Retina
      // displays; rlImGui otherwise scissors as if it were the HiDPI window.
      ImGui::GetIO().DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    }
    shell.Draw(sim, static_cast<double>(GetTime()) * 1000.0);
    if (!shell.TitleActive()) voyageStarted = true;
    rlImGuiEnd();
    if (adjusted) display.End(sim.State().options.displayGamma, sim.State().options.displayBrightness);
    EndDrawing();
  }

  if (voyageStarted) {
    std::string saved, error;
    if (!ship::SaveDated(sim, saveDir.u8string(), &saved, &error)) {
      SaveWarning("Autosave failed. Your earlier saves have not been changed.\n" + error);
    } else {
      std::printf("autosaved: %s\n", saved.c_str());
    }
  }
  audio.Shutdown();
  rlImGuiShutdown();
  display.Shutdown();
  if (titleTexture.id != 0) { UnloadTexture(titleTexture); }
  if (wrenExterior.id != 0) { UnloadTexture(wrenExterior); }
  for (auto texture : tutorialTextures) { if (texture.id != 0) { UnloadTexture(texture); } }
  if (fonts.body.texture.id != 0) { UnloadFont(fonts.body); }
  if (fonts.bold.texture.id != 0 && fonts.bold.texture.id != fonts.body.texture.id) {
    UnloadFont(fonts.bold);
  }
  if (fonts.mono.texture.id != 0 && fonts.mono.texture.id != fonts.body.texture.id &&
      fonts.mono.texture.id != fonts.bold.texture.id) {
    UnloadFont(fonts.mono);
  }
  CloseWindow();
  (void)audioReady;
  return 0;
}
