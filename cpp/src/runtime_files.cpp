#include "runtime_files.hpp"

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <string>

namespace spaceship {
namespace {
bool writable(const std::filesystem::path& dir) {
  try {
    std::filesystem::create_directories(dir);
    const auto probe =
        dir / (".write-check-" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (std::filesystem::exists(probe)) return false;
    std::ofstream file(probe, std::ios::binary);
    if (!file) return false;
    file << "test";
    file.close();
    const bool ok = !file.fail();
    std::filesystem::remove(probe);
    return ok;
  } catch (...) {
    return false;
  }
}
unsigned u16(const std::vector<unsigned char>& b, size_t i) {
  return b[i] | (unsigned(b[i + 1]) << 8);
}
unsigned u32(const std::vector<unsigned char>& b, size_t i) {
  return u16(b, i) | (u16(b, i + 2) << 16);
}
bool tag(const std::vector<unsigned char>& b, size_t i, const char* s) {
  return std::string(b.begin() + i, b.begin() + i + 4) == s;
}
}  // namespace
RuntimeFiles RuntimeFiles::prepare(const std::filesystem::path& exe,
                                   const std::filesystem::path& user) {
  RuntimeFiles files{exe / "music", exe / "sounds", exe / "saves", user};
  if (!writable(files.saves)) {
    files.saves = user / "saves";
    if (!writable(files.saves)) throw std::runtime_error("No writable save folder is available.");
  }
  // Audio is optional; a read-only install must still run using shipped assets.
  std::error_code ec;
  std::filesystem::create_directories(files.music, ec);
  ec.clear();
  std::filesystem::create_directories(files.sounds, ec);
  return files;
}
std::filesystem::path RuntimeFiles::slot(int n, bool loading) const {
  if (n < 1 || n > 3) throw std::runtime_error("Choose save slot 1, 2 or 3.");
  auto name = "native-slot-" + std::to_string(n) + ".json";
  auto path = saves / name;
  if (loading && !std::filesystem::exists(path) && std::filesystem::exists(legacySaves / name))
    return legacySaves / name;
  return path;
}
WaveClip readWave(const std::filesystem::path& path) {
  try {
    const auto size = std::filesystem::file_size(path);
    if (size < 44 || size > 32 * 1024 * 1024) return {};
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> b(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(size))) return {};
    if (!tag(b, 0, "RIFF") || !tag(b, 8, "WAVE") || uint64_t(u32(b, 4)) + 8 != size) return {};
    WaveClip clip;
    bool format = false, data = false;
    for (size_t i = 12; i + 8 <= b.size();) {
      const size_t count = u32(b, i + 4), begin = i + 8;
      if (count > b.size() - begin) return {};
      if (tag(b, i, "fmt ")) {
        if (format || count < 16 || u16(b, begin) != 1 || u16(b, begin + 14) != 16) return {};
        clip.channels = u16(b, begin + 2);
        clip.rate = u32(b, begin + 4);
        if ((clip.channels != 1 && clip.channels != 2) || clip.rate < 8000 || clip.rate > 192000 ||
            u16(b, begin + 12) != clip.channels * 2 ||
            u32(b, begin + 8) != clip.rate * clip.channels * 2)
          return {};
        format = true;
      } else if (tag(b, i, "data")) {
        if (data || count == 0) return {};
        clip.samples.assign(b.begin() + begin, b.begin() + begin + count);
        data = true;
      }
      i = begin + count + (count & 1);
      if (i > b.size()) return {};
    }
    if (!format || !data || clip.samples.size() % (clip.channels * 2)) return {};
    return clip;
  } catch (...) {
    return {};
  }
}
}  // namespace spaceship
