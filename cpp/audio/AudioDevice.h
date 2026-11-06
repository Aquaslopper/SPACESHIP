#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "raylib.h"
#include "audio/Audio.h"
#include "core/Simulation.h"

namespace ship::audio {

// Raylib playback adapter. The simulation is never advanced by audio. Resource
// discovery and the active track are exposed to the settings screen so an
// unreadable file cannot silently masquerade as the built-in soundtrack.
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
  static Sound BuildSound(const Buffer& buffer);
  bool LoadTrack(std::size_t start);
  void StartFallback();
  static constexpr std::size_t kCueCount = 4;
  static constexpr const char* kKinds[kCueCount] = {"click", "ping", "thrust", "chime"};
  bool ready_ = false;
  bool musicStarted_ = false;
  bool rejectedMusic_ = false;
  Gains gains_;
  ExternalAudio external_;
  std::vector<std::string> tracks_;
  std::string musicFolder_;
  std::string rejectedTrack_;
  std::size_t track_ = 0;
  Music streamed_{};
  Sound cues_[4]{};
  Sound music_{};
  std::size_t logSeen_ = 0;
};

}  // namespace ship::audio
