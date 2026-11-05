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

#include <cstdio>
#include <filesystem>
#include <string>

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
    if (tracks_.size() > 1 && streamed_.frameCount > 0 && IsMusicStreamPlaying(streamed_)) {
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

  // ---- the voyage --------------------------------------------------------
  ship::Simulation sim = ship::Simulation::NewVoyage(catalog);

  // ---- window ------------------------------------------------------------
  SetTraceLogLevel(LOG_WARNING);
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
  InitWindow(kDefaultWidth, kDefaultHeight, "Spaceship Command");
  SetWindowMinSize(1180, 720);
  SetTargetFPS(60);

  ship::Theme theme = ship::Theme::ById(sim.State().options.theme);
  theme.fontStep = sim.State().options.fontStep;   // Phase 9's reading size
  theme.largeText = sim.State().options.largeText;
  theme.hideCrewLabels = sim.State().options.hideCrewLabels;
  theme.reducedMotion = sim.State().reducedMotion;
  theme.stageCompact = sim.State().shipClass == "wren";

  // rlImGui owns the ImGui context, so fonts go in between its two init halves.
  rlImGuiBeginInitImGui();
  LoadFonts(assets);
  rlImGuiEndInitImGui();
  ship::ui::ApplyPalette(theme);

  ship::ShellView shell;
  shell.Configure(static_cast<float>(GetScreenWidth()),
                  static_cast<float>(GetScreenHeight()), theme);

  // Sound is optional: a machine without a device keeps a silent, working ship.
  AudioDevice audio;
  const bool audioReady = audio.Start(assets);
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

  while (!WindowShouldClose()) {
    // Resizing re-measures, exactly as the reference's coalesced handler does.
    // The reading size travels with the theme, so a step change re-measures
    // every row the shell lays out rather than only repainting the faces.
    if (theme.fontStep != sim.State().options.fontStep) {
      theme.fontStep = sim.State().options.fontStep;
      shell.Configure(static_cast<float>(GetScreenWidth()),
                      static_cast<float>(GetScreenHeight()), theme);
    }
    if (sim.State().options.fullscreen != fullscreen) {
      fullscreen = sim.State().options.fullscreen;
      ToggleBorderlessWindowed();
    }
    if (IsWindowResized()) {
      theme.stageCompact = sim.State().shipClass == "wren";
      shell.Configure(static_cast<float>(GetScreenWidth()),
                      static_cast<float>(GetScreenHeight()), theme);
      ship::ui::ApplyPalette(theme);
    }

    // The two verbs. ImGui owns the pointer while a window is up, so the shell
    // is handed a click only when ImGui did not want it.
    const bool overUi = ImGui::GetIO().WantCaptureMouse;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && !overUi) {
      const Vector2 mouse = GetMousePosition();
      shell.Click(sim, mouse.x, mouse.y, ship::HitButton::Left,
                  static_cast<double>(GetTime()) * 1000.0);
    } else if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT) && !overUi) {
      const Vector2 mouse = GetMousePosition();
      shell.Click(sim, mouse.x, mouse.y, ship::HitButton::Right,
                  static_cast<double>(GetTime()) * 1000.0);
    }

    audio.Configure(sim.State().options);
    audio.Tick(sim);

    rlImGuiBegin();
    shell.Draw(sim, static_cast<double>(GetTime()) * 1000.0);
    rlImGuiEnd();
  }

  audio.Shutdown();
  rlImGuiShutdown();
  CloseWindow();
  (void)audioReady;
  return 0;
}
