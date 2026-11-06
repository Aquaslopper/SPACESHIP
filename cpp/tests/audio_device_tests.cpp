#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "audio/AudioDevice.h"
#include "data/Catalog.h"

namespace fake {
bool available = true;
bool playing = false;
bool soundPlaying = false;
bool finish = false;
float volume = -1.0f;
std::vector<std::string> loads;
std::vector<std::string> plays;
std::string current;
void Reset() { available = true; playing = soundPlaying = finish = false; volume = -1; loads.clear(); plays.clear(); current.clear(); }
}
void InitAudioDevice() {}
bool IsAudioDeviceReady() { return fake::available; }
void CloseAudioDevice() {}
Wave LoadWaveFromMemory(const char*, const unsigned char*, int) { Wave w{}; w.frameCount = 10; return w; }
Sound LoadSound(const char*) { return {}; }
Sound LoadSoundFromWave(Wave) { Sound s{}; s.frameCount = 10; return s; }
void UnloadWave(Wave) {}
void UnloadSound(Sound) {}
void PlaySound(Sound) { fake::soundPlaying = true; }
void StopSound(Sound) { fake::soundPlaying = false; }
bool IsSoundPlaying(Sound) { return fake::soundPlaying; }
void SetSoundVolume(Sound, float) {}
Music LoadMusicStream(const char* name) {
  fake::loads.emplace_back(name);
  if (std::string(name).find("bad") != std::string::npos) return {};
  fake::current = name;
  Music m{}; m.frameCount = 10; m.stream.buffer = reinterpret_cast<rAudioBuffer*>(1);
  m.ctxData = reinterpret_cast<void*>(1); m.looping = true; return m;
}
void UnloadMusicStream(Music) { fake::playing = false; }
void PlayMusicStream(Music m) { CHECK_FALSE(m.looping); fake::playing = true; fake::plays.push_back(fake::current); }
bool IsMusicStreamPlaying(Music) { return fake::playing; }
void UpdateMusicStream(Music) { if (fake::finish) { fake::playing = false; fake::finish = false; } }
void StopMusicStream(Music) { fake::playing = false; }
void SetMusicVolume(Music, float value) { fake::volume = value; }

TEST_CASE("external music skips unreadable tracks and advances only after playback ends", "[audio-device]") {
  namespace fs = std::filesystem;
  fake::Reset();
  const fs::path root = fs::temp_directory_path() / "spaceship-audio-device-test";
  std::error_code ec; fs::remove_all(root, ec);
  fs::create_directories(root / "primary" / "audio" / "music");
  fs::create_directories(root / "fallback" / "audio" / "music");
  for (const auto& file : {root / "primary/audio/music/01-bad.mp3", root / "primary/audio/music/02-first.MP3", root / "fallback/audio/music/03-second.wav"}) {
    std::ofstream out(file); out << "fake";
  }
  ship::audio::AudioDevice player;
  REQUIRE(player.Start((root / "primary").string(), (root / "fallback").string()));
  REQUIRE(fake::loads.size() == 2);
  CHECK(player.Status().find("02-first.MP3") != std::string::npos);
  CHECK(player.Status().find("could not be decoded") != std::string::npos);
  ship::Options options;
  options.music = true; options.audioLevel = 0.4;
  player.Configure(options);
  REQUIRE(fake::plays.size() == 1);
  CHECK_THAT(fake::volume, Catch::Matchers::WithinAbs(0.248, 0.00001));
  ship::Catalog catalog = ship::Catalog::LoadFromAssets("assets");
  ship::Simulation sim = ship::Simulation::NewVoyage(catalog);
  player.Tick(sim);
  player.Configure(options);
  CHECK(fake::plays.size() == 1);
  fake::finish = true;
  player.Tick(sim);
  REQUIRE(fake::plays.size() == 2);
  CHECK(fake::plays.back().find("03-second.wav") != std::string::npos);
  fake::finish = true;
  player.Tick(sim);
  REQUIRE(fake::plays.size() == 3);
  CHECK(fake::plays.back().find("02-first.MP3") != std::string::npos);
  options.music = false;
  player.Configure(options);
  CHECK_FALSE(fake::playing);
  CHECK(player.Status().find("music is off") != std::string::npos);
  player.Tick(sim);
  CHECK(fake::plays.size() == 3);
  options.music = true;
  player.Configure(options);
  CHECK(fake::plays.size() == 4);
  player.Shutdown();
  fs::remove_all(root, ec);
}

TEST_CASE("music fallback and unavailable audio are explained", "[audio-device]") {
  namespace fs = std::filesystem;
  fake::Reset();
  const fs::path root = fs::temp_directory_path() / "spaceship-audio-fallback-test";
  std::error_code ec; fs::remove_all(root, ec);
  fs::create_directories(root / "audio/music");
  { std::ofstream out(root / "audio/music/bad.ogg"); out << "fake"; }
  ship::audio::AudioDevice player;
  REQUIRE(player.Start(root.string()));
  CHECK(player.Status().find("could not be decoded") != std::string::npos);
  CHECK(player.Status().find("built-in") != std::string::npos);
  CHECK(player.Status().find(root.string()) != std::string::npos);
  ship::Options options; options.music = true;
  player.Configure(options);
  CHECK(fake::soundPlaying);
  player.Shutdown();
  fake::Reset(); fake::available = false;
  ship::audio::AudioDevice silent;
  CHECK_FALSE(silent.Start(root.string()));
  CHECK(silent.Status().find("Audio device unavailable") != std::string::npos);
  silent.Shutdown();
  fs::remove_all(root, ec);
}
