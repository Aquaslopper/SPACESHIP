#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace spaceship {
struct RuntimeFiles {
  std::filesystem::path music, sounds, saves, legacySaves;
  static RuntimeFiles prepare(const std::filesystem::path& executableDirectory,
                              const std::filesystem::path& userDirectory);
  std::filesystem::path slot(int number, bool loading) const;
};
struct WaveClip {
  unsigned rate = 0, channels = 0;
  std::vector<char> samples;
};
// Optional assets: malformed, unsupported or missing WAVs return an empty clip.
WaveClip readWave(const std::filesystem::path& path);
}  // namespace spaceship
