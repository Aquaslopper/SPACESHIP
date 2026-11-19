// ============================================================================
//  AudioDevice.cpp — the simple audio system.
// ============================================================================

#include "audio/AudioDevice.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace ship::audio {

namespace {

bool Contains(const std::string& haystack, const char* needle) {
  return haystack.find(needle) != std::string::npos;
}

// The detail sentence of a committed action picks one of four cue files.
std::string CueKindFor(const std::string& detail) {
  std::string text = detail;
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (Contains(text, "scan") || Contains(text, "sensor") || Contains(text, "signal") ||
      Contains(text, "comms")) {
    return "ping";
  }
  if (Contains(text, "course") || Contains(text, "travel") || Contains(text, "jump") ||
      Contains(text, "crossing")) {
    return "thrust";
  }
  if (Contains(text, "complete") || Contains(text, "arriv") || Contains(text, "paid")) {
    return "chime";
  }
  return "click";
}

// The names the README promises, then the usual aliases, so a file dropped in
// by hand is found even if it is named after another cue.
const std::vector<std::string>& SfxFileNames(const std::string& kind) {
  static const std::vector<std::string> click = {"click.wav"};
  static const std::vector<std::string> alert = {"alert.wav", "ping.wav"};
  static const std::vector<std::string> action = {"action.wav", "thrust.wav"};
  static const std::vector<std::string> complete = {"complete.wav", "arrive.wav", "chime.wav"};
  static const std::vector<std::string> none;
  if (kind == "click") { return click; }
  if (kind == "ping") { return alert; }
  if (kind == "thrust") { return action; }
  if (kind == "chime") { return complete; }
  return none;
}

bool HasMusicSuffix(const std::string& name) {
  const auto dot = name.find_last_of('.');
  if (dot == std::string::npos) { return false; }
  std::string extension = name.substr(dot);
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return extension == ".ogg" || extension == ".mp3" || extension == ".wav";
}

ExternalAudio ScanExternalAudio(const std::string& assetsDir) {
  namespace fs = std::filesystem;
  ExternalAudio found;
  if (assetsDir.empty()) { return found; }
  const fs::path audioDir = fs::path(assetsDir) / "audio";
  std::error_code ec;

  const fs::path musicDir = audioDir / "music";
  if (fs::is_directory(musicDir, ec)) {
    for (const fs::directory_entry& entry : fs::directory_iterator(musicDir, ec)) {
      if (!entry.is_regular_file(ec)) { continue; }
      const std::string name = entry.path().filename().string();
      if (!HasMusicSuffix(name)) { continue; }
      found.music.push_back(fs::absolute(entry.path()).string());
    }
    std::sort(found.music.begin(), found.music.end());
  }

  const fs::path sfxDir = audioDir / "sfx";
  if (fs::is_directory(sfxDir, ec)) {
    for (const char* kind : {"click", "ping", "thrust", "chime"}) {
      for (const std::string& name : SfxFileNames(kind)) {
        const fs::path candidate = sfxDir / name;
        if (fs::is_regular_file(candidate, ec)) {
          found.sfx[kind] = fs::absolute(candidate).string();
          break;
        }
      }
    }
  }
  return found;
}

double MasterGain(const Options& options) {
  const double level = std::isfinite(options.audioLevel) ? options.audioLevel : 0.0;
  return std::clamp(level, 1.0, 10.0);
}

}  // namespace

bool AudioDevice::Start(const std::string& assetsDir, const std::string& fallbackAssetsDir) {
  musicFolder_ = (std::filesystem::path(assetsDir) / "audio" / "music").string();
  InitAudioDevice();
  if (!IsAudioDeviceReady()) { return false; }
  ready_ = true;
  external_ = ScanExternalAudio(assetsDir);
  if (!fallbackAssetsDir.empty() &&
      std::filesystem::path(fallbackAssetsDir).lexically_normal() !=
          std::filesystem::path(assetsDir).lexically_normal()) {
    const ExternalAudio fallback = ScanExternalAudio(fallbackAssetsDir);
    // Installed assets take precedence; a source-tree music folder still works
    // when launching a build from the repository root.
    external_.music.insert(external_.music.end(), fallback.music.begin(), fallback.music.end());
    for (const auto& effect : fallback.sfx) { external_.sfx.emplace(effect); }
  }
  for (std::size_t i = 0; i < kCueCount; ++i) {
    const auto found = external_.sfx.find(kKinds[i]);
    if (found == external_.sfx.end()) { continue; } 
    cues_[i] = LoadSound(found->second.c_str());
    if (cues_[i].frameCount == 0) { std::fprintf(stderr, "spaceship: could not decode sound: %s\n", found->second.c_str()); }
  }
  tracks_ = external_.music;
  if (!tracks_.empty()) { LoadTrack(0); }
  return true;
}

bool AudioDevice::LoadTrack(std::size_t start) {
  if (tracks_.empty()) { return false; }
  for (std::size_t i = 0; i < tracks_.size(); ++i) {
    const std::size_t candidate = (start + i) % tracks_.size();
    Music next = LoadMusicStream(tracks_[candidate].c_str());
    if (next.frameCount > 0 && next.stream.buffer != nullptr) {
      streamed_ = next;
      // Run the playlist ourselves at end-of-stream; raylib's automatic loop
      // otherwise rewinds before the next track can be selected.
      streamed_.looping = false;
      track_ = candidate;
      return true;
    }
    if (next.ctxData != nullptr || next.stream.buffer != nullptr) { UnloadMusicStream(next); }
    rejectedMusic_ = true;
    if (rejectedTrack_.empty()) { rejectedTrack_ = tracks_[candidate]; }
    std::fprintf(stderr, "spaceship: could not decode music file: %s\n", tracks_[candidate].c_str());
  }
  return false;
}

void AudioDevice::Shutdown() {
  if (!ready_) { return; }
  for (Sound& cue : cues_) { if (cue.frameCount > 0) { UnloadSound(cue); } }
  if (streamed_.ctxData != nullptr || streamed_.stream.buffer != nullptr) { UnloadMusicStream(streamed_); }
  CloseAudioDevice();
  ready_ = false;
}

void AudioDevice::Configure(const Options& options) {
  if (!ready_) { return; }
  // The settings screen calls this every frame; do the work only on change.
  if (applied_ && appliedOptions_.soundEffects == options.soundEffects &&
      appliedOptions_.music == options.music && appliedOptions_.audioLevel == options.audioLevel) {
    return;
  }
  applied_ = true;
  appliedOptions_ = options;
  master_ = MasterGain(options);
  effectsOn_ = options.soundEffects;
  for (Sound& cue : cues_) {
    if (cue.frameCount > 0) { SetSoundVolume(cue, static_cast<float>(master_)); }
  }
  if (streamed_.frameCount > 0) {
    SetMusicVolume(streamed_, static_cast<float>(master_));
    if (options.music && !musicStarted_) { PlayMusicStream(streamed_); musicStarted_ = true; }
    if (!options.music && musicStarted_) { StopMusicStream(streamed_); musicStarted_ = false; }
  }
}

void AudioDevice::Cue(const std::string& detail) {
  if (!ready_ || !effectsOn_) { return; }
  const std::string kind = CueKindFor(detail);
  for (std::size_t i = 0; i < kCueCount; ++i) {
    if (kind == kKinds[i]) {
      if (cues_[i].frameCount > 0) { PlaySound(cues_[i]); }
      return;
    }
  }
}

void AudioDevice::Tick(const Simulation& sim) {
  if (!ready_) { return; }
  if (streamed_.frameCount > 0 && musicStarted_ && appliedOptions_.music) {
    UpdateMusicStream(streamed_);
    if (!IsMusicStreamPlaying(streamed_)) {
      const std::size_t next = (track_ + 1) % tracks_.size();
      UnloadMusicStream(streamed_);
      streamed_ = {};
      musicStarted_ = false;
      if (LoadTrack(next)) {
        SetMusicVolume(streamed_, static_cast<float>(master_));
        PlayMusicStream(streamed_);
        musicStarted_ = true;
      }
    }
  }
  const auto& log = sim.State().feed;
  if (log.empty()) { return; }
  if (logSeen_ == 0 || log.size() < logSeen_) { logSeen_ = log.size(); return; }
  for (std::size_t i = logSeen_; i < log.size(); ++i) { Cue(log[i].text); }
  logSeen_ = log.size();
}

std::string AudioDevice::Status() const {
  if (!ready_) { return "Audio device unavailable. Check your system audio output."; }
  if (streamed_.frameCount > 0 && !tracks_.empty()) {
    return std::string(appliedOptions_.music ? "Custom music: "
                                             : "Custom music selected (music is off): ") +
           tracks_[track_] +
           (rejectedMusic_ ? ". This file could not be decoded: " + rejectedTrack_ : ".");
  }
  return (rejectedMusic_ ? "This file could not be decoded: " + rejectedTrack_ + ". "
                         : "No custom music found. ") +
         "Put .ogg, .mp3 or .wav files in " + musicFolder_ + " and restart the game.";
}

}  // namespace ship::audio
