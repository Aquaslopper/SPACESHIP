#include "audio/AudioDevice.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace ship::audio {

bool AudioDevice::Start(const std::string& assetsDir, const std::string& fallbackAssetsDir) {
  musicFolder_ = (std::filesystem::path(assetsDir) / "audio" / "music").string();
  InitAudioDevice();
  if (!IsAudioDeviceReady()) return false;
  ready_ = true;
  external_ = FindExternalAudio(assetsDir);
  if (!fallbackAssetsDir.empty() &&
      std::filesystem::path(fallbackAssetsDir).lexically_normal() !=
          std::filesystem::path(assetsDir).lexically_normal()) {
    const ExternalAudio fallback = FindExternalAudio(fallbackAssetsDir);
    // Installed assets take precedence; a source-tree music folder still works
    // when launching a build from the repository root.
    external_.music.insert(external_.music.end(), fallback.music.begin(), fallback.music.end());
    for (const auto& effect : fallback.sfx) external_.sfx.emplace(effect);
  }
  for (std::size_t i = 0; i < kCueCount; ++i) {
    const auto found = external_.sfx.find(kKinds[i]);
    if (found != external_.sfx.end()) cues_[i] = LoadSound(found->second.c_str());
    if (cues_[i].frameCount == 0) cues_[i] = BuildSound(RenderCue(kKinds[i]));
  }
  tracks_ = external_.music;
  if (tracks_.empty() || !LoadTrack(0)) StartFallback();
  return true;
}

bool AudioDevice::LoadTrack(std::size_t start) {
  if (tracks_.empty()) return false;
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
    if (next.ctxData != nullptr || next.stream.buffer != nullptr) UnloadMusicStream(next);
    rejectedMusic_ = true;
    if (rejectedTrack_.empty()) rejectedTrack_ = tracks_[candidate];
    std::fprintf(stderr, "spaceship: could not decode music file: %s\n", tracks_[candidate].c_str());
  }
  return false;
}

void AudioDevice::StartFallback() {
  tracks_.clear();
  music_ = BuildSound(RenderMusic());
  musicStarted_ = false;
}

void AudioDevice::Shutdown() {
  if (!ready_) return;
  for (Sound& cue : cues_) if (cue.frameCount > 0) UnloadSound(cue);
  if (music_.frameCount > 0) UnloadSound(music_);
  if (streamed_.ctxData != nullptr || streamed_.stream.buffer != nullptr) UnloadMusicStream(streamed_);
  CloseAudioDevice();
  ready_ = false;
}

void AudioDevice::Configure(const Options& options) {
  if (!ready_) return;
  gains_ = GainsFor(options);
  for (Sound& cue : cues_) {
    if (cue.frameCount > 0) SetSoundVolume(cue, static_cast<float>(gains_.master * gains_.effects));
  }
  if (streamed_.frameCount > 0) {
    // Custom recordings are full music tracks, not the quiet synthesizer's
    // internal voices. The same master control still caps playback at 0.62.
    SetMusicVolume(streamed_, static_cast<float>(gains_.master));
    if (gains_.musicOn && !musicStarted_) { PlayMusicStream(streamed_); musicStarted_ = true; }
    if (!gains_.musicOn && musicStarted_) { StopMusicStream(streamed_); musicStarted_ = false; }
  }
  if (music_.frameCount > 0) {
    SetSoundVolume(music_, static_cast<float>(gains_.master * gains_.music));
    if (gains_.musicOn && !IsSoundPlaying(music_)) PlaySound(music_);
    if (!gains_.musicOn && IsSoundPlaying(music_)) StopSound(music_);
  }
}

void AudioDevice::Cue(const std::string& detail) {
  if (!ready_ || !gains_.effectsOn) return;
  const std::string kind = CueKindFor(detail);
  for (std::size_t i = 0; i < kCueCount; ++i) {
    if (kind == kKinds[i]) {
      if (cues_[i].frameCount > 0) PlaySound(cues_[i]);
      return;
    }
  }
}

void AudioDevice::Tick(const Simulation& sim) {
  if (!ready_) return;
  if (streamed_.frameCount > 0 && musicStarted_ && gains_.musicOn) {
    UpdateMusicStream(streamed_);
    if (!IsMusicStreamPlaying(streamed_)) {
      const std::size_t next = (track_ + 1) % tracks_.size();
      UnloadMusicStream(streamed_);
      streamed_ = {};
      musicStarted_ = false;
      if (LoadTrack(next)) {
        SetMusicVolume(streamed_, static_cast<float>(gains_.master));
        PlayMusicStream(streamed_);
        musicStarted_ = true;
      } else {
        StartFallback();
        SetSoundVolume(music_, static_cast<float>(gains_.master * gains_.music));
        if (music_.frameCount > 0) PlaySound(music_);
      }
    }
  }
  const auto& log = sim.State().feed;
  if (log.empty()) return;
  if (logSeen_ == 0 || log.size() < logSeen_) { logSeen_ = log.size(); return; }
  for (std::size_t i = logSeen_; i < log.size(); ++i) Cue(log[i].text);
  logSeen_ = log.size();
}

std::string AudioDevice::Status() const {
  if (!ready_) return "Audio device unavailable. Check your system audio output.";
  if (streamed_.frameCount > 0 && !tracks_.empty()) {
    return std::string(gains_.musicOn ? "Custom music: " : "Custom music selected (music is off): ") +
           tracks_[track_] +
           (rejectedMusic_ ? ". This file could not be decoded: " + rejectedTrack_ : ".");
  }
  return (rejectedMusic_ ? "This file could not be decoded: " + rejectedTrack_ + ". " : "No custom music found. ") +
         "Using the built-in soundtrack. Put .ogg, .mp3 or .wav files in " + musicFolder_ +
         " and restart the game.";
}

Sound AudioDevice::BuildSound(const Buffer& buffer) {
  const std::vector<unsigned char> bytes = ToWav(buffer);
  Wave wave = LoadWaveFromMemory(".wav", bytes.data(), static_cast<int>(bytes.size()));
  if (wave.frameCount == 0) return Sound{};
  Sound sound = LoadSoundFromWave(wave);
  UnloadWave(wave);
  return sound;
}

}  // namespace ship::audio
