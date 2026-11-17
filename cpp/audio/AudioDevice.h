// ============================================================================
//  AudioDevice.h — the simple audio system.
//  ---------------------------------------------------------------------------
//  Streams whatever music files are in assets/audio/music and plays the sfx
//  files in assets/audio/sfx when the simulation log gains a line. Missing
//  files stay silent; a machine without an audio device keeps a working,
//  soundless ship. Options that reach here (audioLevel, soundEffects, music)
//  are applied only when they change, never per frame.
// ============================================================================
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "raylib.h"
#include "core/Simulation.h"

namespace ship::audio {

// Pure filesystem inspection of assets/audio. A missing directory is an empty
// result, not an error.
struct ExternalAudio {
  std::vector<std::string> music;                 // paths, sorted
  std::map<std::string, std::string> sfx;         // cue kind -> the file found
  bool any() const { return !music.empty() || !sfx.empty(); }
};

class AudioDevice {
 public:
  bool Start(const std::string& assetsDir, const std::string& fallbackAssetsDir = {});
  void Shutdown();
  void Configure(const Options& options);
  void Cue(const std::string& detail);
  void Tick(const Simulation& sim);
  const ExternalAudio& External() const { return external_; }
  std::string Status() const;

 private:
  bool LoadTrack(std::size_t start);

  static constexpr std::size_t kCueCount = 4;
  static constexpr const char* kKinds[kCueCount] = {"click", "ping", "thrust", "chime"};

  bool ready_ = false;
  bool musicStarted_ = false;
  bool rejectedMusic_ = false;
  bool effectsOn_ = true;
  bool applied_ = false;
  Options appliedOptions_{};
  double master_ = 0.0;
  ExternalAudio external_;
  std::vector<std::string> tracks_;
  std::string musicFolder_;
  std::string rejectedTrack_;
  std::size_t track_ = 0;
  Music streamed_{};
  Sound cues_[kCueCount]{};
  std::size_t logSeen_ = 0;
};

}  // namespace ship::audio
