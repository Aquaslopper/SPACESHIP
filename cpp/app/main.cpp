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
#include "core/Simulation.h"
#include "data/Catalog.h"
#include "ui/Draw.h"
#include "ui/Palette.h"
#include "ui/Theme.h"

namespace {

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
    if (selected.empty() && HasCatalog(besideExecutable)) { selected = besideExecutable; }

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
  }
  std::fprintf(stderr, "spaceship: title artwork not found\n");
  return Texture2D{};
}

// ============================================================================
//  AudioDevice — the only place a sound becomes a device. Every buffer comes
//  from audio:: (Section D-3's arithmetic); this class decides when to play it
//  and what the ship's own options allow.
// ============================================================================
class AudioDevice {
 public:
  // Phase 9: the player's own files win. Each cue looks for its file first and
  // falls back to the synthesised buffer; the bed streams the first track in
  // music/, or the built-in sequencer when the folder is empty.
  bool Start(const std::string& assetsDir) {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) { return false; }
    ready_ = true;
    external_ = audio::FindExternalAudio(assetsDir);
    for (std::size_t i = 0; i < kCueCount; ++i) {
      const auto found = external_.sfx.find(kKinds[i]);
      if (found != external_.sfx.end()) {
        cues_[i] = LoadSound(found->second.c_str());
      }
      if (cues_[i].frameCount == 0) {
        cues_[i] = BuildSound(audio::RenderCue(kKinds[i]));
      }
    }
    if (!external_.music.empty()) {
      streamed_ = LoadMusicStream(external_.music.front().c_str());
      if (streamed_.frameCount > 0) {
        streamed_.looping = true;
        tracks_ = external_.music;
      } else {
        tracks_.clear();
      }
    }
    if (tracks_.empty()) { music_ = BuildSound(audio::RenderMusic()); }
    return true;
  }

  // Which files were actually picked up, for the boot log.
  const audio::ExternalAudio& External() const { return external_; }

  void Shutdown() {
    if (!ready_) { return; }
    for (Sound& cue : cues_) {
      if (cue.frameCount > 0) { UnloadSound(cue); }
    }
    if (music_.frameCount > 0) { UnloadSound(music_); }
    if (streamed_.frameCount > 0) { UnloadMusicStream(streamed_); }
    CloseAudioDevice();
    ready_ = false;
  }

  // syncAudioOptions(): the three gains, and starting or stopping the bed.
  void Configure(const Options& options) {
    if (!ready_) { return; }
    const audio::Gains gains = audio::GainsFor(options);
    gains_ = gains;
    for (Sound& cue : cues_) {
      if (cue.frameCount > 0) {
        SetSoundVolume(cue, static_cast<float>(gains.master * gains.effects));
      }
    }
    const float musicVolume = static_cast<float>(gains.master * gains.music);
    if (streamed_.frameCount > 0) {
      SetMusicVolume(streamed_, musicVolume);
      if (gains.musicOn) {
        if (!IsMusicStreamPlaying(streamed_)) { PlayMusicStream(streamed_); }
      } else if (IsMusicStreamPlaying(streamed_)) {
        StopMusicStream(streamed_);
      }
    }
    if (music_.frameCount > 0) {
      SetSoundVolume(music_, musicVolume);
      if (gains.musicOn) {
        if (!IsSoundPlaying(music_)) { PlaySound(music_); }
      } else if (IsSoundPlaying(music_)) {
        StopSound(music_);
      }
    }
  }

  // One committed action: the detail's sentence picks the cue.
  void Cue(const std::string& detail) {
    if (!ready_ || !gains_.effectsOn) { return; }
    const std::string kind = audio::CueKindFor(detail);
    for (std::size_t i = 0; i < kCueCount; ++i) {
      if (kind != kKinds[i]) { continue; }
      if (cues_[i].frameCount > 0) { PlaySound(cues_[i]); }
      return;
    }
  }

  // The log is the ship's voice: every new line is a cue, in order.
  void Tick(const Simulation& sim) {
    if (!ready_) { return; }
    if (!tracks_.empty() && streamed_.frameCount > 0 && IsMusicStreamPlaying(streamed_)) {
      UpdateMusicStream(streamed_);
      // Cycle: a finished track hands over to the next one in filename order.
      if (streamed_.looping && GetMusicTimePlayed(streamed_) >=
                                    GetMusicTimeLength(streamed_) - 0.05f) {
        track_ = (track_ + 1) % tracks_.size();
        StopMusicStream(streamed_);
        UnloadMusicStream(streamed_);
        streamed_ = LoadMusicStream(tracks_[track_].c_str());
        streamed_.looping = true;
        SetMusicVolume(streamed_, static_cast<float>(gains_.master * gains_.music));
        PlayMusicStream(streamed_);
      }
    }
    const std::vector<FeedLine>& log = sim.State().feed;
    if (log.empty()) { return; }
    if (logSeen_ == 0) {
      logSeen_ = log.size();
      return;
    }
    if (log.size() < logSeen_) { logSeen_ = log.size(); return; }  // a load
    for (std::size_t i = logSeen_; i < log.size(); ++i) { Cue(log[i].text); }
    logSeen_ = log.size();
  }

 private:
  static Sound BuildSound(const audio::Buffer& buffer) {
    const std::vector<unsigned char> bytes = audio::ToWav(buffer);
    Wave wave = LoadWaveFromMemory(".wav", bytes.data(), static_cast<int>(bytes.size()));
    if (wave.frameCount == 0) { return Sound{}; }
    Sound sound = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return sound;
  }

  static constexpr std::size_t kCueCount = 4;
  static constexpr const char* kKinds[kCueCount] = {"click", "ping", "thrust", "chime"};
  bool ready_ = false;
  audio::Gains gains_;
  audio::ExternalAudio external_;
  std::vector<std::string> tracks_;   // the player's music, in filename order
  std::size_t track_ = 0;
  Music streamed_{};                  // an external track, streamed
  Sound cues_[4]{};
  Sound music_{};
  std::size_t logSeen_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  // ---- catalogue ---------------------------------------------------------
  const std::filesystem::path assets = FindAssetDir();

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
  ship::ShellView shell;
  shell.SetFonts(fonts.body, fonts.bold, fonts.mono);
  shell.SetTitleTexture(&titleTexture);
  shell.Configure(static_cast<float>(GetScreenWidth()),
                  static_cast<float>(GetScreenHeight()), theme);

  // Sound is optional: a machine without a device keeps a silent, working ship.
  AudioDevice audio;
  const bool audioReady = audio.Start(assets.string());
  audio.Configure(sim.State().options);
  std::printf("audio: %zu music file(s), %zu sound file(s)\n",
              audio.External().music.size(), audio.External().sfx.size());

  // The reference opens on its title menu, with the voyage already begun.
  shell.ShowTitle(false);

  // Phase 9: the borderless window follows Options, whichever way it changes.
  bool fullscreen = sim.State().options.fullscreen;
  if (fullscreen != IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) {
    ToggleBorderlessWindowed();
  }

  while (!WindowShouldClose() && !shell.QuitRequested()) {
    if (IsKeyPressed(KEY_ESCAPE)) { shell.Escape(sim); }
    BeginDrawing();
    // rlImGui's event processing and frame setup happen inside the active
    // raylib drawing frame, matching its documented render-loop contract.
    rlImGuiBegin();

    // Every game control is hit-tested by ShellView in raylib coordinates.
    // Do not gate these clicks on ImGui::WantCaptureMouse: the full-screen
    // ImGui draw-list host can report capture despite owning no game controls.
    // ImGui text fields still receive the event when no shell hit matches.
    const Vector2 pointer = GetMousePosition();
    shell.Move(pointer.x, pointer.y);
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
      const Vector2 delta = GetMouseDelta();
      shell.Drag(sim, pointer.x, pointer.y, delta.x, delta.y, true,
                 IsMouseButtonPressed(MOUSE_BUTTON_LEFT));
    }
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) { shell.Scroll(pointer.x, pointer.y, wheel); }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
      const Vector2 mouse = GetMousePosition();
      shell.Click(sim, mouse.x, mouse.y, ship::HitButton::Left,
                  static_cast<double>(GetTime()) * 1000.0);
    } else if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
      const Vector2 mouse = GetMousePosition();
      shell.Click(sim, mouse.x, mouse.y, ship::HitButton::Right,
                  static_cast<double>(GetTime()) * 1000.0);
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

    shell.Draw(sim, static_cast<double>(GetTime()) * 1000.0);
    rlImGuiEnd();
    EndDrawing();
  }

  audio.Shutdown();
  rlImGuiShutdown();
  if (titleTexture.id != 0) { UnloadTexture(titleTexture); }
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
